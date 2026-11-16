#include "core/auth_flow.h"

#include <cctype>
#include <utility>

namespace mc1h {
namespace {

std::string trim(const std::string& s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return s.substr(b, e - b);
}

AuthStep ask(AuthPrompt kind, std::string text, bool secret, std::string info = {}) {
  AuthStep s;
  s.prompt = kind;
  s.prompt_text = std::move(text);
  s.secret = secret;
  s.info = std::move(info);
  return s;
}

}  // namespace

std::string base64_encode(const std::string& raw) {
  static const char* kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((raw.size() + 2) / 3 * 4);
  std::size_t i = 0;
  for (; i + 2 < raw.size(); i += 3) {
    const auto a = static_cast<unsigned char>(raw[i]);
    const auto b = static_cast<unsigned char>(raw[i + 1]);
    const auto c = static_cast<unsigned char>(raw[i + 2]);
    out += kAlphabet[a >> 2];
    out += kAlphabet[((a & 3) << 4) | (b >> 4)];
    out += kAlphabet[((b & 15) << 2) | (c >> 6)];
    out += kAlphabet[c & 63];
  }
  if (i + 1 == raw.size()) {
    const auto a = static_cast<unsigned char>(raw[i]);
    out += kAlphabet[a >> 2];
    out += kAlphabet[(a & 3) << 4];
    out += "==";
  } else if (i + 2 == raw.size()) {
    const auto a = static_cast<unsigned char>(raw[i]);
    const auto b = static_cast<unsigned char>(raw[i + 1]);
    out += kAlphabet[a >> 2];
    out += kAlphabet[((a & 3) << 4) | (b >> 4)];
    out += kAlphabet[(b & 15) << 2];
    out += '=';
  }
  return out;
}

json make_set_tdlib_parameters(const TdlibParams& p) {
  return json{
      {"@type", "setTdlibParameters"},
      {"use_test_dc", p.use_test_dc},
      {"database_directory", p.database_directory},
      {"files_directory", p.files_directory},
      {"database_encryption_key", base64_encode(p.database_encryption_key)},
      {"use_file_database", p.use_file_database},
      {"use_chat_info_database", p.use_chat_info_database},
      {"use_message_database", p.use_message_database},
      {"use_secret_chats", p.use_secret_chats},
      {"api_id", p.api_id},
      {"api_hash", p.api_hash},
      {"system_language_code", p.system_language_code},
      {"device_model", p.device_model},
      {"system_version", p.system_version},
      {"application_version", p.application_version},
  };
}

AuthStep AuthFlow::on_state(const json& authorization_state) {
  state_ = authorization_state;
  state_type_ = jtype(authorization_state);
  pending_ = AuthPrompt::None;
  return prompt_for_state({});
}

AuthStep AuthFlow::prompt_for_state(const std::string& prefix_info) {
  const std::string& t = state_type_;
  AuthStep s;
  if (t == "authorizationStateWaitTdlibParameters") {
    if (params_.api_id == 0 || params_.api_hash.empty()) {
      s.fatal = true;
      s.info = "api_id/api_hash are not set";
      return s;
    }
    s.request = make_set_tdlib_parameters(params_);
    s.info = prefix_info;
    return s;
  }
  if (t == "authorizationStateWaitPhoneNumber") {
    s = ask(AuthPrompt::Phone, "Phone number (international format): ", false, prefix_info);
  } else if (t == "authorizationStateWaitCode") {
    const json& info = jobj(state_, "code_info");
    std::string where = jtype(jobj(info, "type"));
    s = ask(AuthPrompt::Code, "Login code (" + where + "): ", false, prefix_info);
  } else if (t == "authorizationStateWaitPassword") {
    std::string hint = jstr(state_, "password_hint");
    s = ask(AuthPrompt::Password, hint.empty() ? "2FA password: " : "2FA password (hint: " + hint + "): ", true,
            prefix_info);
  } else if (t == "authorizationStateWaitRegistration") {
    first_name_.clear();
    s = ask(AuthPrompt::FirstName, "New account. First name: ", false, prefix_info);
  } else if (t == "authorizationStateWaitEmailAddress") {
    s = ask(AuthPrompt::Email, "Login email address: ", false, prefix_info);
  } else if (t == "authorizationStateWaitEmailCode") {
    s = ask(AuthPrompt::EmailCode, "Email code: ", false, prefix_info);
  } else if (t == "authorizationStateWaitOtherDeviceConfirmation") {
    s.info = "Confirm this login on another device: " + jstr(state_, "link");
  } else if (t == "authorizationStateWaitPremiumPurchase") {
    s.fatal = true;
    s.info = "Telegram requires a Premium purchase to log in with this number; not supported";
  } else if (t == "authorizationStateReady") {
    s.ready = true;
    s.info = "Authorized";
  } else if (t == "authorizationStateLoggingOut") {
    s.info = "Logging out...";
  } else if (t == "authorizationStateClosing") {
    s.info = "Closing...";
  } else if (t == "authorizationStateClosed") {
    s.closed = true;
  } else {
    s.info = "Unhandled authorization state: " + t;
  }
  pending_ = s.prompt;
  if (!prefix_info.empty() && s.info.empty()) s.info = prefix_info;
  return s;
}

AuthStep AuthFlow::provide(const std::string& raw_answer) {
  const std::string answer = pending_ == AuthPrompt::Password ? raw_answer : trim(raw_answer);
  const AuthPrompt was = pending_;
  if (was == AuthPrompt::None) return {};
  if (answer.empty() && was != AuthPrompt::LastName) return prompt_for_state("Empty input");
  AuthStep s;
  pending_ = AuthPrompt::None;
  switch (was) {
    case AuthPrompt::Phone:
      s.request = json{{"@type", "setAuthenticationPhoneNumber"}, {"phone_number", answer}, {"settings", nullptr}};
      break;
    case AuthPrompt::Code:
      s.request = json{{"@type", "checkAuthenticationCode"}, {"code", answer}};
      break;
    case AuthPrompt::Password:
      s.request = json{{"@type", "checkAuthenticationPassword"}, {"password", answer}};
      break;
    case AuthPrompt::FirstName:
      first_name_ = answer;
      s = ask(AuthPrompt::LastName, "Last name (may be empty): ", false);
      pending_ = s.prompt;
      break;
    case AuthPrompt::LastName:
      s.request = json{{"@type", "registerUser"},
                       {"first_name", first_name_},
                       {"last_name", answer},
                       {"disable_notification", false}};
      break;
    case AuthPrompt::Email:
      s.request = json{{"@type", "setAuthenticationEmailAddress"}, {"email_address", answer}};
      break;
    case AuthPrompt::EmailCode:
      s.request = json{{"@type", "checkAuthenticationEmailCode"},
                       {"code", {{"@type", "emailAddressAuthenticationCode"}, {"code", answer}}}};
      break;
    case AuthPrompt::None:
      break;
  }
  return s;
}

AuthStep AuthFlow::on_error(const json& error) {
  const std::string msg = "Error " + std::to_string(jint(error, "code")) + ": " + jstr(error, "message");
  if (state_type_ == "authorizationStateWaitTdlibParameters") {
    AuthStep s;
    s.fatal = true;
    s.info = "setTdlibParameters failed. " + msg;
    return s;
  }
  // The state does not change after a failed auth request: ask again.
  return prompt_for_state(msg);
}

}  // namespace mc1h
