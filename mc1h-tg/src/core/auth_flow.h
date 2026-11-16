// mc1h-tg — TDLib authorization state machine, UI-agnostic.
//
// The caller feeds authorizationState* objects and user answers; the flow
// returns the request to send and/or the next prompt. No I/O happens here.
#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/json.h"

namespace mc1h {

struct TdlibParams {
  bool use_test_dc = false;
  std::string database_directory;
  std::string files_directory;
  std::string database_encryption_key;  // raw bytes; empty = unencrypted TDLib DB
  bool use_file_database = true;
  bool use_chat_info_database = true;
  bool use_message_database = true;
  bool use_secret_chats = true;
  std::int32_t api_id = 0;
  std::string api_hash;
  std::string system_language_code = "en";
  std::string device_model = "Desktop";
  std::string system_version;
  std::string application_version = "0.1";
};

std::string base64_encode(const std::string& raw);

// setTdlibParameters (td_api.tl, TDLib 1.8.70). `bytes` fields are base64 in JSON.
json make_set_tdlib_parameters(const TdlibParams& p);

enum class AuthPrompt { None, Phone, Code, Password, FirstName, LastName, Email, EmailCode };

struct AuthStep {
  std::optional<json> request;  // send it (an auth request; its error goes to on_error)
  AuthPrompt prompt = AuthPrompt::None;
  std::string prompt_text;
  bool secret = false;  // do not echo the answer
  std::string info;     // message for the user
  bool ready = false;   // authorizationStateReady
  bool closed = false;  // authorizationStateClosed: the TDLib instance is gone
  bool fatal = false;   // cannot continue (info explains)
};

class AuthFlow {
 public:
  explicit AuthFlow(TdlibParams params) : params_(std::move(params)) {}

  AuthStep on_state(const json& authorization_state);
  AuthStep provide(const std::string& answer);
  AuthStep on_error(const json& error);

  AuthPrompt pending() const { return pending_; }
  const std::string& state_type() const { return state_type_; }

 private:
  AuthStep prompt_for_state(const std::string& prefix_info);

  TdlibParams params_;
  json state_;
  std::string state_type_;
  AuthPrompt pending_ = AuthPrompt::None;
  std::string first_name_;
};

}  // namespace mc1h
