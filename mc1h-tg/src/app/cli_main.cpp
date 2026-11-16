// mc1h-tg — console client: authorization, update routing, archive, ghost mode.
//
// Secrets come only from the environment:
//   MC1H_API_ID, MC1H_API_HASH  (required, from my.telegram.org)
//   MC1H_DB_KEY                 (optional TDLib database encryption key)
#include <atomic>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "app/console.h"
#include "app/td_json_transport.h"
#include "core/archive.h"
#include "core/archive_recorder.h"
#include "core/auth_flow.h"
#include "core/ghost_client.h"
#include "core/ghost_policy.h"
#include "core/json.h"
#include "core/update_router.h"

namespace fs = std::filesystem;
using namespace mc1h;

namespace {

std::atomic<bool> g_interrupt{false};

extern "C" void on_sigint(int) { g_interrupt.store(true); }

std::int64_t unix_now() {
  return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

std::string env(const char* name) {
  const char* v = std::getenv(name);
  return v ? std::string(v) : std::string();
}

bool parse_i64(const std::string& s, std::int64_t& out) {
  if (s.empty()) return false;
  try {
    std::size_t pos = 0;
    long long v = std::stoll(s, &pos, 10);
    if (pos != s.size()) return false;
    out = v;
    return true;
  } catch (...) {
    return false;
  }
}

std::vector<std::string> split_ws(const std::string& line) {
  std::istringstream in(line);
  std::vector<std::string> out;
  std::string w;
  while (in >> w) out.push_back(w);
  return out;
}

// Remainder of `line` after skipping `n` whitespace-separated words.
std::string rest_after(const std::string& line, int n) {
  std::size_t i = 0;
  for (int k = 0; k < n; ++k) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  }
  while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
  return line.substr(i);
}

bool save_text_atomic(const fs::path& path, const std::string& text, std::string& error) {
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "cannot write " + tmp.string();
      return false;
    }
    out << text;
    if (!out) {
      error = "write failed: " + tmp.string();
      return false;
    }
  }
  std::error_code ec;
  fs::rename(tmp, path, ec);
  if (ec) {
    error = "rename failed: " + ec.message();
    return false;
  }
  return true;
}

const char* kHelp = R"(Commands:
  help                              this text
  me                                getMe
  chats [n]                         load and list the first n chats (default 20)
  history <chat_id> [n]             fetch n last messages into the archive (default 20)
  search <text>                     full-text search in the local archive
  show <chat_id> <message_id>       archived message with edit history and deletion mark
  send <chat_id> <text>             send a text message
  read <chat_id> <message_id...>    viewMessages (blocked by ghost "reads")
  typing <chat_id>                  sendChatAction typing (blocked by ghost "typing")
  online on|off                     setOption online (true is blocked by ghost "online")
  ghost                             show ghost settings
  ghost <feature> on|off            account setting; features: reads typing online stories opens ads
  ghost chat <chat_id> <feature> on|off|default
  stats                             counters
  logout confirm                    terminate this Telegram session
  quit                              close TDLib cleanly and exit
During login, answer any prompt with /quit to exit.)";

struct Options {
  fs::path data_dir = "mc1h-data";
  bool test_dc = false;
  int verbosity = 1;
};

bool parse_args(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--data-dir" && i + 1 < argc) {
      o.data_dir = argv[++i];
    } else if (a == "--test-dc") {
      o.test_dc = true;
    } else if (a == "--verbosity" && i + 1 < argc) {
      std::int64_t v = 0;
      if (!parse_i64(argv[++i], v) || v < 0 || v > 1024) return false;
      o.verbosity = static_cast<int>(v);
    } else {
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, opt)) {
    std::cerr << "usage: mc1h_cli [--data-dir DIR] [--test-dc] [--verbosity N]\n"
                 "env: MC1H_API_ID, MC1H_API_HASH (required), MC1H_DB_KEY (optional)\n";
    return 2;
  }

  TdlibParams params;
  std::int64_t api_id = 0;
  if (!parse_i64(env("MC1H_API_ID"), api_id) || api_id <= 0 || api_id > 0x7FFFFFFF) {
    std::cerr << "MC1H_API_ID is missing or not a positive int32\n";
    return 2;
  }
  params.api_id = static_cast<std::int32_t>(api_id);
  params.api_hash = env("MC1H_API_HASH");
  if (params.api_hash.empty()) {
    std::cerr << "MC1H_API_HASH is missing\n";
    return 2;
  }
  params.database_encryption_key = env("MC1H_DB_KEY");
  params.use_test_dc = opt.test_dc;

  std::error_code ec;
  fs::create_directories(opt.data_dir / "tdlib", ec);
  fs::create_directories(opt.data_dir / "files", ec);
  if (ec) {
    std::cerr << "cannot create " << opt.data_dir << ": " << ec.message() << "\n";
    return 1;
  }
  params.database_directory = (opt.data_dir / "tdlib").string();
  params.files_directory = (opt.data_dir / "files").string();
#ifdef _WIN32
  params.system_version = "Windows";
#else
  params.system_version = "Linux";
#endif
  params.application_version = "mc1h-tg 0.1";

  // TDLib log goes to a file so it does not interleave with the console.
  const std::string log_path = (opt.data_dir / "tdlib.log").string();
  TdJsonTransport::execute_static(
      json{{"@type", "setLogStream"},
           {"log_stream", {{"@type", "logStreamFile"}, {"path", log_path}, {"max_file_size", 10 << 20},
                           {"redirect_stderr", false}}}}
          .dump());
  TdJsonTransport::execute_static(
      json{{"@type", "setLogVerbosityLevel"}, {"new_verbosity_level", opt.verbosity}}.dump());

  // Ghost settings.
  const fs::path ghost_path = opt.data_dir / "ghost.json";
  GhostPolicy policy;
  if (fs::exists(ghost_path)) {
    std::ifstream in(ghost_path, std::ios::binary);
    json j = json::parse(in, nullptr, false);
    std::string err;
    policy = GhostPolicy::from_json(j.is_discarded() ? json() : j, &err);
    if (!err.empty()) std::cerr << "warning: " << err << " (" << ghost_path << ")\n";
  }

  std::string err;
  auto archive = Archive::open((opt.data_dir / "archive.sqlite").string(), &err);
  if (!archive) {
    std::cerr << "archive: " << err << "\n";
    return 1;
  }

  Console console;
  if (params.database_encryption_key.empty()) {
    console.print("warning: MC1H_DB_KEY is not set; the TDLib database is not encrypted");
  }
  console.print("warning: the local archive (archive.sqlite) is not encrypted yet");

  TdJsonTransport transport;
  GhostClient client(transport, policy);
  client.set_blocked_callback([&](const json&, const GhostDecision& d) {
    std::string where = d.chat_id ? " chat " + std::to_string(d.chat_id) : "";
    console.print(std::string("[ghost] blocked ") + d.method + where + " (" + to_string(*d.feature) + ")");
  });

  UpdateRouter router;
  ArchiveRecorder recorder(*archive, unix_now, [&](const std::string& e) { console.print("[archive] " + e); });
  recorder.attach(router);
  AuthFlow flow(params);

  bool ready = false;
  bool closed = false;
  bool quitting = false;
  int exit_code = 0;
  std::map<std::int64_t, std::string> user_names;

  auto chat_label = [&](std::int64_t chat_id) {
    auto t = archive->chat_title(chat_id);
    return t && !t->empty() ? *t : std::to_string(chat_id);
  };
  auto sender_label = [&](const json& sender) {
    if (jtype(sender) == "messageSenderUser") {
      auto it = user_names.find(jint(sender, "user_id"));
      if (it != user_names.end()) return it->second;
      return "user " + std::to_string(jint(sender, "user_id"));
    }
    return chat_label(jint(sender, "chat_id"));
  };
  auto format_message = [&](const json& m) {
    std::string text = extract_text(jobj(m, "content"));
    if (text.size() > 200) text = text.substr(0, 200) + "...";
    std::string type = jtype(jobj(m, "content"));
    std::string line = "[" + chat_label(jint(m, "chat_id")) + "] #" + std::to_string(jint(m, "id")) + " " +
                       sender_label(jobj(m, "sender_id")) + ": ";
    if (type != "messageText") line += "<" + type + "> ";
    if (jobj(m, "self_destruct_type").is_object()) line += "<self-destruct> ";
    return line + text;
  };

  auto save_ghost = [&]() {
    std::string e;
    if (!save_text_atomic(ghost_path, client.policy_snapshot().to_json().dump(2), e)) {
      console.print("ghost settings not saved: " + e);
    }
  };

  // Sends a request; the response goes to `on_ok`, errors are printed.
  auto request = [&](json req, UpdateRouter::Handler on_ok) {
    std::string method = jtype(req);
    client.send(router.track(std::move(req), [&console, method, on_ok](const json& r) {
      if (jtype(r) == "error") {
        console.print(method + ": error " + std::to_string(jint(r, "code")) + " " + jstr(r, "message"));
      } else if (on_ok) {
        on_ok(r);
      }
    }));
  };

  auto begin_close = [&]() {
    if (quitting) return;
    quitting = true;
    console.print("closing TDLib...");
    client.send(json{{"@type", "close"}});
  };

  std::function<void(const AuthStep&)> apply_step = [&](const AuthStep& s) {
    if (!s.info.empty()) console.print(s.info);
    if (s.request) {
      client.send(router.track(*s.request, [&](const json& r) {
        if (jtype(r) == "error") apply_step(flow.on_error(r));
      }));
    }
    if (s.prompt != AuthPrompt::None) console.request_line(s.prompt_text, s.secret);
    if (s.ready && !ready) {
      ready = true;
      if (client.policy_snapshot().account().hide_online) {
        // Explicitly offline; TDLib keeps "online" false unless the app sets it.
        client.send(json{{"@type", "setOption"},
                         {"name", "online"},
                         {"value", {{"@type", "optionValueBoolean"}, {"value", false}}}});
      }
      console.print("type 'help' for commands");
      console.request_line("> ", false);
    }
    if (s.fatal) {
      exit_code = 1;
      begin_close();
    }
    if (s.closed) closed = true;
  };

  router.on("updateAuthorizationState",
            [&](const json& u) { apply_step(flow.on_state(jobj(u, "authorization_state"))); });
  router.on("updateUser", [&](const json& u) {
    const json& user = jobj(u, "user");
    std::string name = jstr(user, "first_name");
    std::string last = jstr(user, "last_name");
    if (!last.empty()) name += " " + last;
    user_names[jint(user, "id")] = name;
  });
  router.on("updateNewMessage", [&](const json& u) {
    if (ready) console.print(format_message(jobj(u, "message")));
  });
  router.on("updateMessageContent", [&](const json& u) {
    if (!ready) return;
    console.print("[" + chat_label(jint(u, "chat_id")) + "] #" + std::to_string(jint(u, "message_id")) +
                  " edited: " + extract_text(jobj(u, "new_content")));
  });
  router.on("updateDeleteMessages", [&](const json& u) {
    if (!ready || !jbool(u, "is_permanent") || jbool(u, "from_cache")) return;
    console.print("[" + chat_label(jint(u, "chat_id")) + "] deleted: " + jobj(u, "message_ids").dump() +
                  " (kept in archive if seen)");
  });

  auto handle_command = [&](const std::string& line) {
    auto w = split_ws(line);
    if (w.empty()) return;
    const std::string& cmd = w[0];
    std::int64_t a = 0, b = 0;
    if (cmd == "help") {
      console.print(kHelp);
    } else if (cmd == "quit" || cmd == "exit") {
      begin_close();
    } else if (cmd == "logout") {
      if (w.size() == 2 && w[1] == "confirm") {
        request(json{{"@type", "logOut"}}, nullptr);
      } else {
        console.print("this terminates the Telegram session; type 'logout confirm'");
      }
    } else if (cmd == "me") {
      request(json{{"@type", "getMe"}}, [&](const json& u) {
        console.print("id " + std::to_string(jint(u, "id")) + " " + jstr(u, "first_name") + " " +
                      jstr(u, "last_name"));
      });
    } else if (cmd == "chats") {
      std::int64_t n = 20;
      if (w.size() > 1 && (!parse_i64(w[1], n) || n <= 0 || n > 1000)) {
        console.print("usage: chats [n]");
        return;
      }
      json list = {{"@type", "chatListMain"}};
      request(json{{"@type", "loadChats"}, {"chat_list", list}, {"limit", n}}, nullptr);
      request(json{{"@type", "getChats"}, {"chat_list", list}, {"limit", n}}, [&](const json& r) {
        const json& ids = jobj(r, "chat_ids");
        if (!ids.is_array()) return;
        for (const json& id : ids) {
          if (id.is_number_integer()) {
            console.print(std::to_string(id.get<std::int64_t>()) + "  " + chat_label(id.get<std::int64_t>()));
          }
        }
      });
    } else if (cmd == "history") {
      std::int64_t n = 20;
      if (w.size() < 2 || !parse_i64(w[1], a) || (w.size() > 2 && (!parse_i64(w[2], n) || n <= 0 || n > 100))) {
        console.print("usage: history <chat_id> [n<=100]");
        return;
      }
      request(json{{"@type", "getChatHistory"},
                   {"chat_id", a},
                   {"from_message_id", 0},
                   {"offset", 0},
                   {"limit", n},
                   {"only_local", false}},
              [&](const json& r) {
                recorder.record_response(r);
                const json& msgs = jobj(r, "messages");
                if (!msgs.is_array()) return;
                for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) {
                  if (it->is_object()) console.print(format_message(*it));
                }
              });
    } else if (cmd == "search") {
      std::string q = rest_after(line, 1);
      auto hits = archive->search(q, 30);
      if (hits.empty()) console.print(archive->last_error().empty() ? "no matches" : archive->last_error());
      for (const auto& h : hits) {
        console.print(std::to_string(h.chat_id) + " #" + std::to_string(h.message_id) + " [" + h.chat_title + "]" +
                      (h.deleted ? " (deleted)" : "") + " " + h.snippet);
      }
    } else if (cmd == "show") {
      if (w.size() != 3 || !parse_i64(w[1], a) || !parse_i64(w[2], b)) {
        console.print("usage: show <chat_id> <message_id>");
        return;
      }
      auto m = archive->get(a, b);
      if (!m) {
        console.print("not in archive");
        return;
      }
      console.print("type " + m->content_type + ", date " + std::to_string(m->date) + ", edits " +
                    std::to_string(m->edit_count) +
                    (m->deleted_at ? ", deleted at " + std::to_string(*m->deleted_at) : std::string()) +
                    (m->self_destruct ? ", self-destruct" : ""));
      console.print("text: " + m->text);
      for (const auto& e : archive->edits(a, b)) {
        console.print("  edit@" + std::to_string(e.edit_date ? e.edit_date : e.recorded_at) + ": '" + e.old_text +
                      "' -> '" + e.new_text + "'");
      }
    } else if (cmd == "send") {
      std::string text = rest_after(line, 2);
      if (w.size() < 3 || !parse_i64(w[1], a) || text.empty()) {
        console.print("usage: send <chat_id> <text>");
        return;
      }
      request(json{{"@type", "sendMessage"},
                   {"chat_id", a},
                   {"topic_id", nullptr},
                   {"reply_to", nullptr},
                   {"options", nullptr},
                   {"reply_markup", nullptr},
                   {"input_message_content",
                    {{"@type", "inputMessageText"},
                     {"text", {{"@type", "formattedText"}, {"text", text}, {"entities", json::array()}}},
                     {"link_preview_options", nullptr},
                     {"clear_draft", false}}}},
              [&](const json& m) { console.print("queued as #" + std::to_string(jint(m, "id"))); });
    } else if (cmd == "read") {
      if (w.size() < 3 || !parse_i64(w[1], a)) {
        console.print("usage: read <chat_id> <message_id...>");
        return;
      }
      json ids = json::array();
      for (std::size_t i = 2; i < w.size(); ++i) {
        if (!parse_i64(w[i], b)) {
          console.print("bad message id: " + w[i]);
          return;
        }
        ids.push_back(b);
      }
      request(json{{"@type", "viewMessages"},
                   {"chat_id", a},
                   {"message_ids", ids},
                   {"source", {{"@type", "messageSourceChatHistory"}}},
                   {"force_read", true}},
              [&](const json&) { console.print("viewMessages: ok"); });
    } else if (cmd == "typing") {
      if (w.size() != 2 || !parse_i64(w[1], a)) {
        console.print("usage: typing <chat_id>");
        return;
      }
      request(json{{"@type", "sendChatAction"},
                   {"chat_id", a},
                   {"topic_id", nullptr},
                   {"business_connection_id", ""},
                   {"action", {{"@type", "chatActionTyping"}}}},
              [&](const json&) { console.print("sendChatAction: ok"); });
    } else if (cmd == "online") {
      if (w.size() != 2 || (w[1] != "on" && w[1] != "off")) {
        console.print("usage: online on|off");
        return;
      }
      request(json{{"@type", "setOption"},
                   {"name", "online"},
                   {"value", {{"@type", "optionValueBoolean"}, {"value", w[1] == "on"}}}},
              [&](const json&) { console.print("setOption online: ok"); });
    } else if (cmd == "ghost") {
      if (w.size() == 1) {
        console.print(client.policy_snapshot().to_json().dump(2));
      } else if (w.size() == 3 && ghost_feature_from_string(w[1]) && (w[2] == "on" || w[2] == "off")) {
        const GhostFeature f = *ghost_feature_from_string(w[1]);
        const bool on = w[2] == "on";
        client.update_policy([&](GhostPolicy& p) { p.account().set(f, on); });
        if (f == GhostFeature::Online && on) {
          client.send(json{{"@type", "setOption"},
                           {"name", "online"},
                           {"value", {{"@type", "optionValueBoolean"}, {"value", false}}}});
        }
        save_ghost();
        console.print(std::string("ghost ") + to_string(f) + " = " + w[2]);
      } else if (w.size() == 5 && w[1] == "chat" && parse_i64(w[2], a) && ghost_feature_from_string(w[3]) &&
                 (w[4] == "on" || w[4] == "off" || w[4] == "default")) {
        const GhostFeature f = *ghost_feature_from_string(w[3]);
        GhostChatOverride probe;
        if (!probe.set(f, true)) {
          console.print(std::string("'") + to_string(f) + "' is account-wide only");
          return;
        }
        std::optional<bool> v;
        if (w[4] != "default") v = (w[4] == "on");
        client.update_policy([&](GhostPolicy& p) { p.set_chat_override(a, f, v); });
        save_ghost();
        console.print("ghost chat " + w[2] + " " + to_string(f) + " = " + w[4]);
      } else {
        console.print("usage: ghost | ghost <feature> on|off | ghost chat <chat_id> <feature> on|off|default");
      }
    } else if (cmd == "stats") {
      const auto& s = recorder.stats();
      console.print("forwarded " + std::to_string(client.forwarded_count()) + ", ghost-blocked " +
                    std::to_string(client.blocked_count()) + ", routed " + std::to_string(router.routed()) +
                    ", archived messages " + std::to_string(archive->message_count()) + ", deletions marked " +
                    std::to_string(s.deletions_marked) + ", deletions of unseen " +
                    std::to_string(s.deletions_unknown) + ", archive errors " + std::to_string(s.errors));
    } else {
      console.print("unknown command; type 'help'");
    }
  };

  std::signal(SIGINT, on_sigint);

  // TDLib creates the client instance on the first request sent to it.
  request(json{{"@type", "getOption"}, {"name", "version"}},
          [&](const json& v) { console.print("TDLib " + jstr(v, "value")); });

  while (!closed) {
    if (g_interrupt.exchange(false)) begin_close();
    if (auto obj = client.receive(0.2)) router.route(*obj);
    while (auto line = console.poll_line()) {
      if (flow.pending() != AuthPrompt::None) {
        if (*line == "/quit") {
          begin_close();
        } else {
          apply_step(flow.provide(*line));
        }
      } else if (ready) {
        handle_command(*line);
        if (!quitting) console.request_line("> ", false);
      }
    }
    if (console.eof() && !quitting) begin_close();
  }

  console.print("closed; archived messages: " + std::to_string(archive->message_count()));
  return exit_code;
}
