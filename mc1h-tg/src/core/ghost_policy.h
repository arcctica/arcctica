// mc1h-tg — client-side ghost mode policy.
//
// The policy only decides whether an outgoing TDLib request may leave the
// client. It never touches the server: a blocked request is simply not sent
// (GhostClient answers it locally with a synthetic "ok").
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "core/json.h"

namespace mc1h {

enum class GhostFeature {
  ReadReceipts,  // viewMessages, readAll*, readChatList, readBusinessMessage
  Typing,        // sendChatAction
  Online,        // setOption("online", true)
  StoryViews,    // openStory
  ContentOpens,  // openMessageContent (voice/video note "listened", self-destruct timer start)
  AdViews,       // viewSponsoredChat, viewVideoMessageAdvertisement
};

const char* to_string(GhostFeature f);
std::optional<GhostFeature> ghost_feature_from_string(const std::string& name);

struct GhostSettings {
  bool hide_read_receipts = false;
  bool hide_typing = false;
  bool hide_online = false;
  bool hide_story_views = false;
  bool hide_content_opens = false;
  bool hide_ad_views = false;

  bool get(GhostFeature f) const;
  void set(GhostFeature f, bool value);
};

// Per-chat override: an unset field inherits the account setting.
struct GhostChatOverride {
  std::optional<bool> hide_read_receipts;
  std::optional<bool> hide_typing;
  std::optional<bool> hide_story_views;
  std::optional<bool> hide_content_opens;

  std::optional<bool> get(GhostFeature f) const;
  // Returns false when the feature has no per-chat meaning (Online, AdViews).
  bool set(GhostFeature f, std::optional<bool> value);
  bool empty() const;
};

struct GhostDecision {
  bool block = false;
  std::optional<GhostFeature> feature;  // set when the request is ghost-relevant
  std::int64_t chat_id = 0;             // 0 when the request is not chat-scoped
  std::string method;
};

class GhostPolicy {
 public:
  GhostSettings& account() { return account_; }
  const GhostSettings& account() const { return account_; }

  void set_chat_override(std::int64_t chat_id, GhostFeature f, std::optional<bool> value);
  const GhostChatOverride* chat_override(std::int64_t chat_id) const;
  const std::map<std::int64_t, GhostChatOverride>& chat_overrides() const { return chats_; }

  // Effective setting for a feature in a chat (chat_id == 0: account only).
  bool hides(GhostFeature f, std::int64_t chat_id) const;

  GhostDecision evaluate(const json& request) const;

  json to_json() const;
  // Unknown keys are ignored; malformed values leave defaults and set *error.
  static GhostPolicy from_json(const json& j, std::string* error = nullptr);

 private:
  GhostSettings account_;
  std::map<std::int64_t, GhostChatOverride> chats_;
};

}  // namespace mc1h
