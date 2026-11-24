#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace xbox::core {

struct HttpRequest {
  std::string method = "GET";
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  std::string content_type = "application/json";
  std::string cookie_jar;
  int timeout_ms = 15000;
  int connect_timeout_ms = 8000;
  bool follow_redirects = true;
  bool verbose = false;
};

struct HttpResponse {
  long status = 0;
  std::string body;
  std::map<std::string, std::string> headers;
  std::vector<std::pair<std::string, std::string>> header_entries;
  std::string error;
  std::string effective_url;
  bool ok = false;

  bool json_ok() const { return status >= 200 && status < 300; }
};

class HttpClient {
 public:
  HttpClient();
  HttpClient(const HttpClient&) = default;
  HttpClient& operator=(const HttpClient&) = default;
  HttpClient(HttpClient&&) = default;
  HttpClient& operator=(HttpClient&&) = default;

  HttpResponse request(const HttpRequest& request) const;
  HttpResponse get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& headers = {}) const;
  HttpResponse post_json(const std::string& url, const std::string& json_body,
                         const std::vector<std::pair<std::string, std::string>>& headers = {}) const;
  HttpResponse post_form(const std::string& url, const std::vector<std::pair<std::string, std::string>>& fields,
                         const std::vector<std::pair<std::string, std::string>>& headers = {}) const;

 private:
  HttpResponse perform(const HttpRequest& request) const;
};

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& fields);

}  // namespace xbox::core
