#pragma once

#include <cstdint>
#include <string>

#include "nlohmann/json.hpp"

namespace mc1h {

using json = nlohmann::json;

// Typed accessors that never throw. TDLib's JSON encodes int53 as a number
// and int64 as a string, so int-like getters accept both.
inline std::string jstr(const json& j, const char* key, const std::string& fallback = {}) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return fallback;
  return it->get<std::string>();
}

inline std::int64_t jint(const json& j, const char* key, std::int64_t fallback = 0) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end()) return fallback;
  if (it->is_number_integer()) return it->get<std::int64_t>();
  if (it->is_number_unsigned()) return static_cast<std::int64_t>(it->get<std::uint64_t>());
  if (it->is_string()) {
    const std::string& s = it->get_ref<const std::string&>();
    if (s.empty()) return fallback;
    std::size_t pos = 0;
    try {
      long long v = std::stoll(s, &pos, 10);
      if (pos == s.size()) return v;
    } catch (...) {
    }
  }
  return fallback;
}

inline bool jbool(const json& j, const char* key, bool fallback = false) {
  if (!j.is_object()) return fallback;
  auto it = j.find(key);
  if (it == j.end() || !it->is_boolean()) return fallback;
  return it->get<bool>();
}

inline const json& jobj(const json& j, const char* key) {
  static const json kNull;
  if (!j.is_object()) return kNull;
  auto it = j.find(key);
  return it == j.end() ? kNull : *it;
}

inline std::string jtype(const json& j) { return jstr(j, "@type"); }

}  // namespace mc1h
