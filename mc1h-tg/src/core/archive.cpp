#include "core/archive.h"

#include <sqlite3.h>

#include <cctype>
#include <utility>

namespace mc1h {
namespace {

class Stmt {
 public:
  Stmt(sqlite3* db, const char* sql) {
    if (sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr) != SQLITE_OK) stmt_ = nullptr;
  }
  ~Stmt() { sqlite3_finalize(stmt_); }
  Stmt(const Stmt&) = delete;
  Stmt& operator=(const Stmt&) = delete;

  bool ok() const { return stmt_ != nullptr; }
  Stmt& bind(int i, std::int64_t v) {
    if (stmt_) sqlite3_bind_int64(stmt_, i, v);
    return *this;
  }
  Stmt& bind(int i, const std::string& v) {
    if (stmt_) sqlite3_bind_text(stmt_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
    return *this;
  }
  Stmt& bind_null(int i) {
    if (stmt_) sqlite3_bind_null(stmt_, i);
    return *this;
  }
  int step() { return stmt_ ? sqlite3_step(stmt_) : SQLITE_MISUSE; }
  std::int64_t i64(int col) { return sqlite3_column_int64(stmt_, col); }
  bool is_null(int col) { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }
  std::string text(int col) {
    const unsigned char* p = sqlite3_column_text(stmt_, col);
    int n = sqlite3_column_bytes(stmt_, col);
    return p ? std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)) : std::string();
  }

 private:
  sqlite3_stmt* stmt_ = nullptr;
};

void append_part(std::string& out, const std::string& part) {
  if (part.empty()) return;
  if (!out.empty()) out += '\n';
  out += part;
}

std::string formatted_text(const json& ft) { return jstr(ft, "text"); }

struct SenderInfo {
  std::string kind;
  std::int64_t id = 0;
};

SenderInfo parse_sender(const json& sender) {
  const std::string t = jtype(sender);
  if (t == "messageSenderUser") return {"user", jint(sender, "user_id")};
  if (t == "messageSenderChat") return {"chat", jint(sender, "chat_id")};
  return {};
}

bool is_self_destruct(const json& message) {
  const json& sd = jobj(message, "self_destruct_type");
  if (sd.is_object() && !jtype(sd).empty()) return true;
  return jbool(jobj(message, "content"), "is_secret", false);
}

}  // namespace

std::string extract_text(const json& content) {
  std::string out;
  const std::string type = jtype(content);
  if (type == "messageText") {
    append_part(out, formatted_text(jobj(content, "text")));
  } else if (type == "messagePoll") {
    append_part(out, formatted_text(jobj(jobj(content, "poll"), "question")));
  } else {
    append_part(out, formatted_text(jobj(content, "caption")));
  }
  if (type == "messageDocument") append_part(out, jstr(jobj(content, "document"), "file_name"));
  if (type == "messageAudio") append_part(out, jstr(jobj(content, "audio"), "file_name"));
  return out;
}

std::string to_fts_query(const std::string& user_input) {
  std::string out;
  std::size_t i = 0;
  const std::size_t n = user_input.size();
  while (i < n) {
    while (i < n && std::isspace(static_cast<unsigned char>(user_input[i]))) ++i;
    if (i >= n) break;
    std::string term;
    while (i < n && !std::isspace(static_cast<unsigned char>(user_input[i]))) {
      if (user_input[i] == '"') term += '"';  // FTS5 escapes " inside a string as ""
      term += user_input[i];
      ++i;
    }
    if (!out.empty()) out += ' ';
    out += '"' + term + '"';
  }
  return out;
}

Archive::Archive(sqlite3* db) : db_(db) {}

Archive::~Archive() { sqlite3_close_v2(db_); }

std::unique_ptr<Archive> Archive::open(const std::string& path, std::string* error) {
  sqlite3* db = nullptr;
  int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                           nullptr);
  if (rc != SQLITE_OK) {
    if (error) *error = std::string("sqlite open failed: ") + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
    sqlite3_close_v2(db);
    return nullptr;
  }
  std::unique_ptr<Archive> a(new Archive(db));
  if (!a->init_schema()) {
    if (error) *error = a->last_error_;
    return nullptr;
  }
  return a;
}

bool Archive::exec(const char* sql) {
  char* err = nullptr;
  if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
    last_error_ = err ? err : sqlite3_errmsg(db_);
    sqlite3_free(err);
    return false;
  }
  return true;
}

bool Archive::init_schema() {
  static const char* kSchema = R"SQL(
PRAGMA foreign_keys = ON;
CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS chats(
  chat_id INTEGER PRIMARY KEY,
  title TEXT NOT NULL DEFAULT '',
  type TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS users(
  user_id INTEGER PRIMARY KEY,
  first_name TEXT NOT NULL DEFAULT '',
  last_name TEXT NOT NULL DEFAULT '',
  username TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS messages(
  id INTEGER PRIMARY KEY,
  chat_id INTEGER NOT NULL,
  message_id INTEGER NOT NULL,
  sender_kind TEXT NOT NULL DEFAULT '',
  sender_id INTEGER NOT NULL DEFAULT 0,
  date INTEGER NOT NULL DEFAULT 0,
  edit_date INTEGER NOT NULL DEFAULT 0,
  content_type TEXT NOT NULL DEFAULT '',
  text TEXT NOT NULL DEFAULT '',
  is_outgoing INTEGER NOT NULL DEFAULT 0,
  self_destruct INTEGER NOT NULL DEFAULT 0,
  deleted_at INTEGER,
  edit_count INTEGER NOT NULL DEFAULT 0,
  raw_json TEXT NOT NULL,
  UNIQUE(chat_id, message_id));
CREATE INDEX IF NOT EXISTS messages_by_date ON messages(chat_id, date);
CREATE TABLE IF NOT EXISTS message_edits(
  id INTEGER PRIMARY KEY,
  chat_id INTEGER NOT NULL,
  message_id INTEGER NOT NULL,
  recorded_at INTEGER NOT NULL,
  edit_date INTEGER NOT NULL DEFAULT 0,
  old_text TEXT NOT NULL,
  new_text TEXT NOT NULL,
  old_content_type TEXT NOT NULL,
  new_content_type TEXT NOT NULL);
CREATE INDEX IF NOT EXISTS edits_by_message ON message_edits(chat_id, message_id);
CREATE VIRTUAL TABLE IF NOT EXISTS messages_fts USING fts5(
  text, content='messages', content_rowid='id', tokenize='unicode61 remove_diacritics 2');
CREATE TRIGGER IF NOT EXISTS messages_ai AFTER INSERT ON messages BEGIN
  INSERT INTO messages_fts(rowid, text) VALUES (new.id, new.text);
END;
CREATE TRIGGER IF NOT EXISTS messages_ad AFTER DELETE ON messages BEGIN
  INSERT INTO messages_fts(messages_fts, rowid, text) VALUES ('delete', old.id, old.text);
END;
CREATE TRIGGER IF NOT EXISTS messages_au AFTER UPDATE OF text ON messages BEGIN
  INSERT INTO messages_fts(messages_fts, rowid, text) VALUES ('delete', old.id, old.text);
  INSERT INTO messages_fts(rowid, text) VALUES (new.id, new.text);
END;
INSERT OR IGNORE INTO meta(key, value) VALUES ('schema_version', '1');
)SQL";
  if (!exec("PRAGMA journal_mode = WAL;")) return false;
  return exec(kSchema);
}

bool Archive::record_edit(std::int64_t chat_id, std::int64_t message_id, const std::string& old_text,
                          const std::string& old_type, const std::string& new_text, const std::string& new_type,
                          std::int64_t now) {
  Stmt ins(db_,
           "INSERT INTO message_edits(chat_id, message_id, recorded_at, old_text, new_text, old_content_type, "
           "new_content_type) VALUES (?,?,?,?,?,?,?)");
  ins.bind(1, chat_id).bind(2, message_id).bind(3, now).bind(4, old_text).bind(5, new_text).bind(6, old_type).bind(
      7, new_type);
  if (ins.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  Stmt up(db_, "UPDATE messages SET edit_count = edit_count + 1 WHERE chat_id = ? AND message_id = ?");
  up.bind(1, chat_id).bind(2, message_id);
  if (up.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

bool Archive::upsert_message(const json& message, std::int64_t now) {
  if (jtype(message) != "message") {
    last_error_ = "upsert_message: not a td_api message";
    return false;
  }
  const std::int64_t chat_id = jint(message, "chat_id");
  const std::int64_t message_id = jint(message, "id");
  if (chat_id == 0 || message_id == 0) {
    last_error_ = "upsert_message: missing chat_id or id";
    return false;
  }
  const json& content = jobj(message, "content");
  const std::string type = jtype(content);
  const std::string text = extract_text(content);
  const SenderInfo sender = parse_sender(jobj(message, "sender_id"));

  if (!exec("SAVEPOINT upsert")) return false;
  auto fail = [&]() {
    last_error_ = sqlite3_errmsg(db_);
    exec("ROLLBACK TO upsert; RELEASE upsert;");
    return false;
  };

  Stmt sel(db_, "SELECT text, content_type FROM messages WHERE chat_id = ? AND message_id = ?");
  sel.bind(1, chat_id).bind(2, message_id);
  int rc = sel.step();
  if (rc == SQLITE_ROW) {
    std::string old_text = sel.text(0);
    std::string old_type = sel.text(1);
    if ((old_text != text || old_type != type) &&
        !record_edit(chat_id, message_id, old_text, old_type, text, type, now)) {
      return fail();
    }
    Stmt up(db_,
            "UPDATE messages SET sender_kind = ?, sender_id = ?, date = ?, edit_date = max(edit_date, ?), "
            "content_type = ?, text = ?, is_outgoing = ?, self_destruct = max(self_destruct, ?), raw_json = ? "
            "WHERE chat_id = ? AND message_id = ?");
    up.bind(1, sender.kind).bind(2, sender.id).bind(3, jint(message, "date")).bind(4, jint(message, "edit_date"));
    up.bind(5, type).bind(6, text).bind(7, jbool(message, "is_outgoing") ? 1 : 0);
    up.bind(8, is_self_destruct(message) ? 1 : 0).bind(9, message.dump()).bind(10, chat_id).bind(11, message_id);
    if (up.step() != SQLITE_DONE) return fail();
  } else if (rc == SQLITE_DONE) {
    Stmt ins(db_,
             "INSERT INTO messages(chat_id, message_id, sender_kind, sender_id, date, edit_date, content_type, "
             "text, is_outgoing, self_destruct, raw_json) VALUES (?,?,?,?,?,?,?,?,?,?,?)");
    ins.bind(1, chat_id).bind(2, message_id).bind(3, sender.kind).bind(4, sender.id).bind(5, jint(message, "date"));
    ins.bind(6, jint(message, "edit_date")).bind(7, type).bind(8, text).bind(9, jbool(message, "is_outgoing") ? 1 : 0);
    ins.bind(10, is_self_destruct(message) ? 1 : 0).bind(11, message.dump());
    if (ins.step() != SQLITE_DONE) return fail();
  } else {
    return fail();
  }
  return exec("RELEASE upsert");
}

bool Archive::apply_content(std::int64_t chat_id, std::int64_t message_id, const json& new_content,
                            std::int64_t now) {
  Stmt sel(db_, "SELECT text, content_type, raw_json FROM messages WHERE chat_id = ? AND message_id = ?");
  sel.bind(1, chat_id).bind(2, message_id);
  int rc = sel.step();
  if (rc == SQLITE_DONE) return true;  // never seen: nothing to keep
  if (rc != SQLITE_ROW) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  json message = json::parse(sel.text(2), nullptr, false);
  if (message.is_discarded() || !message.is_object()) {
    last_error_ = "apply_content: stored raw_json is corrupt";
    return false;
  }
  message["content"] = new_content;
  return upsert_message(message, now);
}

bool Archive::apply_edited(std::int64_t chat_id, std::int64_t message_id, std::int64_t edit_date) {
  Stmt up(db_, "UPDATE messages SET edit_date = max(edit_date, ?) WHERE chat_id = ? AND message_id = ?");
  up.bind(1, edit_date).bind(2, chat_id).bind(3, message_id);
  if (up.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  Stmt e(db_,
         "UPDATE message_edits SET edit_date = ? WHERE id = (SELECT max(id) FROM message_edits "
         "WHERE chat_id = ? AND message_id = ? AND edit_date = 0)");
  e.bind(1, edit_date).bind(2, chat_id).bind(3, message_id);
  if (e.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

bool Archive::apply_send_succeeded(const json& message, std::int64_t old_message_id, std::int64_t now) {
  const std::int64_t chat_id = jint(message, "chat_id");
  const std::int64_t new_id = jint(message, "id");
  if (old_message_id != new_id) {
    Stmt exists(db_, "SELECT 1 FROM messages WHERE chat_id = ? AND message_id = ?");
    exists.bind(1, chat_id).bind(2, new_id);
    if (exists.step() == SQLITE_ROW) {
      Stmt del(db_, "DELETE FROM messages WHERE chat_id = ? AND message_id = ?");
      del.bind(1, chat_id).bind(2, old_message_id);
      del.step();
    } else {
      Stmt mv(db_, "UPDATE messages SET message_id = ? WHERE chat_id = ? AND message_id = ?");
      mv.bind(1, new_id).bind(2, chat_id).bind(3, old_message_id);
      if (mv.step() != SQLITE_DONE) {
        last_error_ = sqlite3_errmsg(db_);
        return false;
      }
      Stmt mve(db_, "UPDATE message_edits SET message_id = ? WHERE chat_id = ? AND message_id = ?");
      mve.bind(1, new_id).bind(2, chat_id).bind(3, old_message_id);
      mve.step();
    }
  }
  return upsert_message(message, now);
}

int Archive::mark_deleted(std::int64_t chat_id, const std::vector<std::int64_t>& message_ids, std::int64_t now) {
  int found = 0;
  for (std::int64_t id : message_ids) {
    Stmt up(db_, "UPDATE messages SET deleted_at = ? WHERE chat_id = ? AND message_id = ? AND deleted_at IS NULL");
    up.bind(1, now).bind(2, chat_id).bind(3, id);
    if (up.step() == SQLITE_DONE) found += sqlite3_changes(db_);
  }
  return found;
}

bool Archive::upsert_chat(std::int64_t chat_id, const std::string& title, const std::string& type) {
  Stmt s(db_,
         "INSERT INTO chats(chat_id, title, type) VALUES (?,?,?) "
         "ON CONFLICT(chat_id) DO UPDATE SET title = excluded.title, type = excluded.type");
  s.bind(1, chat_id).bind(2, title).bind(3, type);
  if (s.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

bool Archive::set_chat_title(std::int64_t chat_id, const std::string& title) {
  Stmt s(db_,
         "INSERT INTO chats(chat_id, title) VALUES (?,?) "
         "ON CONFLICT(chat_id) DO UPDATE SET title = excluded.title");
  s.bind(1, chat_id).bind(2, title);
  if (s.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

bool Archive::upsert_user(std::int64_t user_id, const std::string& first_name, const std::string& last_name,
                          const std::string& username) {
  Stmt s(db_,
         "INSERT INTO users(user_id, first_name, last_name, username) VALUES (?,?,?,?) "
         "ON CONFLICT(user_id) DO UPDATE SET first_name = excluded.first_name, last_name = excluded.last_name, "
         "username = excluded.username");
  s.bind(1, user_id).bind(2, first_name).bind(3, last_name).bind(4, username);
  if (s.step() != SQLITE_DONE) {
    last_error_ = sqlite3_errmsg(db_);
    return false;
  }
  return true;
}

std::optional<StoredMessage> Archive::get(std::int64_t chat_id, std::int64_t message_id) {
  Stmt s(db_,
         "SELECT sender_kind, sender_id, date, edit_date, content_type, text, is_outgoing, self_destruct, "
         "deleted_at, edit_count, raw_json FROM messages WHERE chat_id = ? AND message_id = ?");
  s.bind(1, chat_id).bind(2, message_id);
  if (s.step() != SQLITE_ROW) return std::nullopt;
  StoredMessage m;
  m.chat_id = chat_id;
  m.message_id = message_id;
  m.sender_kind = s.text(0);
  m.sender_id = s.i64(1);
  m.date = s.i64(2);
  m.edit_date = s.i64(3);
  m.content_type = s.text(4);
  m.text = s.text(5);
  m.is_outgoing = s.i64(6) != 0;
  m.self_destruct = s.i64(7) != 0;
  if (!s.is_null(8)) m.deleted_at = s.i64(8);
  m.edit_count = static_cast<int>(s.i64(9));
  m.raw_json = s.text(10);
  return m;
}

std::vector<EditRecord> Archive::edits(std::int64_t chat_id, std::int64_t message_id) {
  std::vector<EditRecord> out;
  Stmt s(db_,
         "SELECT recorded_at, edit_date, old_text, new_text, old_content_type, new_content_type "
         "FROM message_edits WHERE chat_id = ? AND message_id = ? ORDER BY id");
  s.bind(1, chat_id).bind(2, message_id);
  while (s.step() == SQLITE_ROW) {
    EditRecord e;
    e.recorded_at = s.i64(0);
    e.edit_date = s.i64(1);
    e.old_text = s.text(2);
    e.new_text = s.text(3);
    e.old_content_type = s.text(4);
    e.new_content_type = s.text(5);
    out.push_back(std::move(e));
  }
  return out;
}

std::vector<SearchHit> Archive::search(const std::string& user_query, int limit) {
  std::vector<SearchHit> out;
  const std::string q = to_fts_query(user_query);
  if (q.empty() || limit <= 0) return out;
  Stmt s(db_,
         "SELECT m.chat_id, m.message_id, m.date, m.deleted_at IS NOT NULL, coalesce(c.title, ''), "
         "snippet(messages_fts, 0, '[', ']', '...', 12) "
         "FROM messages_fts JOIN messages m ON m.id = messages_fts.rowid "
         "LEFT JOIN chats c ON c.chat_id = m.chat_id "
         "WHERE messages_fts MATCH ? ORDER BY m.date DESC, m.id DESC LIMIT ?");
  if (!s.ok()) {
    last_error_ = sqlite3_errmsg(db_);
    return out;
  }
  s.bind(1, q).bind(2, limit);
  int rc;
  while ((rc = s.step()) == SQLITE_ROW) {
    SearchHit h;
    h.chat_id = s.i64(0);
    h.message_id = s.i64(1);
    h.date = s.i64(2);
    h.deleted = s.i64(3) != 0;
    h.chat_title = s.text(4);
    h.snippet = s.text(5);
    out.push_back(std::move(h));
  }
  if (rc != SQLITE_DONE) last_error_ = sqlite3_errmsg(db_);
  return out;
}

std::optional<std::string> Archive::chat_title(std::int64_t chat_id) {
  Stmt s(db_, "SELECT title FROM chats WHERE chat_id = ?");
  s.bind(1, chat_id);
  if (s.step() != SQLITE_ROW) return std::nullopt;
  return s.text(0);
}

std::int64_t Archive::message_count() {
  Stmt s(db_, "SELECT count(*) FROM messages");
  return s.step() == SQLITE_ROW ? s.i64(0) : -1;
}

}  // namespace mc1h
