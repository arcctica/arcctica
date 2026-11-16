#include "app/td_json_transport.h"

#include <td/telegram/td_json_client.h>

#include <deque>
#include <map>
#include <mutex>

#include "core/json.h"

namespace mc1h {
namespace {

struct Mailbox {
  std::mutex mu;
  std::map<int, std::deque<std::string>> parked;
};

Mailbox& mailbox() {
  static Mailbox m;
  return m;
}

// "@client_id" is added by td_receive to every object (td_json_client.h).
int client_id_of(const char* raw) {
  json j = json::parse(raw, nullptr, false);
  if (j.is_discarded()) return 0;
  return static_cast<int>(jint(j, "@client_id", 0));
}

}  // namespace

TdJsonTransport::TdJsonTransport() : client_id_(td_create_client_id()) {}

TdJsonTransport::~TdJsonTransport() {
  std::lock_guard<std::mutex> lock(mailbox().mu);
  mailbox().parked.erase(client_id_);
}

void TdJsonTransport::send(const std::string& request_json) { td_send(client_id_, request_json.c_str()); }

std::optional<std::string> TdJsonTransport::receive(double timeout_seconds) {
  {
    std::lock_guard<std::mutex> lock(mailbox().mu);
    auto it = mailbox().parked.find(client_id_);
    if (it != mailbox().parked.end() && !it->second.empty()) {
      std::string s = std::move(it->second.front());
      it->second.pop_front();
      return s;
    }
  }
  const char* raw = td_receive(timeout_seconds);
  if (raw == nullptr) return std::nullopt;
  const int owner = client_id_of(raw);
  if (owner == 0 || owner == client_id_) return std::string(raw);
  std::lock_guard<std::mutex> lock(mailbox().mu);
  mailbox().parked[owner].emplace_back(raw);
  return std::nullopt;
}

std::string TdJsonTransport::execute(const std::string& request_json) { return execute_static(request_json); }

std::string TdJsonTransport::execute_static(const std::string& request_json) {
  const char* raw = td_execute(request_json.c_str());
  return raw ? std::string(raw) : std::string(R"({"@type":"error","code":400,"message":"td_execute returned null"})");
}

}  // namespace mc1h
