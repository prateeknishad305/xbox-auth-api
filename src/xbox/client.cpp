#include "xbox/client.h"

#include <thread>

#include "core/log.h"

namespace xbox::api {

XboxClient::XboxClient(auth::AuthService& auth, core::HttpClient http)
    : auth_(auth), http_(std::move(http)) {}

bool XboxClient::fetch(const std::string& url,
                       const std::vector<std::pair<std::string, std::string>>& headers,
                       core::Json& payload, ApiError& error, bool allow_retry) {
  auth::AuthError auth_error;
  if (!auth_.ensure_session(auth_error)) {
    error.status = auth_error.status;
    error.code = auth_error.code;
    error.message = auth_error.message;
    return false;
  }

  std::string authorization = auth_.authorization_header();
  if (authorization.empty()) {
    error.status = 401;
    error.code = "not_authenticated";
    error.message = "No Xbox Live session is available. Sign in first.";
    return false;
  }

  core::HttpRequest request;
  request.method = "GET";
  request.url = url;
  request.headers = headers;
  request.headers.push_back({"Authorization", authorization});
  request.headers.push_back({"Accept", "application/json"});
  request.headers.push_back({"Accept-Language", "en-US"});
  request.timeout_ms = auth_.config().http_timeout_ms;
  request.connect_timeout_ms = auth_.config().http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.error.empty()) {
    error.status = 502;
    error.code = "network_error";
    error.message = "Xbox Live request failed: " + response.error;
    return false;
  }

  if (response.status == 401 && allow_retry) {
    auth::AuthError renew_error;
    if (auth_.renew(renew_error)) {
      return fetch(url, headers, payload, error, false);
    }
    error.status = renew_error.status;
    error.code = renew_error.code;
    error.message = renew_error.message;
    return false;
  }

  if (response.status == 429) {
    error.status = 429;
    error.code = "rate_limited";
    error.message = "Xbox Live rate limited the request. Try again later.";
    error.details = core::json_parse(response.body);
    return false;
  }

  bool parsed = false;
  core::Json body = core::json_parse(response.body, &parsed);
  if (!response.ok) {
    error.status = static_cast<int>(response.status);
    error.code = "xbox_request_failed";
    error.message = "Xbox Live request failed (" + std::to_string(response.status) + ")";
    error.details = parsed ? body : core::Json::object();
    return false;
  }
  if (!parsed) {
    error.status = 502;
    error.code = "invalid_response";
    error.message = "Xbox Live returned an unreadable payload.";
    return false;
  }

  payload = body;
  return true;
}

bool XboxClient::get(const std::string& url,
                     const std::vector<std::pair<std::string, std::string>>& headers,
                     core::Json& payload, ApiError& error) {
  return fetch(url, headers, payload, error, true);
}

}  // namespace xbox::api
