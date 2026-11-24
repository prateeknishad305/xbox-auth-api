#include "core/json.h"

namespace xbox::core {

std::string json_string(const Json& node, const std::string& key, const std::string& fallback) {
  if (!node.is_object()) return fallback;
  auto it = node.find(key);
  if (it == node.end() || it->is_null()) return fallback;
  if (it->is_string()) return it->get<std::string>();
  if (it->is_number()) return it->dump();
  if (it->is_boolean()) return it->get<bool>() ? "true" : "false";
  return fallback;
}

long long json_int(const Json& node, const std::string& key, long long fallback) {
  if (!node.is_object()) return fallback;
  auto it = node.find(key);
  if (it == node.end() || it->is_null()) return fallback;
  if (it->is_number_integer()) return it->get<long long>();
  if (it->is_number_float()) return static_cast<long long>(it->get<double>());
  if (it->is_string()) {
    try {
      return std::stoll(it->get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  if (it->is_boolean()) return it->get<bool>() ? 1 : 0;
  return fallback;
}

double json_double(const Json& node, const std::string& key, double fallback) {
  if (!node.is_object()) return fallback;
  auto it = node.find(key);
  if (it == node.end() || it->is_null()) return fallback;
  if (it->is_number()) return it->get<double>();
  if (it->is_string()) {
    try {
      return std::stod(it->get<std::string>());
    } catch (...) {
      return fallback;
    }
  }
  return fallback;
}

bool json_bool(const Json& node, const std::string& key, bool fallback) {
  if (!node.is_object()) return fallback;
  auto it = node.find(key);
  if (it == node.end() || it->is_null()) return fallback;
  if (it->is_boolean()) return it->get<bool>();
  if (it->is_number()) return it->get<double>() != 0.0;
  if (it->is_string()) {
    std::string text = it->get<std::string>();
    std::string lowered;
    for (char character : text) lowered.push_back(static_cast<char>(std::tolower(character)));
    return lowered == "true" || lowered == "1";
  }
  return fallback;
}

Json json_object(const Json& node, const std::string& key) {
  if (!node.is_object()) return Json::object();
  auto it = node.find(key);
  if (it == node.end() || !it->is_object()) return Json::object();
  return *it;
}

Json json_array(const Json& node, const std::string& key) {
  if (!node.is_object()) return Json::array();
  auto it = node.find(key);
  if (it == node.end() || !it->is_array()) return Json::array();
  return *it;
}

bool json_has(const Json& node, const std::string& key) {
  if (!node.is_object()) return false;
  auto it = node.find(key);
  return it != node.end() && !it->is_null();
}

Json json_parse(const std::string& text, bool* ok) {
  if (ok != nullptr) *ok = false;
  if (text.empty()) return Json::object();
  Json parsed = Json::parse(text, nullptr, false);
  if (parsed.is_discarded()) return Json::object();
  if (ok != nullptr) *ok = true;
  return parsed;
}

std::string json_dump(const Json& node) {
  return node.dump();
}

}  // namespace xbox::core
