// mc1h-tg — ITransport wrapper that enforces GhostPolicy on outgoing requests.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>

#include "core/ghost_policy.h"
#include "core/json.h"
#include "core/transport.h"

namespace mc1h {

class GhostClient {
 public:
  using BlockedCallback = std::function<void(const json& request, const GhostDecision& decision)>;

  explicit GhostClient(ITransport& transport, GhostPolicy policy = {});

  // Thread-safe. A blocked request is not forwarded; a synthetic
  // {"@type":"ok","@extra":<request @extra>} is queued for receive().
  void send(const json& request);

  // Single consumer. Synthetic responses are delivered before transport
  // output. A synthetic response queued while this call waits inside the
  // transport is delivered on the next call, so its latency is bounded by
  // timeout_seconds.
  std::optional<json> receive(double timeout_seconds);

  // Synchronous request; the policy applies here too.
  json execute(const json& request);

  // Policy access is serialized with send().
  GhostPolicy policy_snapshot() const;
  void update_policy(const std::function<void(GhostPolicy&)>& mutate);

  void set_blocked_callback(BlockedCallback cb);
  std::uint64_t blocked_count() const { return blocked_.load(); }
  std::uint64_t forwarded_count() const { return forwarded_.load(); }

 private:
  json synthetic_ok(const json& request) const;

  ITransport& transport_;
  mutable std::mutex mu_;
  GhostPolicy policy_;
  BlockedCallback on_blocked_;
  std::deque<json> local_;
  std::atomic<std::uint64_t> blocked_{0};
  std::atomic<std::uint64_t> forwarded_{0};
};

}  // namespace mc1h
