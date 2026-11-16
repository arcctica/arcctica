// mc1h-tg unit tests. No framework: CHECK counts and reports failures.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "core/archive.h"
#include "core/archive_recorder.h"
#include "core/auth_flow.h"
#include "core/ghost_client.h"
#include "core/ghost_policy.h"
#include "core/metadata_strip.h"
#include "core/update_router.h"

using namespace mc1h;
using Bytes = std::vector<std::uint8_t>;

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond)                                                              \
  do {                                                                           \
    ++g_checks;                                                                  \
    if (!(cond)) {                                                               \
      ++g_failures;                                                              \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);       \
    }                                                                            \
  } while (0)

// ------------------------------------------------------------------ helpers

class FakeTransport : public ITransport {
 public:
  void send(const std::string& s) override {
    std::lock_guard<std::mutex> l(mu);
    sent.push_back(s);
  }
  std::optional<std::string> receive(double) override {
    std::lock_guard<std::mutex> l(mu);
    if (incoming.empty()) return std::nullopt;
    std::string s = incoming.front();
    incoming.pop_front();
    return s;
  }
  std::string execute(const std::string& s) override {
    std::lock_guard<std::mutex> l(mu);
    executed.push_back(s);
    return execute_reply;
  }
  std::mutex mu;
  std::vector<std::string> sent;
  std::vector<std::string> executed;
  std::deque<std::string> incoming;
  std::string execute_reply = R"({"@type":"ok"})";
};

static json msg(std::int64_t chat, std::int64_t id, const std::string& text, std::int64_t date = 1000) {
  return json{{"@type", "message"},
              {"id", id},
              {"chat_id", chat},
              {"sender_id", {{"@type", "messageSenderUser"}, {"user_id", 42}}},
              {"date", date},
              {"edit_date", 0},
              {"is_outgoing", false},
              {"self_destruct_type", nullptr},
              {"content",
               {{"@type", "messageText"},
                {"text", {{"@type", "formattedText"}, {"text", text}, {"entities", json::array()}}}}}};
}

static json text_content(const std::string& text) {
  return json{{"@type", "messageText"},
              {"text", {{"@type", "formattedText"}, {"text", text}, {"entities", json::array()}}}};
}

// ------------------------------------------------------------------ ghost policy

static void test_ghost_policy() {
  GhostPolicy p;
  json view = {{"@type", "viewMessages"}, {"chat_id", 10}, {"message_ids", {1, 2}}, {"force_read", true}};
  CHECK(!p.evaluate(view).block);
  CHECK(p.evaluate(view).feature == GhostFeature::ReadReceipts);
  CHECK(p.evaluate(view).chat_id == 10);

  p.account().hide_read_receipts = true;
  CHECK(p.evaluate(view).block);
  CHECK(p.evaluate(json{{"@type", "readAllChatMentions"}, {"chat_id", 10}}).block);
  CHECK(p.evaluate(json{{"@type", "readAllForumTopicReactions"}, {"chat_id", 10}, {"forum_topic_id", 1}}).block);
  CHECK(p.evaluate(json{{"@type", "readChatList"}, {"chat_list", {{"@type", "chatListMain"}}}}).block);
  CHECK(!p.evaluate(json{{"@type", "sendMessage"}, {"chat_id", 10}}).block);
  CHECK(!p.evaluate(json{{"@type", "sendMessage"}, {"chat_id", 10}}).feature.has_value());
  CHECK(!p.evaluate(json{{"@type", "getChatHistory"}, {"chat_id", 10}}).block);

  // Per-chat override in both directions.
  p.set_chat_override(10, GhostFeature::ReadReceipts, false);
  CHECK(!p.evaluate(view).block);
  json view20 = view;
  view20["chat_id"] = 20;
  CHECK(p.evaluate(view20).block);
  p.account().hide_read_receipts = false;
  p.set_chat_override(20, GhostFeature::ReadReceipts, true);
  CHECK(p.evaluate(view20).block);
  CHECK(!p.evaluate(view).block);
  p.set_chat_override(20, GhostFeature::ReadReceipts, std::nullopt);
  CHECK(!p.evaluate(view20).block);
  CHECK(p.chat_override(20) == nullptr);

  // int53 may arrive as a string in hand-written requests.
  json view_str = {{"@type", "viewMessages"}, {"chat_id", "10"}};
  CHECK(p.evaluate(view_str).chat_id == 10);

  // Typing.
  json typing = {{"@type", "sendChatAction"}, {"chat_id", 5}, {"action", {{"@type", "chatActionTyping"}}}};
  CHECK(!p.evaluate(typing).block);
  p.account().hide_typing = true;
  CHECK(p.evaluate(typing).block);

  // Online: only setOption("online", true) is blocked.
  auto set_online = [](const char* name, json value) {
    return json{{"@type", "setOption"}, {"name", name}, {"value", value}};
  };
  json on = {{"@type", "optionValueBoolean"}, {"value", true}};
  json off = {{"@type", "optionValueBoolean"}, {"value", false}};
  CHECK(!p.evaluate(set_online("online", on)).block);
  p.account().hide_online = true;
  CHECK(p.evaluate(set_online("online", on)).block);
  CHECK(!p.evaluate(set_online("online", off)).block);
  CHECK(!p.evaluate(set_online("online", json{{"@type", "optionValueEmpty"}})).block);
  CHECK(!p.evaluate(set_online("language_pack_id", on)).block);

  // Stories are keyed by story_poster_chat_id.
  json story = {{"@type", "openStory"}, {"story_poster_chat_id", 77}, {"story_id", 3}};
  CHECK(!p.evaluate(story).block);
  p.set_chat_override(77, GhostFeature::StoryViews, true);
  CHECK(p.evaluate(story).block);
  CHECK(p.evaluate(story).chat_id == 77);
  CHECK(!p.evaluate(json{{"@type", "closeStory"}, {"story_poster_chat_id", 77}, {"story_id", 3}}).block);

  // Content opens and ads.
  p.account().hide_content_opens = true;
  CHECK(p.evaluate(json{{"@type", "openMessageContent"}, {"chat_id", 1}, {"message_id", 2}}).block);
  p.account().hide_ad_views = true;
  CHECK(p.evaluate(json{{"@type", "viewSponsoredChat"}, {"sponsored_chat_unique_id", 1}}).block);
  CHECK(!p.evaluate(json{{"@type", "clickChatSponsoredMessage"}, {"chat_id", 1}}).block);

  // Garbage input never blocks and never throws.
  CHECK(!p.evaluate(json()).block);
  CHECK(!p.evaluate(json::array()).block);
  CHECK(!p.evaluate(json{{"@type", 5}}).block);

  // Online/ads have no per-chat meaning.
  GhostChatOverride o;
  CHECK(!o.set(GhostFeature::Online, true));
  CHECK(!o.set(GhostFeature::AdViews, true));

  // Serialization round trip.
  json j = p.to_json();
  std::string err;
  GhostPolicy q = GhostPolicy::from_json(j, &err);
  CHECK(err.empty());
  CHECK(q.to_json() == j);
  CHECK(q.hides(GhostFeature::StoryViews, 77));
  CHECK(!q.hides(GhostFeature::StoryViews, 78));

  err.clear();
  GhostPolicy bad = GhostPolicy::from_json(
      json{{"account", {{"reads", "yes"}, {"typing", true}}}, {"chats", {{"abc", {{"reads", true}}}, {"5", {{"online", true}}}}}},
      &err);
  CHECK(!err.empty());
  CHECK(bad.account().hide_typing);
  CHECK(!bad.account().hide_read_receipts);
  CHECK(bad.chat_overrides().empty());

  CHECK(ghost_feature_from_string("stories") == GhostFeature::StoryViews);
  CHECK(!ghost_feature_from_string("nope").has_value());
  CHECK(std::string(to_string(GhostFeature::ContentOpens)) == "opens");
}

// ------------------------------------------------------------------ ghost client

static void test_ghost_client() {
  FakeTransport t;
  GhostPolicy p;
  p.account().hide_read_receipts = true;
  GhostClient c(t, p);
  std::vector<std::string> blocked_methods;
  c.set_blocked_callback([&](const json&, const GhostDecision& d) { blocked_methods.push_back(d.method); });

  t.incoming.push_back(R"({"@type":"updateOption","name":"version"})");
  c.send(json{{"@type", "viewMessages"}, {"chat_id", 1}, {"@extra", {{"id", 7}, {"tag", "x"}}}});
  CHECK(t.sent.empty());
  CHECK(c.blocked_count() == 1);
  CHECK(blocked_methods.size() == 1 && blocked_methods[0] == "viewMessages");

  // Synthetic ok first, carrying @extra unchanged.
  auto r1 = c.receive(0);
  CHECK(r1.has_value());
  CHECK(jtype(*r1) == "ok");
  CHECK((*r1)["@extra"] == (json{{"id", 7}, {"tag", "x"}}));
  auto r2 = c.receive(0);
  CHECK(r2.has_value() && jtype(*r2) == "updateOption");
  CHECK(!c.receive(0).has_value());

  // No @extra: still an ok, without @extra.
  c.send(json{{"@type", "readChatList"}});
  auto r3 = c.receive(0);
  CHECK(r3 && jtype(*r3) == "ok" && !r3->contains("@extra"));

  // Allowed requests are forwarded verbatim.
  json allowed = {{"@type", "getMe"}, {"@extra", 9}};
  c.send(allowed);
  CHECK(t.sent.size() == 1 && json::parse(t.sent[0]) == allowed);
  CHECK(c.forwarded_count() == 1);

  // execute(): blocked -> local ok, allowed -> transport.
  json ex = c.execute(json{{"@type", "viewMessages"}, {"chat_id", 1}, {"@extra", "e"}});
  CHECK(jtype(ex) == "ok" && ex["@extra"] == "e");
  CHECK(t.executed.empty());
  CHECK(jtype(c.execute(json{{"@type", "getTextEntities"}, {"text", "x"}})) == "ok");
  CHECK(t.executed.size() == 1);

  // Invalid JSON from the transport becomes an error object.
  t.incoming.push_back("{not json");
  auto bad = c.receive(0);
  CHECK(bad && jtype(*bad) == "error");

  // Live policy change.
  c.update_policy([](GhostPolicy& pol) { pol.account().hide_read_receipts = false; });
  c.send(json{{"@type", "viewMessages"}, {"chat_id", 1}});
  CHECK(t.sent.size() == 2);
  CHECK(!c.policy_snapshot().account().hide_read_receipts);

  // Concurrent senders: every request is either forwarded or answered.
  FakeTransport t2;
  GhostPolicy p2;
  p2.account().hide_typing = true;
  GhostClient c2(t2, p2);
  auto worker = [&](bool typing) {
    for (int i = 0; i < 2000; ++i) {
      c2.send(typing ? json{{"@type", "sendChatAction"}, {"chat_id", i}} : json{{"@type", "getMe"}});
    }
  };
  std::thread a(worker, true), b(worker, false);
  int received_ok = 0;
  std::thread consumer([&] {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (received_ok < 2000 && std::chrono::steady_clock::now() < deadline) {
      if (auto r = c2.receive(0)) {
        if (jtype(*r) == "ok") ++received_ok;
      } else {
        std::this_thread::yield();
      }
    }
  });
  a.join();
  b.join();
  consumer.join();
  CHECK(received_ok == 2000);
  CHECK(t2.sent.size() == 2000);
  CHECK(c2.blocked_count() == 2000 && c2.forwarded_count() == 2000);
}

// ------------------------------------------------------------------ update router

static void test_update_router() {
  UpdateRouter r;
  int typed = 0, any = 0, responses = 0, errors = 0;
  r.on("updateNewMessage", [&](const json&) { ++typed; });
  r.on("ok", [&](const json&) { ++typed; });
  r.on_any([&](const json&) { ++any; });

  json q1 = r.track(json{{"@type", "getMe"}}, [&](const json& resp) {
    if (jtype(resp) == "error") ++errors; else ++responses;
  });
  json q2 = r.track(json{{"@type", "getMe"}}, [&](const json& resp) {
    if (jtype(resp) == "error") ++errors; else ++responses;
  });
  CHECK(q1["@extra"] != q2["@extra"]);
  CHECK(r.pending() == 2);

  r.route(json{{"@type", "ok"}, {"@extra", q1["@extra"]}});
  CHECK(responses == 1 && typed == 0);  // tracked response does not reach typed handlers
  r.route(json{{"@type", "error"}, {"code", 400}, {"@extra", q2["@extra"]}});
  CHECK(errors == 1 && r.pending() == 0);
  r.route(json{{"@type", "ok"}, {"@extra", q1["@extra"]}});  // already consumed: routed as a plain object
  CHECK(responses == 1 && typed == 1);
  r.route(json{{"@type", "updateNewMessage"}});
  CHECK(typed == 2);
  CHECK(any == 4);
  CHECK(r.routed() == 4);

  // Reentrancy: a handler may track a new request while being routed.
  bool nested = false;
  r.on("updateAuthorizationState", [&](const json&) {
    json q = r.track(json{{"@type", "getOption"}}, [&](const json&) { nested = true; });
    r.route(json{{"@type", "optionValueString"}, {"@extra", q["@extra"]}});
  });
  r.route(json{{"@type", "updateAuthorizationState"}});
  CHECK(nested);
  r.route(json());  // non-object is harmless
}

// ------------------------------------------------------------------ archive

static void test_archive() {
  std::string err;
  auto a = Archive::open(":memory:", &err);
  CHECK(a != nullptr);
  if (!a) return;
  std::int64_t now = 5000;
  ArchiveRecorder rec(*a, [&] { return now; });
  UpdateRouter router;
  rec.attach(router);

  router.route(json{{"@type", "updateNewChat"},
                    {"chat", {{"@type", "chat"}, {"id", -100}, {"title", "Группа"}, {"type", {{"@type", "chatTypeSupergroup"}}}}}});
  CHECK(a->chat_title(-100) == std::string("Группа"));
  router.route(json{{"@type", "updateChatTitle"}, {"chat_id", -100}, {"title", "Группа 2"}});
  CHECK(a->chat_title(-100) == std::string("Группа 2"));

  router.route(json{{"@type", "updateNewMessage"}, {"message", msg(-100, 1, "Привет, мир! meeting at noon")}});
  router.route(json{{"@type", "updateNewMessage"}, {"message", msg(-100, 2, "second message", 1001)}});
  CHECK(rec.stats().messages == 2);
  CHECK(a->message_count() == 2);
  auto m1 = a->get(-100, 1);
  CHECK(m1 && m1->text == "Привет, мир! meeting at noon");
  CHECK(m1 && m1->sender_kind == "user" && m1->sender_id == 42);
  CHECK(m1 && !m1->deleted_at && m1->edit_count == 0 && !m1->self_destruct);

  // Case-insensitive Cyrillic search via unicode61.
  auto hits = a->search("привет", 10);
  CHECK(hits.size() == 1 && hits[0].message_id == 1 && hits[0].chat_title == "Группа 2");
  CHECK(hits.size() == 1 && hits[0].snippet.find("[Привет]") != std::string::npos);
  CHECK(a->search("message", 10).size() == 1);
  CHECK(a->search("meeting noon", 10).size() == 1);  // implicit AND
  CHECK(a->search("meeting absent", 10).empty());

  // FTS syntax in user input never errors.
  for (const char* q : {"\"", "AND", "NEAR(", "a*", "-x", "OR OR", "(", "col:val", "\"\"\"", "   "}) {
    a->search(q, 5);
    CHECK(a->last_error().find("syntax") == std::string::npos);
  }
  CHECK(to_fts_query("a \"b") == "\"a\" \"\"\"b\"");
  CHECK(to_fts_query("  ").empty());

  // Edit: content changes -> edit row; edit_date arrives later.
  now = 6000;
  router.route(json{{"@type", "updateMessageContent"}, {"chat_id", -100}, {"message_id", 2}, {"new_content", text_content("second message (fixed)")}});
  router.route(json{{"@type", "updateMessageEdited"}, {"chat_id", -100}, {"message_id", 2}, {"edit_date", 1500}, {"reply_markup", nullptr}});
  auto m2 = a->get(-100, 2);
  CHECK(m2 && m2->text == "second message (fixed)" && m2->edit_count == 1 && m2->edit_date == 1500);
  auto ed = a->edits(-100, 2);
  CHECK(ed.size() == 1 && ed[0].old_text == "second message" && ed[0].new_text == "second message (fixed)");
  CHECK(ed.size() == 1 && ed[0].edit_date == 1500 && ed[0].recorded_at == 6000);
  CHECK(a->search("fixed", 10).size() == 1);
  CHECK(json::parse(m2->raw_json)["content"]["text"]["text"] == "second message (fixed)");

  // Same text again (e.g. a non-text content refresh): no new edit.
  router.route(json{{"@type", "updateMessageContent"}, {"chat_id", -100}, {"message_id", 2}, {"new_content", text_content("second message (fixed)")}});
  CHECK(a->edits(-100, 2).size() == 1);
  // Content update for an unknown message is a no-op, not an error.
  router.route(json{{"@type", "updateMessageContent"}, {"chat_id", -100}, {"message_id", 999}, {"new_content", text_content("x")}});
  CHECK(rec.stats().errors == 0);

  // Deletions: only is_permanent && !from_cache count.
  router.route(json{{"@type", "updateDeleteMessages"}, {"chat_id", -100}, {"message_ids", {1}}, {"is_permanent", false}, {"from_cache", false}});
  router.route(json{{"@type", "updateDeleteMessages"}, {"chat_id", -100}, {"message_ids", {1}}, {"is_permanent", true}, {"from_cache", true}});
  CHECK(!a->get(-100, 1)->deleted_at);
  CHECK(rec.stats().deletions_ignored == 2);
  now = 7000;
  router.route(json{{"@type", "updateDeleteMessages"}, {"chat_id", -100}, {"message_ids", {1, 404}}, {"is_permanent", true}, {"from_cache", false}});
  auto d1 = a->get(-100, 1);
  CHECK(d1 && d1->deleted_at == 7000 && d1->text == "Привет, мир! meeting at noon");
  CHECK(rec.stats().deletions_marked == 1 && rec.stats().deletions_unknown == 1);
  auto dh = a->search("привет", 10);
  CHECK(dh.size() == 1 && dh[0].deleted);
  // A repeated deletion does not move the timestamp.
  now = 8000;
  router.route(json{{"@type", "updateDeleteMessages"}, {"chat_id", -100}, {"message_ids", {1}}, {"is_permanent", true}, {"from_cache", false}});
  CHECK(a->get(-100, 1)->deleted_at == 7000);

  // Temporary id -> server id.
  json pending = msg(-100, 1048577, "outgoing text");
  pending["is_outgoing"] = true;
  router.route(json{{"@type", "updateNewMessage"}, {"message", pending}});
  json sent = msg(-100, 3, "outgoing text");
  sent["is_outgoing"] = true;
  router.route(json{{"@type", "updateMessageSendSucceeded"}, {"message", sent}, {"old_message_id", 1048577}});
  CHECK(!a->get(-100, 1048577).has_value());
  CHECK(a->get(-100, 3) && a->get(-100, 3)->is_outgoing);

  // Content kinds and self-destruct flag.
  json photo = msg(5, 10, "");
  photo["content"] = {{"@type", "messagePhoto"}, {"caption", {{"@type", "formattedText"}, {"text", "sunset caption"}}}, {"is_secret", false}};
  photo["self_destruct_type"] = {{"@type", "messageSelfDestructTypeTimer"}, {"self_destruct_time", 10}};
  CHECK(a->upsert_message(photo, now));
  CHECK(a->get(5, 10)->self_destruct && a->get(5, 10)->content_type == "messagePhoto");
  CHECK(a->search("sunset", 5).size() == 1);
  CHECK(extract_text(json{{"@type", "messagePoll"}, {"poll", {{"question", {{"text", "Lunch?"}}}}}}) == "Lunch?");
  CHECK(extract_text(json{{"@type", "messageDocument"}, {"document", {{"file_name", "report.pdf"}}}, {"caption", {{"text", "see"}}}}) == "see\nreport.pdf");
  CHECK(extract_text(json()).empty());

  // Responses: messages arrays, with null holes.
  rec.record_response(json{{"@type", "messages"}, {"total_count", 2}, {"messages", {msg(6, 1, "history one"), nullptr, msg(6, 2, "history two")}}});
  CHECK(a->get(6, 1) && a->get(6, 2));
  rec.record_response(msg(6, 3, "single"));
  CHECK(a->get(6, 3).has_value());

  // Malformed input is rejected, not stored.
  CHECK(!a->upsert_message(json{{"@type", "message"}}, now));
  CHECK(!a->upsert_message(json{{"@type", "chat"}, {"id", 1}}, now));
  CHECK(rec.stats().errors == 0);

  // Persistence on disk.
  namespace fs = std::filesystem;
  fs::path dir = fs::temp_directory_path() / ("mc1h_test_" + std::to_string(std::random_device{}()));
  fs::create_directories(dir);
  std::string path = (dir / "a.sqlite").string();
  {
    auto f = Archive::open(path, &err);
    CHECK(f && f->upsert_message(msg(1, 1, "persisted words"), 1));
  }
  {
    auto f = Archive::open(path, &err);
    CHECK(f && f->get(1, 1) && f->search("persisted", 5).size() == 1);
  }
  fs::remove_all(dir);
  CHECK(Archive::open((dir / "missing" / "x.sqlite").string(), &err) == nullptr);
  CHECK(!err.empty());
}

// ------------------------------------------------------------------ auth flow

static void test_auth_flow() {
  CHECK(base64_encode("").empty());
  CHECK(base64_encode("f") == "Zg==");
  CHECK(base64_encode("fo") == "Zm8=");
  CHECK(base64_encode("foo") == "Zm9v");
  CHECK(base64_encode("foob") == "Zm9vYg==");
  CHECK(base64_encode("fooba") == "Zm9vYmE=");
  CHECK(base64_encode("foobar") == "Zm9vYmFy");
  CHECK(base64_encode(std::string("\xff\x00\x10", 3)) == "/wAQ");

  TdlibParams params;
  params.api_id = 12345;
  params.api_hash = "hash";
  params.database_directory = "db";
  params.files_directory = "files";
  params.database_encryption_key = "key";
  AuthFlow flow(params);

  AuthStep s = flow.on_state(json{{"@type", "authorizationStateWaitTdlibParameters"}});
  CHECK(s.request && jtype(*s.request) == "setTdlibParameters");
  CHECK(s.request && (*s.request)["database_encryption_key"] == "a2V5");
  CHECK(s.request && (*s.request)["api_id"] == 12345 && (*s.request)["use_test_dc"] == false);
  for (const char* f : {"use_test_dc", "database_directory", "files_directory", "database_encryption_key",
                        "use_file_database", "use_chat_info_database", "use_message_database", "use_secret_chats",
                        "api_id", "api_hash", "system_language_code", "device_model", "system_version",
                        "application_version"}) {
    CHECK(s.request && s.request->contains(f));
  }
  CHECK(s.request && s.request->size() == 15);  // 14 fields + @type, as in td_api.tl
  CHECK(flow.on_error(json{{"@type", "error"}, {"code", 400}, {"message", "API_ID_INVALID"}}).fatal);

  AuthFlow no_keys{TdlibParams{}};
  CHECK(no_keys.on_state(json{{"@type", "authorizationStateWaitTdlibParameters"}}).fatal);

  s = flow.on_state(json{{"@type", "authorizationStateWaitPhoneNumber"}});
  CHECK(s.prompt == AuthPrompt::Phone && !s.secret && !s.request);
  s = flow.provide("   ");
  CHECK(s.prompt == AuthPrompt::Phone && s.info == "Empty input");
  s = flow.provide(" +15550000000 ");
  CHECK(s.request && (*s.request)["phone_number"] == "+15550000000" && (*s.request)["settings"].is_null());
  CHECK(flow.pending() == AuthPrompt::None);
  CHECK(!flow.provide("stray").request);  // no prompt pending

  s = flow.on_state(json{{"@type", "authorizationStateWaitCode"}, {"code_info", {{"type", {{"@type", "authenticationCodeTypeTelegramMessage"}}}}}});
  CHECK(s.prompt == AuthPrompt::Code && s.prompt_text.find("authenticationCodeTypeTelegramMessage") != std::string::npos);
  s = flow.provide("12345");
  CHECK(s.request && jtype(*s.request) == "checkAuthenticationCode" && (*s.request)["code"] == "12345");
  s = flow.on_error(json{{"@type", "error"}, {"code", 400}, {"message", "PHONE_CODE_INVALID"}});
  CHECK(s.prompt == AuthPrompt::Code && s.info.find("PHONE_CODE_INVALID") != std::string::npos && !s.fatal);

  s = flow.on_state(json{{"@type", "authorizationStateWaitPassword"}, {"password_hint", "pet"}});
  CHECK(s.prompt == AuthPrompt::Password && s.secret && s.prompt_text.find("pet") != std::string::npos);
  s = flow.provide(" pass word ");
  CHECK(s.request && (*s.request)["password"] == " pass word ");  // passwords are not trimmed

  s = flow.on_state(json{{"@type", "authorizationStateWaitRegistration"}});
  CHECK(s.prompt == AuthPrompt::FirstName);
  s = flow.provide("Ann");
  CHECK(s.prompt == AuthPrompt::LastName && !s.request);
  s = flow.provide("");
  CHECK(s.request && jtype(*s.request) == "registerUser" && (*s.request)["first_name"] == "Ann" && (*s.request)["last_name"] == "");

  s = flow.on_state(json{{"@type", "authorizationStateWaitEmailCode"}});
  s = flow.provide("777");
  CHECK(s.request && (*s.request)["code"]["@type"] == "emailAddressAuthenticationCode" && (*s.request)["code"]["code"] == "777");

  CHECK(flow.on_state(json{{"@type", "authorizationStateWaitPremiumPurchase"}}).fatal);
  CHECK(flow.on_state(json{{"@type", "authorizationStateWaitOtherDeviceConfirmation"}, {"link", "tg://login?token=x"}}).info.find("tg://login") != std::string::npos);
  CHECK(flow.on_state(json{{"@type", "authorizationStateReady"}}).ready);
  CHECK(flow.on_state(json{{"@type", "authorizationStateClosed"}}).closed);
}

// ------------------------------------------------------------------ metadata strip

static void put16(Bytes& b, unsigned v) {
  b.push_back(static_cast<std::uint8_t>(v >> 8));
  b.push_back(static_cast<std::uint8_t>(v));
}
static void put32(Bytes& b, std::uint32_t v) {
  put16(b, v >> 16);
  put16(b, v & 0xFFFF);
}
static void segment(Bytes& b, std::uint8_t marker, const Bytes& payload) {
  b.push_back(0xFF);
  b.push_back(marker);
  put16(b, static_cast<unsigned>(payload.size() + 2));
  b.insert(b.end(), payload.begin(), payload.end());
}
static Bytes str_bytes(const std::string& s) { return Bytes(s.begin(), s.end()); }

// Little-endian EXIF with Orientation and a GPS IFD pointer.
static Bytes exif_le(int orientation) {
  Bytes t = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00, 0x02, 0x00};
  Bytes e1 = {0x12, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00, static_cast<std::uint8_t>(orientation), 0x00, 0x00, 0x00};
  Bytes e2 = {0x25, 0x88, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00, 0x26, 0x00, 0x00, 0x00};
  t.insert(t.end(), e1.begin(), e1.end());
  t.insert(t.end(), e2.begin(), e2.end());
  Bytes next = {0, 0, 0, 0, /* GPS IFD: 0 entries */ 0, 0, 0, 0, 0, 0};
  t.insert(t.end(), next.begin(), next.end());
  return t;
}

static bool contains(const Bytes& hay, const Bytes& needle) {
  return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

static Bytes synthetic_jpeg(Bytes& entropy_out) {
  Bytes j = {0xFF, 0xD8};
  Bytes jfif = str_bytes(std::string("JFIF\0", 5));
  Bytes rest = {1, 2, 0, 0, 72, 0, 72, 1, 1};  // 1x1 thumbnail
  jfif.insert(jfif.end(), rest.begin(), rest.end());
  jfif.insert(jfif.end(), {9, 9, 9});         // thumbnail RGB
  segment(j, 0xE0, jfif);
  Bytes exif = str_bytes(std::string("Exif\0\0", 6));
  Bytes tiff = exif_le(6);
  exif.insert(exif.end(), tiff.begin(), tiff.end());
  segment(j, 0xE1, exif);
  segment(j, 0xE1, str_bytes(std::string("http://ns.adobe.com/xap/1.0/\0<x:xmpmeta>SECRET-XMP</x:xmpmeta>", 63)));
  segment(j, 0xE2, str_bytes(std::string("ICC_PROFILE\0\x01\x01profile-bytes", 27)));
  segment(j, 0xED, str_bytes("Photoshop 3.0 SECRET-IPTC"));
  segment(j, 0xFE, str_bytes("SECRET-COMMENT"));
  segment(j, 0xEE, str_bytes("Adobe\x00\x64\x00\x00\x00\x00\x01"));
  segment(j, 0xDB, Bytes(65, 1));               // DQT
  segment(j, 0xC0, {8, 0, 1, 0, 1, 1, 1, 0x11, 0});  // SOF0
  segment(j, 0xC4, Bytes(20, 0));               // DHT
  segment(j, 0xDA, {1, 1, 0, 0, 63, 0});        // SOS
  entropy_out = {0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD0, 0x56, 0x78};
  j.insert(j.end(), entropy_out.begin(), entropy_out.end());
  j.push_back(0xFF);  // fill byte before the next marker
  segment(j, 0xFE, str_bytes("SECRET-COMMENT-2"));  // marker between scans
  segment(j, 0xDA, {1, 1, 0, 0, 63, 0});
  j.insert(j.end(), {0xAB, 0xCD});
  j.insert(j.end(), {0xFF, 0xD9});
  Bytes trailer = str_bytes("TRAILER-SECOND-IMAGE");
  j.insert(j.end(), trailer.begin(), trailer.end());
  return j;
}

static void png_chunk(Bytes& b, const std::string& type, const Bytes& data) {
  put32(b, static_cast<std::uint32_t>(data.size()));
  Bytes body = str_bytes(type);
  body.insert(body.end(), data.begin(), data.end());
  b.insert(b.end(), body.begin(), body.end());
  put32(b, crc32_png(body.data(), body.size()));
}

static Bytes synthetic_png() {
  Bytes p = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  png_chunk(p, "IHDR", {0, 0, 0, 1, 0, 0, 0, 1, 8, 2, 0, 0, 0});
  png_chunk(p, "tEXt", str_bytes(std::string("Author\0SECRET-TEXT", 18)));
  png_chunk(p, "iTXt", str_bytes(std::string("XML:com.adobe.xmp\0\0\0\0\0SECRET-ITXT", 34)));
  png_chunk(p, "pHYs", {0, 0, 11, 19, 0, 0, 11, 19, 1});
  png_chunk(p, "eXIf", exif_le(6));
  png_chunk(p, "IDAT", {0x78, 0x9C, 0x63, 0x60, 0x60, 0x60, 0x00, 0x00, 0x00, 0x04, 0x00, 0x01});
  png_chunk(p, "tIME", {0x07, 0xE8, 1, 2, 3, 4, 5});
  png_chunk(p, "IEND", {});
  Bytes trailer = str_bytes("TRAILER");
  p.insert(p.end(), trailer.begin(), trailer.end());
  return p;
}

static void test_metadata_strip() {
  CHECK(crc32_png(reinterpret_cast<const std::uint8_t*>("IEND"), 4) == 0xAE426082u);
  CHECK(crc32_png(reinterpret_cast<const std::uint8_t*>("123456789"), 9) == 0xCBF43926u);
  for (int o = 1; o <= 8; ++o) {
    Bytes t = minimal_exif_tiff(o);
    CHECK(exif_orientation(t.data(), t.size()) == o);
  }
  Bytes le = exif_le(3);
  CHECK(exif_orientation(le.data(), le.size()) == 3);
  CHECK(exif_orientation(le.data(), 12) == 0);  // truncated IFD
  CHECK(exif_orientation(nullptr, 0) == 0);

  // JPEG
  Bytes entropy;
  Bytes jpeg = synthetic_jpeg(entropy);
  StripResult r = strip_metadata(jpeg);
  CHECK(r.ok);
  CHECK(r.report.format == "jpeg");
  CHECK(r.report.orientation == 6 && r.report.orientation_written);
  for (const char* secret : {"SECRET-XMP", "SECRET-IPTC", "SECRET-COMMENT", "TRAILER-SECOND-IMAGE"}) {
    CHECK(!contains(r.data, str_bytes(secret)));
  }
  CHECK(!contains(r.data, {0x25, 0x88}));       // GPS pointer tag (LE) gone
  CHECK(!contains(r.data, {9, 9, 9}));          // JFIF thumbnail gone
  CHECK(contains(r.data, entropy));             // scan data byte-identical
  CHECK(contains(r.data, {0xAB, 0xCD, 0xFF, 0xD9}));
  CHECK(contains(r.data, str_bytes("profile-bytes")));
  CHECK(contains(r.data, str_bytes("Adobe")));
  CHECK(r.data.size() >= 4 && r.data[r.data.size() - 2] == 0xFF && r.data.back() == 0xD9);
  // Output: SOI, JFIF APP0 (16 bytes), minimal EXIF APP1 (34 bytes).
  CHECK(r.data.size() > 40 && r.data[2] == 0xFF && r.data[3] == 0xE0 && r.data[5] == 16);
  CHECK(r.data[20] == 0xFF && r.data[21] == 0xE1 && r.data[23] == 34);
  CHECK(exif_orientation(r.data.data() + 30, 26) == 6);
  // Idempotent: stripping the output changes nothing.
  StripResult r2 = strip_metadata(r.data);
  CHECK(r2.ok && r2.data == r.data);

  StripOptions no_orient;
  no_orient.keep_orientation = false;
  no_orient.keep_icc = false;
  StripResult r3 = strip_metadata(jpeg, no_orient);
  CHECK(r3.ok && !r3.report.orientation_written);
  CHECK(!contains(r3.data, str_bytes("Exif")));
  CHECK(!contains(r3.data, str_bytes("profile-bytes")));

  // Malformed JPEGs are rejected.
  CHECK(!strip_metadata(Bytes{0xFF, 0xD8, 0xFF}).ok);
  Bytes no_eoi(jpeg.begin(), jpeg.end() - 22);
  CHECK(!strip_metadata(no_eoi).ok);
  Bytes bad_len = {0xFF, 0xD8, 0xFF, 0xE1, 0x00, 0x01, 0xFF, 0xD9};
  CHECK(!strip_metadata(bad_len).ok);

  // PNG
  Bytes png = synthetic_png();
  StripResult p = strip_metadata(png);
  CHECK(p.ok && p.report.format == "png");
  for (const char* secret : {"SECRET-TEXT", "SECRET-ITXT", "TRAILER", "tIME", "tEXt", "iTXt"}) {
    CHECK(!contains(p.data, str_bytes(secret)));
  }
  CHECK(contains(p.data, str_bytes("pHYs")));
  CHECK(p.report.orientation == 6 && p.report.orientation_written);
  // Every chunk has a valid CRC; eXIf precedes IDAT and holds only orientation.
  std::size_t pos = 8;
  std::vector<std::string> order;
  bool crc_ok = true;
  while (pos + 12 <= p.data.size()) {
    std::uint32_t len = (std::uint32_t(p.data[pos]) << 24) | (p.data[pos + 1] << 16) | (p.data[pos + 2] << 8) | p.data[pos + 3];
    std::string type(p.data.begin() + static_cast<long>(pos) + 4, p.data.begin() + static_cast<long>(pos) + 8);
    std::size_t c = pos + 8 + len;
    std::uint32_t stored = (std::uint32_t(p.data[c]) << 24) | (p.data[c + 1] << 16) | (p.data[c + 2] << 8) | p.data[c + 3];
    crc_ok = crc_ok && crc32_png(p.data.data() + pos + 4, 4 + len) == stored;
    if (type == "eXIf") CHECK(exif_orientation(p.data.data() + pos + 8, len) == 6 && len == 26);
    order.push_back(type);
    pos = c + 4;
  }
  CHECK(crc_ok);
  CHECK(pos == p.data.size());
  CHECK((order == std::vector<std::string>{"IHDR", "pHYs", "eXIf", "IDAT", "IEND"}));
  StripResult p2 = strip_metadata(p.data);
  CHECK(p2.ok && p2.data == p.data);

  Bytes bad_crc = png;
  bad_crc[8 + 8 + 13] ^= 1;  // IHDR CRC
  CHECK(!strip_metadata(bad_crc).ok);
  CHECK(!strip_metadata(Bytes(png.begin(), png.begin() + 40)).ok);
  CHECK(!strip_metadata(str_bytes("GIF89a....")).ok);
  CHECK(!strip_metadata(Bytes{}).ok);

  // Robustness: every truncation and random corruption must fail cleanly or
  // succeed; never crash (run under ASan/UBSan).
  std::mt19937 rng(1234);
  int survived = 0;
  for (const Bytes* src : {&jpeg, &png}) {
    for (std::size_t n = 0; n <= src->size(); ++n) {
      Bytes cut(src->begin(), src->begin() + static_cast<long>(n));
      StripResult x = strip_metadata(cut);
      if (x.ok) ++survived;
    }
    for (int i = 0; i < 3000; ++i) {
      Bytes m = *src;
      int flips = 1 + static_cast<int>(rng() % 4);
      for (int k = 0; k < flips; ++k) m[rng() % m.size()] = static_cast<std::uint8_t>(rng());
      StripResult x = strip_metadata(m);
      if (x.ok) ++survived;
    }
  }
  CHECK(survived > 0);
}

int main() {
  test_ghost_policy();
  test_ghost_client();
  test_update_router();
  test_archive();
  test_auth_flow();
  test_metadata_strip();
  std::printf("%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
