#pragma once

#include <string>

#include "core/http.h"

namespace xbox::core {

std::string merge_set_cookies(const std::string& jar, const HttpResponse& response);
std::string cookie_jar_header(const std::string& jar);

}  // namespace xbox::core
