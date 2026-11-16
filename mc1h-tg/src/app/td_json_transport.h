// mc1h-tg — ITransport over TDLib's td_json_client.h (td_create_client_id API).
#pragma once

#include <string>

#include "core/transport.h"

namespace mc1h {

// One TDLib client. td_receive() is process-global and returns objects for
// every client; objects that belong to another TdJsonTransport are parked
// in a process-wide mailbox and handed to their owner on its next receive().
// Contract: receive() of all instances must be driven from ONE thread
// (td_json_client.h forbids concurrent td_receive calls).
class TdJsonTransport final : public ITransport {
 public:
  TdJsonTransport();
  ~TdJsonTransport() override;
  TdJsonTransport(const TdJsonTransport&) = delete;
  TdJsonTransport& operator=(const TdJsonTransport&) = delete;

  int client_id() const { return client_id_; }

  void send(const std::string& request_json) override;
  std::optional<std::string> receive(double timeout_seconds) override;
  std::string execute(const std::string& request_json) override;

  // Static helper for requests that do not need a client (logging setup).
  static std::string execute_static(const std::string& request_json);

 private:
  int client_id_ = 0;
};

}  // namespace mc1h
