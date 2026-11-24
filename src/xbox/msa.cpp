#include "xbox/msa.h"

#include <algorithm>
#include <cctype>
#include <regex>
#include <utility>

#include "core/cookies.h"
#include "core/crypto.h"
#include "core/json.h"
#include "core/log.h"

namespace xbox::msa {
namespace {

const char kBrowserAgent[] =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/156.0.0.0 Safari/537.36";
const int kMaxRedirects = 6;

std::string build_query(const std::vector<std::pair<std::string, std::string>>& params) {
  std::string out;
  for (std::size_t index = 0; index < params.size(); ++index) {
    if (index > 0) out.push_back('&');
    out.append(core::url_encode(params[index].first));
    out.push_back('=');
    out.append(core::url_encode(params[index].second));
  }
  return out;
}

std::string to_lower(const std::string& text) {
  std::string lowered = text;
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return lowered;
}

std::string resolve_location(const std::string& current_url, const std::string& location) {
  if (location.empty()) return location;
  if (location.find("://") != std::string::npos) return location;

  std::size_t scheme_end = current_url.find("://");
  if (scheme_end == std::string::npos) return location;
  std::string origin = current_url.substr(0, scheme_end + 3);
  std::string rest = current_url.substr(scheme_end + 3);
  std::size_t path_start = rest.find('/');
  std::string host = path_start == std::string::npos ? rest : rest.substr(0, path_start);
  std::string path = path_start == std::string::npos ? "/" : rest.substr(path_start);

  if (location[0] == '/') return origin + host + location;

  std::size_t last_slash = path.find_last_of('/');
  std::string directory = last_slash == std::string::npos ? "/" : path.substr(0, last_slash + 1);
  return origin + host + directory + location;
}

std::string header_value(const core::HttpResponse& response, const std::string& name) {
  for (const auto& entry : response.header_entries) {
    if (entry.first == name) return entry.second;
  }
  auto it = response.headers.find(name);
  return it == response.headers.end() ? std::string() : it->second;
}

std::string regex_group(const std::string& text, const std::string& pattern, int group) {
  try {
    std::regex expression(pattern, std::regex::icase);
    std::smatch match;
    if (std::regex_search(text, match, expression) && match.size() > static_cast<std::size_t>(group)) {
      return match[group].str();
    }
  } catch (const std::regex_error&) {
  }
  return std::string();
}

std::string extract_ppft(const std::string& html) {
  std::string value = regex_group(html, "name=\\\\?\"PPFT\\\\?\"[^>]*value=\\\\?\"([^\"\\\\]+)\\\\?\"", 1);
  if (!value.empty()) return value;
  return regex_group(html, "\"PPFT\"\\s*:\\s*\"([^\"]+)\"", 1);
}

std::string extract_url_post(const std::string& html) {
  std::string value = regex_group(html, "urlPost\"?\\s*:\\s*\"([^\"]+)\"", 1);
  if (!value.empty()) return value;
  return regex_group(html, "urlPost'\\s*:\\s*'([^']+)'", 1);
}

std::string extract_error_text(const std::string& html) {
  std::string value = regex_group(html, "\"sErrTxt\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"", 1);
  if (!value.empty()) return value;
  return regex_group(html, "\"sErrTxt\"\\s*:\\s*'([^']*)'", 1);
}

std::string extract_confirm_action(const std::string& html) {
  std::string action = regex_group(html, "<form[^>]*action=\"([^\"]*identity/confirm[^\"]*)\"", 1);
  if (action.empty()) {
    action = regex_group(html, "<form[^>]*action='([^']*identity/confirm[^']*)'", 1);
  }
  if (action.empty()) return std::string();
  if (action.find("://") == std::string::npos) action = "https://account.live.com" + action;
  return action;
}

std::vector<std::pair<std::string, std::string>> extract_form_fields(const std::string& html) {
  std::vector<std::pair<std::string, std::string>> fields;
  std::size_t position = 0;
  while (position < html.size()) {
    std::size_t input = html.find("<input", position);
    if (input == std::string::npos) break;
    std::size_t end = html.find('>', input);
    if (end == std::string::npos) break;
    std::string tag = html.substr(input, end - input);
    position = end + 1;
    std::string name = regex_group(tag, "name=\"([^\"]*)\"", 1);
    if (name.empty()) name = regex_group(tag, "name='([^']*)'", 1);
    if (name.empty()) continue;
    std::string value = regex_group(tag, "value=\"([^\"]*)\"", 1);
    if (value.empty()) value = regex_group(tag, "value='([^']*)'", 1);
    fields.emplace_back(name, value);
  }
  return fields;
}

std::string strip_html(const std::string& text) {
  std::string out;
  bool inside_tag = false;
  for (char character : text) {
    if (character == '<') {
      inside_tag = true;
      continue;
    }
    if (character == '>') {
      inside_tag = false;
      continue;
    }
    if (!inside_tag) out.push_back(character);
  }
  std::string decoded;
  decoded.reserve(out.size());
  for (char character : out) decoded.push_back(character);
  std::string collapsed;
  bool space = false;
  for (char character : decoded) {
    if (character == ' ' || character == '\t' || character == '\n' || character == '\r') {
      space = true;
      continue;
    }
    if (space && !collapsed.empty()) collapsed.push_back(' ');
    space = false;
    collapsed.push_back(character);
  }
  return collapsed;
}

std::string unescape_json_string(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] != '\\' || index + 1 >= text.size()) {
      out.push_back(text[index]);
      continue;
    }
    char next = text[index + 1];
    switch (next) {
      case 'n':
        out.push_back('\n');
        ++index;
        break;
      case 't':
        out.push_back('\t');
        ++index;
        break;
      case 'r':
        out.push_back('\r');
        ++index;
        break;
      case 'b':
        out.push_back('\b');
        ++index;
        break;
      case 'f':
        out.push_back('\f');
        ++index;
        break;
      case 'u': {
        if (index + 5 < text.size()) {
          unsigned int code = 0;
          try {
            code = static_cast<unsigned int>(std::stoul(text.substr(index + 2, 4), nullptr, 16));
          } catch (...) {
            code = 0;
          }
          if (code < 0x80) {
            out.push_back(static_cast<char>(code));
          } else if (code < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          } else {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
          }
          index += 5;
        } else {
          ++index;
        }
        break;
      }
      default:
        out.push_back(next);
        ++index;
        break;
    }
  }
  return out;
}

std::vector<std::string> extract_proof_channels(const std::string& html) {
  std::vector<std::string> channels;
  std::size_t marker = html.find("\"rawProofList\":\"");
  if (marker == std::string::npos) return channels;

  std::size_t start = marker + 16;
  std::string raw;
  for (std::size_t index = start; index < html.size(); ++index) {
    if (html[index] == '\\' && index + 1 < html.size()) {
      raw.push_back(html[index]);
      raw.push_back(html[index + 1]);
      ++index;
      continue;
    }
    if (html[index] == '"') break;
    raw.push_back(html[index]);
  }

  std::string decoded = unescape_json_string(raw);
  bool parsed = false;
  core::Json list = core::json_parse(decoded, &parsed);
  if (!parsed || !list.is_array()) return channels;
  for (const core::Json& item : list) {
    if (!item.is_object()) continue;
    std::string channel = core::json_string(item, "channel");
    if (channel.empty()) continue;
    if (std::find(channels.begin(), channels.end(), channel) == channels.end()) {
      channels.push_back(channel);
    }
  }
  return channels;
}

std::string query_value(const std::string& url, const std::string& key) {
  std::size_t start = url.find('?');
  if (start == std::string::npos) return std::string();
  std::string query = url.substr(start + 1);
  std::size_t position = 0;
  while (position <= query.size()) {
    std::size_t separator = query.find('&', position);
    if (separator == std::string::npos) separator = query.size();
    std::string pair = query.substr(position, separator - position);
    position = separator + 1;
    std::size_t equals = pair.find('=');
    if (equals == std::string::npos) continue;
    if (pair.substr(0, equals) == key) return core::url_decode(pair.substr(equals + 1));
  }
  return std::string();
}

}  // namespace

bool parse_token_fragment(const std::string& fragment, std::string& access_token,
                          std::string& refresh_token, long long& expires_in) {
  std::string payload = fragment;
  std::size_t hash = payload.find('#');
  if (hash != std::string::npos) payload = payload.substr(hash + 1);
  if (payload.find("access_token") == std::string::npos) return false;

  std::size_t position = 0;
  while (position <= payload.size()) {
    std::size_t separator = payload.find('&', position);
    if (separator == std::string::npos) separator = payload.size();
    std::string pair = payload.substr(position, separator - position);
    position = separator + 1;
    std::size_t equals = pair.find('=');
    if (equals == std::string::npos) continue;
    std::string name = pair.substr(0, equals);
    std::string value = core::url_decode(pair.substr(equals + 1));
    if (name == "access_token") access_token = value;
    else if (name == "refresh_token") refresh_token = value;
    else if (name == "expires_in") {
      try {
        expires_in = std::stoll(value);
      } catch (...) {
        expires_in = 0;
      }
    }
  }
  return !access_token.empty();
}

AccountClient::AccountClient(core::HttpClient http, core::Config config)
    : http_(std::move(http)), config_(std::move(config)) {}

SignInResult AccountClient::sign_in(const std::string& email, const std::string& password) const {
  SignInResult result;

  std::vector<std::pair<std::string, std::string>> params = {
      {"client_id", config_.password_client_id},
      {"redirect_uri", config_.password_redirect_uri},
      {"response_type", "token"},
      {"scope", config_.password_scope},
  };
  std::string authorize_url = config_.password_authorize_url + "?" + build_query(params);
  std::string url = authorize_url;

  std::string jar;
  core::HttpResponse response;
  for (int hop = 0; hop < kMaxRedirects; ++hop) {
    core::HttpRequest request;
    request.method = "GET";
    request.url = url;
    request.follow_redirects = false;
    request.cookie_jar = jar;
    request.headers.push_back({"User-Agent", kBrowserAgent});
    request.headers.push_back({"Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"});
    request.timeout_ms = config_.auth_request_timeout_ms;
    request.connect_timeout_ms = config_.http_connect_timeout_ms;

    response = http_.request(request);
    if (!response.error.empty()) {
      result.error_code = "network_error";
      result.error_message = "Microsoft sign-in endpoint unreachable: " + response.error;
      result.status = 502;
      return result;
    }

    jar = core::merge_set_cookies(jar, response);

    std::string location = header_value(response, "location");
    if (response.status >= 300 && response.status < 400 && !location.empty()) {
      url = resolve_location(url, location);
      continue;
    }
    break;
  }

  if (response.status >= 300 && response.status < 400) {
    result.error_code = "authorization_failed";
    result.error_message = "Microsoft sign-in returned too many redirects.";
    result.status = 502;
    return result;
  }

  std::string ppft = extract_ppft(response.body);
  std::string url_post = extract_url_post(response.body);
  if (ppft.empty() || url_post.empty()) {
    result.error_code = "pre_auth_failed";
    result.error_message = "Could not read the Microsoft sign-in form parameters.";
    result.status = 502;
    return result;
  }

  core::HttpRequest post;
  post.method = "POST";
  post.url = url_post;
  post.content_type = "application/x-www-form-urlencoded";
  post.body = core::form_encode({{"login", email}, {"loginfmt", email}, {"passwd", password},
                                  {"PPFT", ppft}});
  post.cookie_jar = jar;
  post.follow_redirects = false;
  post.headers.push_back({"User-Agent", kBrowserAgent});
  post.headers.push_back({"Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8"});
  post.timeout_ms = config_.auth_request_timeout_ms;
  post.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse signed_in = http_.request(post);
  if (!signed_in.error.empty()) {
    result.error_code = "network_error";
    result.error_message = "Microsoft sign-in endpoint unreachable: " + signed_in.error;
    result.status = 502;
    return result;
  }

  std::string location = header_value(signed_in, "location");
  if (signed_in.status >= 300 && signed_in.status < 400 && !location.empty()) {
    std::string access_token;
    std::string refresh_token;
    long long expires_in = 0;
    if (parse_token_fragment(location, access_token, refresh_token, expires_in)) {
      result.completed = true;
      result.access_token = access_token;
      result.refresh_token = refresh_token;
      result.expires_in = expires_in > 0 ? expires_in : 3600;
      result.status = signed_in.status;
      return result;
    }
    std::string code = query_value(location, "code");
    if (!code.empty()) {
      result.authorization_code = code;
      result.completed = true;
      result.status = signed_in.status;
      return result;
    }
    result.error_code = "authorization_failed";
    result.error_message = "Microsoft sign-in did not return a token.";
    result.status = 401;
    return result;
  }

  std::string body = signed_in.body;
  std::string lowered = to_lower(body);

  if (signed_in.status == 429) {
    result.throttled = true;
    result.error_code = "login_throttled";
    result.error_message = "Microsoft throttled this sign-in method. Wait before trying again.";
    result.status = 429;
    return result;
  }

  if (signed_in.status >= 400) {
    std::string message;
    bool parsed = false;
    core::Json payload = core::json_parse(body, &parsed);
    if (parsed && payload.is_object()) {
      message = core::json_string(payload, "error_description");
      if (message.empty()) message = core::json_string(payload, "error");
    }
    if (message.empty()) message = strip_html(body);
    std::string lowered_message = to_lower(message);
    if (lowered_message.find("password") != std::string::npos ||
        lowered_message.find("account") != std::string::npos ||
        lowered_message.find("invalid_request") != std::string::npos ||
        lowered_message.find("invalid_grant") != std::string::npos) {
      result.invalid_credentials = true;
      result.error_code = "invalid_credentials";
      result.error_message = message.empty() ? "The Microsoft account password was rejected."
                                             : message;
      result.status = 401;
      return result;
    }
    result.error_code = "login_failed";
    result.error_message = message.empty() ? "Microsoft sign-in failed." : message;
    result.status = 401;
    return result;
  }

  std::string confirm_action = extract_confirm_action(body);
  if (!confirm_action.empty()) {
    result.verification_required = true;
    result.verification_url = authorize_url;

    std::string channels_source = body;
    std::vector<std::pair<std::string, std::string>> fields = extract_form_fields(body);
    if (!fields.empty()) {
      core::HttpRequest confirm;
      confirm.method = "POST";
      confirm.url = confirm_action;
      confirm.content_type = "application/x-www-form-urlencoded";
      confirm.body = core::form_encode(fields);
      confirm.cookie_jar = jar;
      confirm.follow_redirects = false;
      confirm.headers.push_back({"User-Agent", kBrowserAgent});
      confirm.headers.push_back({"Accept", "text/html,application/xhtml+xml,*/*;q=0.8"});
      confirm.timeout_ms = config_.auth_request_timeout_ms;
      confirm.connect_timeout_ms = config_.http_connect_timeout_ms;
      core::HttpResponse confirm_response = http_.request(confirm);
      if (confirm_response.error.empty() && confirm_response.status == 200 &&
          !confirm_response.body.empty()) {
        channels_source = confirm_response.body;
      }
    }

    result.verification_channels = extract_proof_channels(channels_source);
    result.verification_message =
        "Microsoft requires an additional verification step for this sign-in. Open the sign-in URL "
        "in your browser, finish the verification, then copy the fragment after '#' from the final "
        "redirect and send it to POST /api/auth/fragment.";
    result.error_code = "verification_required";
    result.error_message = result.verification_message;
    result.status = 409;
    return result;
  }

  if (lowered.find("you've reached your limit") != std::string::npos ||
      lowered.find("reached your limit with this sign-in method") != std::string::npos ||
      lowered.find("too many attempts") != std::string::npos) {
    result.throttled = true;
    result.error_code = "login_throttled";
    result.error_message = "Microsoft throttled this sign-in method. Wait before trying again.";
    result.status = 429;
    return result;
  }

  std::string error_text = strip_html(extract_error_text(body));
  std::string lowered_error = to_lower(error_text);
  const char* credential_markers[] = {
      "password is incorrect", "account or password is incorrect", "your password is incorrect",
      "incorrect password", "can't find an account", "cannot find an account",
      "doesn't exist", "does not exist", "we can't find that account", "that account doesn't exist",
  };
  bool credential_failure = false;
  for (const char* marker : credential_markers) {
    if (lowered_error.find(marker) != std::string::npos ||
        lowered.find(marker) != std::string::npos) {
      credential_failure = true;
      break;
    }
  }
  if (credential_failure || lowered_error.find("password") != std::string::npos) {
    result.invalid_credentials = true;
    result.error_code = "invalid_credentials";
    result.error_message = error_text.empty() ? "The Microsoft account password was rejected."
                                               : error_text;
    result.status = 401;
    return result;
  }

  result.error_code = "login_failed";
  result.error_message = error_text.empty() ? "Microsoft sign-in failed." : error_text;
  result.status = 401;
  return result;
}

}  // namespace xbox::msa
