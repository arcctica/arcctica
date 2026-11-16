#include "core/update_router.h"

#include <utility>

namespace mc1h {

void UpdateRouter::on(const std::string& type, Handler handler) {
  std::lock_guard<std::mutex> lock(mu_);
  typed_[type].push_back(std::move(handler));
}

void UpdateRouter::on_any(Handler handler) {
  std::lock_guard<std::mutex> lock(mu_);
  any_.push_back(std::move(handler));
}

json UpdateRouter::track(json request, Handler on_response) {
  std::lock_guard<std::mutex> lock(mu_);
  std::int64_t extra = next_extra_++;
  request["@extra"] = extra;
  pending_[extra] = std::move(on_response);
  return request;
}

void UpdateRouter::route(const json& object) {
  Handler response_handler;
  std::vector<Handler> handlers;
  {
    std::lock_guard<std::mutex> lock(mu_);
    ++routed_;
    if (object.is_object()) {
      auto it = object.find("@extra");
      if (it != object.end() && it->is_number_integer()) {
        auto p = pending_.find(it->get<std::int64_t>());
        if (p != pending_.end()) {
          response_handler = std::move(p->second);
          pending_.erase(p);
        }
      }
    }
    if (!response_handler) {
      auto t = typed_.find(jtype(object));
      if (t != typed_.end()) handlers = t->second;
    }
    handlers.insert(handlers.end(), any_.begin(), any_.end());
  }
  // Callbacks run without the lock so they may call track()/on().
  if (response_handler) response_handler(object);
  for (auto& h : handlers) h(object);
}

std::size_t UpdateRouter::pending() const {
  std::lock_guard<std::mutex> lock(mu_);
  return pending_.size();
}

}  // namespace mc1h
