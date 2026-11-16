// mc1h-tg — dispatch of TDLib updates and request responses.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "core/json.h"

namespace mc1h {

class UpdateRouter {
 public:
  using Handler = std::function<void(const json&)>;

  // Handlers for objects whose "@type" equals `type` (updates, and responses
  // that are not tracked). Thread-safe; handlers run on the routing thread.
  void on(const std::string& type, Handler handler);
  // Handler for every routed object, called after typed handlers.
  void on_any(Handler handler);

  // Assigns a unique integer "@extra" and remembers `on_response`. The
  // response (including an "error" object) goes to that callback only.
  // Thread-safe. Returns the request ready to be sent.
  json track(json request, Handler on_response);

  // Routes one object received from TDLib.
  void route(const json& object);

  std::size_t pending() const;
  std::uint64_t routed() const { return routed_.load(); }

 private:
  mutable std::mutex mu_;
  std::map<std::string, std::vector<Handler>> typed_;
  std::vector<Handler> any_;
  std::map<std::int64_t, Handler> pending_;
  std::int64_t next_extra_ = 1;
  std::atomic<std::uint64_t> routed_{0};
};

}  // namespace mc1h
