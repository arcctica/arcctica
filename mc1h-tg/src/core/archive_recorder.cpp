#include "core/archive_recorder.h"

#include <utility>
#include <vector>

namespace mc1h {

ArchiveRecorder::ArchiveRecorder(Archive& archive, Clock clock, ErrorSink on_error)
    : archive_(archive), clock_(std::move(clock)), on_error_(std::move(on_error)) {}

void ArchiveRecorder::error(const std::string& what) {
  ++stats_.errors;
  if (on_error_) on_error_(what + ": " + archive_.last_error());
}

void ArchiveRecorder::attach(UpdateRouter& router) {
  for (const char* type : {"updateNewMessage", "updateMessageContent", "updateMessageEdited",
                           "updateMessageSendSucceeded", "updateDeleteMessages", "updateNewChat", "updateChatTitle",
                           "updateUser"}) {
    router.on(type, [this](const json& u) { handle(u); });
  }
}

void ArchiveRecorder::record_response(const json& response) {
  const std::string t = jtype(response);
  if (t == "message") {
    if (archive_.upsert_message(response, clock_())) {
      ++stats_.messages;
    } else {
      error("archive message");
    }
  } else if (t == "messages" || t == "foundMessages" || t == "foundChatMessages") {
    const json& list = jobj(response, "messages");
    if (!list.is_array()) return;
    for (const json& m : list) {
      if (m.is_null()) continue;  // getMessages returns null for missing ids
      if (archive_.upsert_message(m, clock_())) {
        ++stats_.messages;
      } else {
        error("archive message");
      }
    }
  }
}

void ArchiveRecorder::handle(const json& u) {
  const std::string t = jtype(u);
  const std::int64_t now = clock_();
  if (t == "updateNewMessage") {
    if (archive_.upsert_message(jobj(u, "message"), now)) {
      ++stats_.messages;
    } else {
      error("updateNewMessage");
    }
  } else if (t == "updateMessageContent") {
    ++stats_.edits_seen;
    if (!archive_.apply_content(jint(u, "chat_id"), jint(u, "message_id"), jobj(u, "new_content"), now)) {
      error("updateMessageContent");
    }
  } else if (t == "updateMessageEdited") {
    if (!archive_.apply_edited(jint(u, "chat_id"), jint(u, "message_id"), jint(u, "edit_date"))) {
      error("updateMessageEdited");
    }
  } else if (t == "updateMessageSendSucceeded") {
    if (!archive_.apply_send_succeeded(jobj(u, "message"), jint(u, "old_message_id"), now)) {
      error("updateMessageSendSucceeded");
    }
  } else if (t == "updateDeleteMessages") {
    std::vector<std::int64_t> ids;
    const json& arr = jobj(u, "message_ids");
    if (arr.is_array()) {
      for (const json& v : arr) {
        if (v.is_number_integer()) ids.push_back(v.get<std::int64_t>());
      }
    }
    // Only a permanent deletion that did not come from cache eviction is a
    // real deletion (td_api.tl: updateDeleteMessages is_permanent, from_cache).
    if (!jbool(u, "is_permanent") || jbool(u, "from_cache")) {
      stats_.deletions_ignored += ids.size();
      return;
    }
    int found = archive_.mark_deleted(jint(u, "chat_id"), ids, now);
    stats_.deletions_marked += static_cast<std::uint64_t>(found);
    stats_.deletions_unknown += ids.size() - static_cast<std::size_t>(found);
  } else if (t == "updateNewChat") {
    const json& chat = jobj(u, "chat");
    if (!archive_.upsert_chat(jint(chat, "id"), jstr(chat, "title"), jtype(jobj(chat, "type")))) {
      error("updateNewChat");
    }
  } else if (t == "updateChatTitle") {
    if (!archive_.set_chat_title(jint(u, "chat_id"), jstr(u, "title"))) error("updateChatTitle");
  } else if (t == "updateUser") {
    const json& user = jobj(u, "user");
    std::string username;
    const json& names = jobj(jobj(user, "usernames"), "active_usernames");
    if (names.is_array() && !names.empty() && names.front().is_string()) username = names.front().get<std::string>();
    if (!archive_.upsert_user(jint(user, "id"), jstr(user, "first_name"), jstr(user, "last_name"), username)) {
      error("updateUser");
    }
  }
}

}  // namespace mc1h
