// mc1h-tg — local message archive: SQLite + FTS5.
//
// Keeps every message the client has seen, its edit history, and a
// "deleted" mark for messages TDLib reports as permanently deleted.
// Not thread-safe: use one Archive per thread (the receive thread).
// Storage is NOT encrypted yet (SQLCipher is a planned replacement).
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/json.h"

struct sqlite3;

namespace mc1h {

struct StoredMessage {
  std::int64_t chat_id = 0;
  std::int64_t message_id = 0;
  std::string sender_kind;  // "user" | "chat" | ""
  std::int64_t sender_id = 0;
  std::int64_t date = 0;
  std::int64_t edit_date = 0;
  std::string content_type;
  std::string text;
  bool is_outgoing = false;
  bool self_destruct = false;
  std::optional<std::int64_t> deleted_at;
  int edit_count = 0;
  std::string raw_json;
};

struct EditRecord {
  std::int64_t recorded_at = 0;
  std::int64_t edit_date = 0;  // 0 until updateMessageEdited arrives
  std::string old_text;
  std::string new_text;
  std::string old_content_type;
  std::string new_content_type;
};

struct SearchHit {
  std::int64_t chat_id = 0;
  std::int64_t message_id = 0;
  std::int64_t date = 0;
  bool deleted = false;
  std::string chat_title;
  std::string snippet;
};

// Text a human would search for: message text, media caption, poll question.
std::string extract_text(const json& content);

// Turns free user input into an FTS5 query of quoted terms (implicit AND),
// so FTS syntax characters in the input cannot produce a query error.
std::string to_fts_query(const std::string& user_input);

class Archive {
 public:
  ~Archive();
  Archive(const Archive&) = delete;
  Archive& operator=(const Archive&) = delete;

  // path ":memory:" gives a throwaway archive (tests).
  static std::unique_ptr<Archive> open(const std::string& path, std::string* error);

  // td_api `message`. A changed text/content type on a known message is
  // recorded as an edit. Returns false on a DB error or a non-message input.
  bool upsert_message(const json& message, std::int64_t now);
  // updateMessageContent.
  bool apply_content(std::int64_t chat_id, std::int64_t message_id, const json& new_content,
                     std::int64_t now);
  // updateMessageEdited.
  bool apply_edited(std::int64_t chat_id, std::int64_t message_id, std::int64_t edit_date);
  // updateMessageSendSucceeded: temporary id -> server id.
  bool apply_send_succeeded(const json& message, std::int64_t old_message_id, std::int64_t now);
  // Marks archived messages deleted; returns how many were found.
  int mark_deleted(std::int64_t chat_id, const std::vector<std::int64_t>& message_ids, std::int64_t now);

  bool upsert_chat(std::int64_t chat_id, const std::string& title, const std::string& type);
  bool set_chat_title(std::int64_t chat_id, const std::string& title);
  bool upsert_user(std::int64_t user_id, const std::string& first_name, const std::string& last_name,
                   const std::string& username);

  std::optional<StoredMessage> get(std::int64_t chat_id, std::int64_t message_id);
  std::vector<EditRecord> edits(std::int64_t chat_id, std::int64_t message_id);
  std::vector<SearchHit> search(const std::string& user_query, int limit);
  std::optional<std::string> chat_title(std::int64_t chat_id);
  std::int64_t message_count();

  const std::string& last_error() const { return last_error_; }

 private:
  explicit Archive(sqlite3* db);
  bool exec(const char* sql);
  bool init_schema();
  bool record_edit(std::int64_t chat_id, std::int64_t message_id, const std::string& old_text,
                   const std::string& old_type, const std::string& new_text, const std::string& new_type,
                   std::int64_t now);

  sqlite3* db_ = nullptr;
  std::string last_error_;
};

}  // namespace mc1h
