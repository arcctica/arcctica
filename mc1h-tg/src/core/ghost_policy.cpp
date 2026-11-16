#include "core/ghost_policy.h"

#include <array>
#include <cstring>
#include <utility>

namespace mc1h {
namespace {

struct MethodRule {
  const char* method;
  GhostFeature feature;
  const char* chat_field;  // nullptr when the request has no chat scope
};

// Method names and fields checked against td/generate/scheme/td_api.tl
// at TDLib 1.8.70 (03220f6b).
constexpr std::array<MethodRule, 16> kRules{{
    {"viewMessages", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllChatMentions", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllChatReactions", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllChatPollVotes", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllForumTopicMentions", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllForumTopicReactions", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllForumTopicPollVotes", GhostFeature::ReadReceipts, "chat_id"},
    {"readAllDirectMessagesChatTopicReactions", GhostFeature::ReadReceipts, "chat_id"},
    {"readBusinessMessage", GhostFeature::ReadReceipts, "chat_id"},
    {"readChatList", GhostFeature::ReadReceipts, nullptr},
    {"sendChatAction", GhostFeature::Typing, "chat_id"},
    {"openStory", GhostFeature::StoryViews, "story_poster_chat_id"},
    {"openMessageContent", GhostFeature::ContentOpens, "chat_id"},
    {"viewSponsoredChat", GhostFeature::AdViews, nullptr},
    {"viewVideoMessageAdvertisement", GhostFeature::AdViews, nullptr},
    {"setOption", GhostFeature::Online, nullptr},  // filtered further below
}};

struct FeatureName {
  GhostFeature feature;
  const char* name;
};

constexpr std::array<FeatureName, 6> kFeatureNames{{
    {GhostFeature::ReadReceipts, "reads"},
    {GhostFeature::Typing, "typing"},
    {GhostFeature::Online, "online"},
    {GhostFeature::StoryViews, "stories"},
    {GhostFeature::ContentOpens, "opens"},
    {GhostFeature::AdViews, "ads"},
}};

bool is_online_true(const json& request) {
  if (jstr(request, "name") != "online") return false;
  const json& value = jobj(request, "value");
  // optionValueBoolean value:Bool. Anything else cannot turn "online" on.
  return jtype(value) == "optionValueBoolean" && jbool(value, "value", false);
}

}  // namespace

const char* to_string(GhostFeature f) {
  for (const auto& n : kFeatureNames) {
    if (n.feature == f) return n.name;
  }
  return "?";
}

std::optional<GhostFeature> ghost_feature_from_string(const std::string& name) {
  for (const auto& n : kFeatureNames) {
    if (name == n.name) return n.feature;
  }
  return std::nullopt;
}

bool GhostSettings::get(GhostFeature f) const {
  switch (f) {
    case GhostFeature::ReadReceipts: return hide_read_receipts;
    case GhostFeature::Typing: return hide_typing;
    case GhostFeature::Online: return hide_online;
    case GhostFeature::StoryViews: return hide_story_views;
    case GhostFeature::ContentOpens: return hide_content_opens;
    case GhostFeature::AdViews: return hide_ad_views;
  }
  return false;
}

void GhostSettings::set(GhostFeature f, bool value) {
  switch (f) {
    case GhostFeature::ReadReceipts: hide_read_receipts = value; break;
    case GhostFeature::Typing: hide_typing = value; break;
    case GhostFeature::Online: hide_online = value; break;
    case GhostFeature::StoryViews: hide_story_views = value; break;
    case GhostFeature::ContentOpens: hide_content_opens = value; break;
    case GhostFeature::AdViews: hide_ad_views = value; break;
  }
}

std::optional<bool> GhostChatOverride::get(GhostFeature f) const {
  switch (f) {
    case GhostFeature::ReadReceipts: return hide_read_receipts;
    case GhostFeature::Typing: return hide_typing;
    case GhostFeature::StoryViews: return hide_story_views;
    case GhostFeature::ContentOpens: return hide_content_opens;
    case GhostFeature::Online:
    case GhostFeature::AdViews: return std::nullopt;
  }
  return std::nullopt;
}

bool GhostChatOverride::set(GhostFeature f, std::optional<bool> value) {
  switch (f) {
    case GhostFeature::ReadReceipts: hide_read_receipts = value; return true;
    case GhostFeature::Typing: hide_typing = value; return true;
    case GhostFeature::StoryViews: hide_story_views = value; return true;
    case GhostFeature::ContentOpens: hide_content_opens = value; return true;
    case GhostFeature::Online:
    case GhostFeature::AdViews: return false;
  }
  return false;
}

bool GhostChatOverride::empty() const {
  return !hide_read_receipts && !hide_typing && !hide_story_views && !hide_content_opens;
}

void GhostPolicy::set_chat_override(std::int64_t chat_id, GhostFeature f, std::optional<bool> value) {
  auto& o = chats_[chat_id];
  o.set(f, value);
  if (o.empty()) chats_.erase(chat_id);
}

const GhostChatOverride* GhostPolicy::chat_override(std::int64_t chat_id) const {
  auto it = chats_.find(chat_id);
  return it == chats_.end() ? nullptr : &it->second;
}

bool GhostPolicy::hides(GhostFeature f, std::int64_t chat_id) const {
  if (chat_id != 0) {
    if (const auto* o = chat_override(chat_id)) {
      if (auto v = o->get(f)) return *v;
    }
  }
  return account_.get(f);
}

GhostDecision GhostPolicy::evaluate(const json& request) const {
  GhostDecision d;
  d.method = jtype(request);
  for (const auto& rule : kRules) {
    if (d.method != rule.method) continue;
    if (rule.feature == GhostFeature::Online && !is_online_true(request)) return d;
    d.feature = rule.feature;
    if (rule.chat_field != nullptr) d.chat_id = jint(request, rule.chat_field, 0);
    d.block = hides(rule.feature, d.chat_id);
    return d;
  }
  return d;
}

json GhostPolicy::to_json() const {
  json j;
  json acc = json::object();
  for (const auto& n : kFeatureNames) acc[n.name] = account_.get(n.feature);
  j["account"] = std::move(acc);
  json chats = json::object();
  for (const auto& [chat_id, o] : chats_) {
    json c = json::object();
    for (const auto& n : kFeatureNames) {
      if (auto v = o.get(n.feature)) c[n.name] = *v;
    }
    chats[std::to_string(chat_id)] = std::move(c);
  }
  j["chats"] = std::move(chats);
  j["version"] = 1;
  return j;
}

GhostPolicy GhostPolicy::from_json(const json& j, std::string* error) {
  GhostPolicy p;
  auto fail = [&](const std::string& msg) {
    if (error != nullptr && error->empty()) *error = msg;
  };
  if (!j.is_object()) {
    fail("ghost settings: root is not an object");
    return p;
  }
  const json& acc = jobj(j, "account");
  if (acc.is_object()) {
    for (const auto& n : kFeatureNames) {
      auto it = acc.find(n.name);
      if (it == acc.end()) continue;
      if (!it->is_boolean()) {
        fail(std::string("ghost settings: account.") + n.name + " is not a boolean");
        continue;
      }
      p.account_.set(n.feature, it->get<bool>());
    }
  }
  const json& chats = jobj(j, "chats");
  if (chats.is_object()) {
    for (auto it = chats.begin(); it != chats.end(); ++it) {
      std::int64_t chat_id = 0;
      try {
        std::size_t pos = 0;
        chat_id = std::stoll(it.key(), &pos, 10);
        if (pos != it.key().size()) chat_id = 0;
      } catch (...) {
        chat_id = 0;
      }
      if (chat_id == 0 || !it.value().is_object()) {
        fail("ghost settings: bad chat entry '" + it.key() + "'");
        continue;
      }
      for (const auto& n : kFeatureNames) {
        auto v = it.value().find(n.name);
        if (v == it.value().end()) continue;
        if (!v->is_boolean()) {
          fail("ghost settings: chat " + it.key() + "." + n.name + " is not a boolean");
          continue;
        }
        GhostChatOverride probe;
        if (!probe.set(n.feature, true)) {
          fail(std::string("ghost settings: '") + n.name + "' has no per-chat meaning");
          continue;
        }
        p.set_chat_override(chat_id, n.feature, v->get<bool>());
      }
    }
  }
  return p;
}

}  // namespace mc1h
