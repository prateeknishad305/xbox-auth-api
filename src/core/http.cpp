#include "core/http.h"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <mutex>
#include <thread>

#include "core/crypto.h"
#include "core/log.h"

namespace xbox::core {
namespace {

std::once_flag& init_flag() {
  static std::once_flag flag;
  return flag;
}

void ensure_curl_init() {
  std::call_once(init_flag(), []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

struct WriteContext {
  std::string data;
  bool overflow = false;
};

size_t write_body(char* pointer, size_t size, size_t count, void* userdata) {
  WriteContext* context = static_cast<WriteContext*>(userdata);
  std::size_t total = size * count;
  context->data.append(pointer, total);
  return total;
}

size_t write_header(char* pointer, size_t size, size_t count, void* userdata) {
  WriteContext* context = static_cast<WriteContext*>(userdata);
  std::size_t total = size * count;
  std::string line(pointer, total);
  std::size_t separator = line.find(':');
  if (separator != std::string::npos) {
    std::string key = line.substr(0, separator);
    std::string value = line.substr(separator + 1);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(value.begin());
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) value.pop_back();
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    context->data.append(key + "\n" + value + "\n");
  }
  return total;
}

}  // namespace

std::string form_encode(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string out;
  for (std::size_t index = 0; index < fields.size(); ++index) {
    if (index > 0) out.push_back('&');
    out.append(url_encode(fields[index].first));
    out.push_back('=');
    out.append(url_encode(fields[index].second));
  }
  return out;
}

HttpClient::HttpClient() {
  ensure_curl_init();
}

HttpResponse HttpClient::perform(const HttpRequest& request) const {
  HttpResponse response;
  CURL* handle = curl_easy_init();
  if (handle == nullptr) {
    response.error = "curl_easy_init failed";
    return response;
  }

  curl_slist* header_list = nullptr;
  bool has_accept = false;
  bool has_user_agent = false;
  for (const auto& header : request.headers) {
    header_list = curl_slist_append(header_list, (header.first + ": " + header.second).c_str());
    std::string key = header.first;
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char character) {
      return static_cast<char>(std::tolower(character));
    });
    if (key == "accept") has_accept = true;
    if (key == "user-agent") has_user_agent = true;
  }
  if (!request.cookie_jar.empty()) {
    header_list = curl_slist_append(header_list, ("Cookie: " + request.cookie_jar).c_str());
  }
  if (!request.body.empty() || request.method == "POST") {
    header_list = curl_slist_append(header_list, ("Content-Type: " + request.content_type).c_str());
  }
  if (!has_accept) {
    header_list = curl_slist_append(header_list, "Accept: application/json");
  }
  if (!has_user_agent) {
    header_list = curl_slist_append(header_list,
                                     "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) XboxLiveApiCpp/1.0");
  }

  WriteContext body_context;
  WriteContext header_context;

  curl_easy_setopt(handle, CURLOPT_URL, request.url.c_str());
  curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_body);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &body_context);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, write_header);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &header_context);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, static_cast<long>(request.timeout_ms));
  curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request.connect_timeout_ms));
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, request.follow_redirects ? 1L : 0L);
  curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
  curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
  curl_easy_setopt(handle, CURLOPT_VERBOSE, request.verbose ? 1L : 0L);

  if (request.method == "POST") {
    curl_easy_setopt(handle, CURLOPT_POST, 1L);
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
  } else if (request.method != "GET") {
    curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    if (!request.body.empty()) {
      curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.body.c_str());
      curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
    }
  }

  CURLcode result = curl_easy_perform(handle);
  if (result == CURLE_OK) {
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    response.status = status;
    response.ok = status >= 200 && status < 300;
    char* effective = nullptr;
    curl_easy_getinfo(handle, CURLINFO_EFFECTIVE_URL, &effective);
    if (effective != nullptr) response.effective_url = effective;
  } else {
    response.error = curl_easy_strerror(result);
  }

  response.body = body_context.data;
  std::size_t position = 0;
  while (position < header_context.data.size()) {
    std::size_t newline = header_context.data.find('\n', position);
    if (newline == std::string::npos) break;
    std::string key = header_context.data.substr(position, newline - position);
    position = newline + 1;
    newline = header_context.data.find('\n', position);
    if (newline == std::string::npos) break;
    std::string value = header_context.data.substr(position, newline - position);
    position = newline + 1;
    if (!key.empty()) {
      response.headers[key] = value;
      response.header_entries.emplace_back(key, value);
    }
  }

  if (header_list != nullptr) curl_slist_free_all(header_list);
  curl_easy_cleanup(handle);
  return response;
}

HttpResponse HttpClient::request(const HttpRequest& request) const {
  HttpResponse response = perform(request);
  return response;
}

HttpResponse HttpClient::get(const std::string& url,
                             const std::vector<std::pair<std::string, std::string>>& headers) const {
  HttpRequest request;
  request.method = "GET";
  request.url = url;
  request.headers = headers;
  return perform(request);
}

HttpResponse HttpClient::post_json(const std::string& url, const std::string& json_body,
                                   const std::vector<std::pair<std::string, std::string>>& headers) const {
  HttpRequest request;
  request.method = "POST";
  request.url = url;
  request.headers = headers;
  request.body = json_body;
  request.content_type = "application/json";
  return perform(request);
}

HttpResponse HttpClient::post_form(const std::string& url,
                                   const std::vector<std::pair<std::string, std::string>>& fields,
                                   const std::vector<std::pair<std::string, std::string>>& headers) const {
  HttpRequest request;
  request.method = "POST";
  request.url = url;
  request.headers = headers;
  request.body = form_encode(fields);
  request.content_type = "application/x-www-form-urlencoded";
  return perform(request);
}

}  // namespace xbox::core
