#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace xbox::core {

using Json = nlohmann::json;

std::string json_string(const Json& node, const std::string& key, const std::string& fallback = "");
long long json_int(const Json& node, const std::string& key, long long fallback = 0);
double json_double(const Json& node, const std::string& key, double fallback = 0.0);
bool json_bool(const Json& node, const std::string& key, bool fallback = false);
Json json_object(const Json& node, const std::string& key);
Json json_array(const Json& node, const std::string& key);
bool json_has(const Json& node, const std::string& key);
Json json_parse(const std::string& text, bool* ok = nullptr);
std::string json_dump(const Json& node);

}  // namespace xbox::core
