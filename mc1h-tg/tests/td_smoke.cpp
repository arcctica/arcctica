// Smoke test against the real libtdjson, no Telegram account needed:
// ghost-blocked requests never reach TDLib, allowed ones round-trip with @extra,
// and the instance closes cleanly.
#include <chrono>
#include <functional>
#include <cstdio>
#include <string>

#include "app/td_json_transport.h"
#include "core/ghost_client.h"
#include "core/update_router.h"

using namespace mc1h;

static int g_fail = 0;
#define CHECK(c)                                              \
  do {                                                        \
    if (!(c)) {                                               \
      ++g_fail;                                               \
      std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); \
    }                                                         \
  } while (0)

int main() {
  TdJsonTransport::execute_static(R"({"@type":"setLogVerbosityLevel","new_verbosity_level":0})");
  TdJsonTransport transport;
  GhostClient client(transport);
  UpdateRouter router;

  json version, blocked_reply, forwarded_reply;
  std::string last_auth_state;
  router.on("updateAuthorizationState",
            [&](const json& u) { last_auth_state = jtype(jobj(u, "authorization_state")); });

  auto pump_until = [&](const std::function<bool()>& done, double seconds) {
    auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done() && std::chrono::steady_clock::now() < end) {
      if (auto o = client.receive(0.1)) router.route(*o);
    }
    return done();
  };

  client.send(router.track(json{{"@type", "getOption"}, {"name", "version"}}, [&](const json& r) { version = r; }));
  CHECK(pump_until([&] { return !version.is_null(); }, 10));
  CHECK(jstr(version, "value") == "1.8.70");
  CHECK(pump_until([&] { return last_auth_state == "authorizationStateWaitTdlibParameters"; }, 10));

  // Ghost on: viewMessages must not reach TDLib.
  client.update_policy([](GhostPolicy& p) { p.account().hide_read_receipts = true; });
  client.send(router.track(json{{"@type", "viewMessages"}, {"chat_id", 1}, {"message_ids", {1}}, {"force_read", true}},
                           [&](const json& r) { blocked_reply = r; }));
  CHECK(pump_until([&] { return !blocked_reply.is_null(); }, 5));
  CHECK(jtype(blocked_reply) == "ok");
  CHECK(client.blocked_count() == 1);

  // Ghost off: the same request goes to TDLib, which refuses it before init.
  client.update_policy([](GhostPolicy& p) { p.account().hide_read_receipts = false; });
  client.send(router.track(json{{"@type", "viewMessages"}, {"chat_id", 1}, {"message_ids", {1}}, {"force_read", true}},
                           [&](const json& r) { forwarded_reply = r; }));
  CHECK(pump_until([&] { return !forwarded_reply.is_null(); }, 10));
  CHECK(jtype(forwarded_reply) == "error");
  std::printf("TDLib reply to forwarded viewMessages: %s\n", forwarded_reply.dump().c_str());

  // Synchronous path.
  json ents = client.execute(json{{"@type", "getTextEntities"}, {"text", "see https://example.org @name"}});
  CHECK(jtype(ents) == "textEntities");
  CHECK(jobj(ents, "entities").size() == 2);

  client.send(json{{"@type", "close"}});
  CHECK(pump_until([&] { return last_auth_state == "authorizationStateClosed"; }, 20));
  CHECK(router.pending() == 0);
  std::printf("td_smoke: %s\n", g_fail == 0 ? "ok" : "FAILED");
  return g_fail == 0 ? 0 : 1;
}
