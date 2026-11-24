#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/http.h"
#include "core/json.h"
#include "xbox/auth.h"

namespace xbox::api {

struct ApiError {
  int status = 500;
  std::string code = "api_error";
  std::string message;
  core::Json details = core::Json::object();
};

class XboxClient {
 public:
  explicit XboxClient(auth::AuthService& auth, core::HttpClient http);

  bool get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers,
           core::Json& payload, ApiError& error);
  const auth::AuthService& auth_service() const { return auth_; }

 private:
  bool fetch(const std::string& url,
             const std::vector<std::pair<std::string, std::string>>& headers, core::Json& payload,
             ApiError& error, bool allow_retry);

  auth::AuthService& auth_;
  core::HttpClient http_;
};

}  // namespace xbox::api
