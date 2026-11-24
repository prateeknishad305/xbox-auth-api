#include "core/cookies.h"

#include <algorithm>
#include <cctype>
#include <vector>

namespace xbox::core {
namespace {

std::string trim(const std::string& text) {
  std::size_t start = 0;
  std::size_t end = text.size();
  while (start < end && (text[start] == ' ' || text[start] == '\t')) ++start;
  while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                         text[end - 1] == '\r' || text[end - 1] == '\n')) {
    --end;
  }
  return text.substr(start, end - start);
}

std::vector<std::pair<std::string, std::string>> split_pairs(const std::string& jar) {
  std::vector<std::pair<std::string, std::string>> pairs;
  std::size_t position = 0;
  while (position <= jar.size()) {
    std::size_t separator = jar.find(';', position);
    if (separator == std::string::npos) separator = jar.size();
    std::string piece = trim(jar.substr(position, separator - position));
    position = separator + 1;
    if (piece.empty()) continue;
    std::size_t equals = piece.find('=');
    if (equals == std::string::npos) continue;
    pairs.emplace_back(trim(piece.substr(0, equals)), trim(piece.substr(equals + 1)));
  }
  return pairs;
}

bool is_expired(const std::string& set_cookie) {
  std::string lowered = set_cookie;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  if (lowered.find("max-age=0") != std::string::npos) return true;
  std::size_t expires = lowered.find("expires=");
  if (expires == std::string::npos) return false;
  std::size_t comma = lowered.find(',', expires);
  std::string value = lowered.substr(expires + 8, comma == std::string::npos
                                                       ? std::string::npos
                                                       : comma - expires - 8);
  return value.find("1970") != std::string::npos || value.find("01 jan 1970") != std::string::npos;
}

}  // namespace

std::string merge_set_cookies(const std::string& jar, const HttpResponse& response) {
  std::vector<std::pair<std::string, std::string>> pairs = split_pairs(jar);
  for (const auto& entry : response.header_entries) {
    if (entry.first != "set-cookie") continue;
    std::string set_cookie = entry.second;
    std::size_t semicolon = set_cookie.find(';');
    std::string pair = trim(semicolon == std::string::npos ? set_cookie
                                                           : set_cookie.substr(0, semicolon));
    std::size_t equals = pair.find('=');
    if (equals == std::string::npos) continue;
    std::string name = trim(pair.substr(0, equals));
    std::string value = trim(pair.substr(equals + 1));
    if (name.empty()) continue;

    bool replaced = false;
    for (auto& existing : pairs) {
      if (existing.first == name) {
        existing.second = value;
        replaced = true;
        break;
      }
    }
    if (!replaced) {
      if (value.empty() || is_expired(set_cookie)) continue;
      pairs.emplace_back(name, value);
    } else if (value.empty() || is_expired(set_cookie)) {
      pairs.erase(std::remove_if(pairs.begin(), pairs.end(),
                                 [&name](const std::pair<std::string, std::string>& item) {
                                   return item.first == name;
                                 }),
                  pairs.end());
    }
  }

  std::string out;
  for (const auto& pair : pairs) {
    if (!out.empty()) out.append("; ");
    out.append(pair.first).append("=").append(pair.second);
  }
  return out;
}

std::string cookie_jar_header(const std::string& jar) { return jar; }

}  // namespace xbox::core
