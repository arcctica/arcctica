// mc1h-tg — feeds TDLib updates and history responses into the Archive.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "core/archive.h"
#include "core/update_router.h"

namespace mc1h {

struct RecorderStats {
  std::uint64_t messages = 0;
  std::uint64_t edits_seen = 0;
  std::uint64_t deletions_marked = 0;    // permanent deletions of archived messages
  std::uint64_t deletions_unknown = 0;   // permanent deletions of never-seen messages
  std::uint64_t deletions_ignored = 0;   // !is_permanent || from_cache
  std::uint64_t errors = 0;
};

class ArchiveRecorder {
 public:
  using Clock = std::function<std::int64_t()>;
  using ErrorSink = std::function<void(const std::string&)>;

  ArchiveRecorder(Archive& archive, Clock clock, ErrorSink on_error = {});

  // Subscribes to updateNewMessage, updateMessageContent, updateMessageEdited,
  // updateMessageSendSucceeded, updateDeleteMessages, updateNewChat,
  // updateChatTitle, updateUser.
  void attach(UpdateRouter& router);

  // Archives the `message` / `messages` objects of a request response
  // (getChatHistory, getMessage, searchMessages ...).
  void record_response(const json& response);

  // Single entry for one update; public for tests.
  void handle(const json& update);

  const RecorderStats& stats() const { return stats_; }

 private:
  void error(const std::string& what);

  Archive& archive_;
  Clock clock_;
  ErrorSink on_error_;
  RecorderStats stats_;
};

}  // namespace mc1h
