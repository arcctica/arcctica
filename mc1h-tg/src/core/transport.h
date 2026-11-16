// mc1h-tg — transport abstraction between the core and TDLib's JSON interface.
#pragma once

#include <optional>
#include <string>

namespace mc1h {

// One TDLib client instance seen through its JSON interface.
//
// Threading contract (mirrors td_json_client.h):
//   * send() and execute() may be called from any thread.
//   * receive() has a single consumer thread.
class ITransport {
 public:
  virtual ~ITransport() = default;

  // Queues an asynchronous request. The response arrives through receive()
  // and carries the request's "@extra" unchanged.
  virtual void send(const std::string& request_json) = 0;

  // Waits up to timeout_seconds for the next update or response.
  virtual std::optional<std::string> receive(double timeout_seconds) = 0;

  // Synchronous request. TDLib accepts only methods documented as
  // "Can be called synchronously" here.
  virtual std::string execute(const std::string& request_json) = 0;
};

}  // namespace mc1h
