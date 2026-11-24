#pragma once

#include <string>

namespace xbox::core {

enum class LogLevel { kDebug = 0, kInfo = 1, kWarn = 2, kError = 3 };

void set_log_level(LogLevel level);
LogLevel log_level();
void log_message(LogLevel level, const std::string& message);
std::string redact(const std::string& text);

}  // namespace xbox::core
