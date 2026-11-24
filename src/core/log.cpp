#include "core/log.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace xbox::core {
namespace {

std::mutex& log_mutex() {
  static std::mutex mutex;
  return mutex;
}

LogLevel& level_storage() {
  static LogLevel level = LogLevel::kInfo;
  return level;
}

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::kDebug:
      return "debug";
    case LogLevel::kInfo:
      return "info";
    case LogLevel::kWarn:
      return "warn";
    case LogLevel::kError:
      return "error";
  }
  return "info";
}

}  // namespace

void set_log_level(LogLevel level) {
  std::lock_guard<std::mutex> guard(log_mutex());
  level_storage() = level;
}

LogLevel log_level() {
  std::lock_guard<std::mutex> guard(log_mutex());
  return level_storage();
}

std::string redact(const std::string& text) {
  static const char* keys[] = {"access_token", "refresh_token", "RpsTicket", "\"Token\"",
                               "Authorization", "xbl_token",  "xsts_token", "ms_access_token",
                               "ms_refresh_token", "code_verifier", "device_code"};
  std::string out = text;
  for (const char* key : keys) {
    std::string needle = std::string("\"") + key + "\"";
    std::size_t position = 0;
    while ((position = out.find(needle, position)) != std::string::npos) {
      std::size_t colon = out.find(':', position + needle.size());
      if (colon == std::string::npos) break;
      std::size_t start = out.find_first_not_of(" \t", colon + 1);
      if (start == std::string::npos) break;
      std::size_t end = out.find_first_of(",}\n", start);
      if (end == std::string::npos) end = out.size();
      out.replace(start, end - start, "<redacted>");
      position = start + 10;
    }
  }
  return out;
}

void log_message(LogLevel level, const std::string& message) {
  if (static_cast<int>(level) < static_cast<int>(log_level())) return;
  std::lock_guard<std::mutex> guard(log_mutex());
  std::fprintf(stderr, "[%s] %s\n", level_name(level), redact(message).c_str());
  std::fflush(stderr);
}

}  // namespace xbox::core
