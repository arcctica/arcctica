#include "core/ghost_client.h"

#include <utility>

namespace mc1h {

GhostClient::GhostClient(ITransport& transport, GhostPolicy policy)
    : transport_(transport), policy_(std::move(policy)) {}

json GhostClient::synthetic_ok(const json& request) const {
  json ok = {{"@type", "ok"}};
  if (request.is_object()) {
    auto it = request.find("@extra");
    if (it != request.end()) ok["@extra"] = *it;
  }
  return ok;
}

void GhostClient::send(const json& request) {
  GhostDecision decision;
  BlockedCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    decision = policy_.evaluate(request);
    if (decision.block) {
      local_.push_back(synthetic_ok(request));
      cb = on_blocked_;
    }
  }
  if (decision.block) {
    ++blocked_;
    if (cb) cb(request, decision);
    return;
  }
  ++forwarded_;
  transport_.send(request.dump());
}

std::optional<json> GhostClient::receive(double timeout_seconds) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!local_.empty()) {
      json front = std::move(local_.front());
      local_.pop_front();
      return front;
    }
  }
  auto raw = transport_.receive(timeout_seconds);
  if (!raw) return std::nullopt;
  json parsed = json::parse(*raw, nullptr, /*allow_exceptions=*/false);
  if (parsed.is_discarded()) {
    return json{{"@type", "error"}, {"code", 500}, {"message", "mc1h: transport returned invalid JSON"}};
  }
  return parsed;
}

json GhostClient::execute(const json& request) {
  GhostDecision decision;
  BlockedCallback cb;
  {
    std::lock_guard<std::mutex> lock(mu_);
    decision = policy_.evaluate(request);
    cb = on_blocked_;
  }
  if (decision.block) {
    ++blocked_;
    if (cb) cb(request, decision);
    return synthetic_ok(request);
  }
  ++forwarded_;
  json parsed = json::parse(transport_.execute(request.dump()), nullptr, false);
  if (parsed.is_discarded()) {
    return json{{"@type", "error"}, {"code", 500}, {"message", "mc1h: transport returned invalid JSON"}};
  }
  return parsed;
}

GhostPolicy GhostClient::policy_snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return policy_;
}

void GhostClient::update_policy(const std::function<void(GhostPolicy&)>& mutate) {
  std::lock_guard<std::mutex> lock(mu_);
  mutate(policy_);
}

void GhostClient::set_blocked_callback(BlockedCallback cb) {
  std::lock_guard<std::mutex> lock(mu_);
  on_blocked_ = std::move(cb);
}

}  // namespace mc1h
