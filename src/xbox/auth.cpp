#include "xbox/auth.h"

#include <algorithm>
#include <chrono>
#include <utility>

#include "core/crypto.h"
#include "core/json.h"
#include "core/log.h"
#include "xbox/msa.h"

namespace xbox::auth {
namespace {

core::Json xui_claims(const core::Json& payload) {
  core::Json display = core::json_object(payload, "DisplayClaims");
  if (!display.is_object()) return core::Json::object();
  core::Json xui = display.value("xui", core::Json::object());
  if (xui.is_array()) {
    if (xui.empty()) return core::Json::object();
    return xui[0];
  }
  if (!xui.is_object()) return core::Json::object();
  return xui;
}

}  // namespace

namespace {

const char kXblUrl[] = "https://user.auth.xboxlive.com/user/authenticate";
const char kXstsUrl[] = "https://xsts.auth.xboxlive.com/xsts/authorize";
const char kProfileUrl[] =
    "https://profile.xboxlive.com/users/me/profile/settings?settings=Gamertag";
const long long kXblDefaultLifetimeSeconds = 86400;
const long long kXstsDefaultLifetimeSeconds = 7200;

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

std::string ms_error_code(const core::Json& payload, long status) {
  std::string code = core::json_string(payload, "error");
  if (!code.empty()) return code;
  return "http_" + std::to_string(status);
}

std::string ms_error_message(const core::Json& payload, long status, const std::string& fallback) {
  std::string message = core::json_string(payload, "error_description");
  if (!message.empty()) return message;
  message = core::json_string(payload, "error");
  if (!message.empty()) return message;
  return fallback + " (" + std::to_string(status) + ")";
}

}  // namespace

AuthService::AuthService(core::Config config, core::TokenStore& store, core::HttpClient http)
    : config_(std::move(config)), store_(store), http_(std::move(http)) {
  load_tokens();
}

bool AuthService::load_tokens() {
  std::lock_guard<std::mutex> guard(mutex_);
  core::TokenBundle loaded;
  if (store_.load(loaded)) {
    tokens_ = loaded;
    return true;
  }
  return false;
}

bool AuthService::persist() {
  std::lock_guard<std::mutex> guard(mutex_);
  tokens_.updated_at = core::now_epoch_seconds();
  return store_.save(tokens_);
}

void AuthService::prune_flows() {
  std::lock_guard<std::mutex> guard(mutex_);
  long long now = core::now_epoch_millis();
  for (auto it = pending_.begin(); it != pending_.end();) {
    if (it->second.expires_at <= now) {
      it = pending_.erase(it);
    } else {
      ++it;
    }
  }
}

AuthorizationRequest AuthService::start_authorization(AuthError& error) {
  return start_authorization(std::string(), error);
}

AuthorizationRequest AuthService::start_authorization(const std::string& login_hint, AuthError& error) {
  (void)error;
  prune_flows();
  AuthorizationRequest request;
  std::string verifier;
  std::string challenge;
  if (config_.pkce) {
    verifier = core::random_base64url(64);
    challenge = core::sha256_base64url(verifier);
  }
  std::string state = core::random_base64url(24);

  {
    std::lock_guard<std::mutex> guard(mutex_);
    PendingFlow flow;
    flow.verifier = verifier;
    flow.expires_at = core::now_epoch_millis() + 600000;
    pending_[state] = flow;
  }

  std::vector<std::pair<std::string, std::string>> params = {
      {"client_id", config_.client_id},
      {"response_type", "code"},
      {"redirect_uri", config_.redirect_uri},
      {"scope", config_.scope},
      {"state", state},
  };
  if (!challenge.empty()) {
    params.emplace_back("code_challenge", challenge);
    params.emplace_back("code_challenge_method", "S256");
  }
  if (config_.legacy_authorize) {
    params.emplace_back("op", "auth");
    params.emplace_back("ver", "1.0");
  }
  if (!login_hint.empty()) params.emplace_back("login_hint", login_hint);
  request.authorization_url = config_.authorize_url + "?" + build_query(params);
  request.state = state;
  request.expires_in = 600;
  return request;
}

bool AuthService::complete_authorization(const std::string& state, const std::string& code,
                                         AuthError& error) {
  if (code.empty()) {
    error.code = "missing_code";
    error.message = "Authorization code is missing from the callback.";
    error.status = 400;
    return false;
  }
  std::string verifier;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it = pending_.find(state);
    if (it == pending_.end()) {
      error.code = "unknown_state";
      error.message = "Unknown or expired authorization state. Start a new login.";
      error.status = 400;
      return false;
    }
    if (it->second.expires_at <= core::now_epoch_millis()) {
      pending_.erase(it);
      error.code = "state_expired";
      error.message = "Authorization state expired. Start a new login.";
      error.status = 400;
      return false;
    }
    verifier = it->second.verifier;
    pending_.erase(it);
  }

  if (!exchange_code_for_tokens(code, verifier, active_profile(), error)) return false;
  return establish_session(error);
}

bool AuthService::complete_authorization_any(const std::string& code, AuthError& error) {
  if (code.empty()) {
    error.code = "missing_code";
    error.message = "Authorization code is missing.";
    error.status = 400;
    return false;
  }
  std::string state_value;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    long long newest_expiry = 0;
    for (const auto& entry : pending_) {
      if (entry.second.expires_at <= core::now_epoch_millis()) continue;
      if (entry.second.expires_at > newest_expiry) {
        newest_expiry = entry.second.expires_at;
        state_value = entry.first;
      }
    }
  }
  if (state_value.empty()) {
    error.code = "unknown_state";
    error.message = "No pending login. Start one with POST /api/auth/login first.";
    error.status = 400;
    return false;
  }
  return complete_authorization(state_value, code, error);
}

AuthService::MsProfile AuthService::active_profile() const {
  MsProfile profile;
  profile.client_id = config_.client_id;
  profile.scope = config_.scope;
  profile.authorize_url = config_.authorize_url;
  profile.token_url = config_.token_url;
  profile.redirect_uri = config_.redirect_uri;
  profile.send_scope = true;
  return profile;
}

AuthService::MsProfile AuthService::cookie_profile() const {
  MsProfile profile;
  profile.client_id = config_.cookie_client_id;
  profile.scope = config_.cookie_scope;
  profile.authorize_url = config_.cookie_authorize_url;
  profile.token_url = config_.cookie_token_url;
  profile.redirect_uri = config_.cookie_redirect_uri;
  profile.send_scope = true;
  return profile;
}

bool AuthService::exchange_code_for_tokens(const std::string& code, const std::string& verifier,
                                           const MsProfile& profile, AuthError& error) {
  std::vector<std::pair<std::string, std::string>> fields = {
      {"grant_type", "authorization_code"},
      {"client_id", profile.client_id},
      {"code", code},
      {"redirect_uri", profile.redirect_uri},
  };
  if (!verifier.empty()) fields.emplace_back("code_verifier", verifier);
  if (profile.send_scope && !profile.scope.empty()) fields.emplace_back("scope", profile.scope);
  if (!config_.client_secret.empty()) fields.emplace_back("client_secret", config_.client_secret);

  core::HttpRequest request;
  request.method = "POST";
  request.url = profile.token_url;
  request.content_type = "application/x-www-form-urlencoded";
  request.body = core::form_encode(fields);
  request.timeout_ms = config_.auth_request_timeout_ms;
  request.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.error.empty()) {
    error.code = "network_error";
    error.message = "Microsoft token endpoint unreachable: " + response.error;
    error.status = 502;
    return false;
  }
  bool parsed = false;
  core::Json payload = core::json_parse(response.body, &parsed);
  if (!response.ok) {
    error.code = ms_error_code(payload, response.status);
    if (error.code == "invalid_grant" || error.code == "interaction_required" ||
        error.code == "consent_required") {
      error.code = "reauth_required";
    }
    error.message = ms_error_message(payload, response.status, "Token exchange failed");
    error.status = 400;
    return false;
  }
  if (!parsed) {
    error.code = "invalid_response";
    error.message = "Microsoft token endpoint returned an unreadable payload.";
    error.status = 502;
    return false;
  }
  set_ms_profile(profile.client_id, profile.scope, profile.token_url);
  return apply_ms_token_response(payload, error);
}

bool AuthService::exchange_code_for_tokens(const std::string& code, const MsProfile& profile,
                                           AuthError& error) {
  return exchange_code_for_tokens(code, std::string(), profile, error);
}

std::string AuthService::query_value(const std::string& url, const std::string& key) const {
  std::size_t start = url.find('?');
  if (start == std::string::npos) return std::string();
  std::string query = url.substr(start + 1);
  std::size_t position = 0;
  while (position <= query.size()) {
    std::size_t separator = query.find('&', position);
    if (separator == std::string::npos) separator = query.size();
    std::string pair = query.substr(position, separator - position);
    std::size_t equals = pair.find('=');
    if (equals != std::string::npos) {
      std::string name = pair.substr(0, equals);
      if (name == key) return core::url_decode(pair.substr(equals + 1));
    }
    position = separator + 1;
  }
  return std::string();
}

bool AuthService::fetch_authorization_code(const std::string& cookie_header, const MsProfile& profile,
                                           std::string& code, AuthError& error) {
  std::vector<std::pair<std::string, std::string>> params = {
      {"client_id", profile.client_id},
      {"response_type", "code"},
      {"redirect_uri", profile.redirect_uri},
      {"scope", profile.scope},
  };

  core::HttpRequest request;
  request.method = "GET";
  request.url = profile.authorize_url + "?" + build_query(params);
  request.headers.push_back({"Cookie", cookie_header});
  request.headers.push_back({"User-Agent",
                             "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                             "(KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36"});
  request.follow_redirects = false;
  request.timeout_ms = config_.auth_request_timeout_ms;
  request.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.error.empty()) {
    error.code = "network_error";
    error.message = "Microsoft authorize endpoint unreachable: " + response.error;
    error.status = 502;
    return false;
  }

  const std::string& final_url = response.effective_url;
  code = query_value(final_url, "code");
  if (code.empty()) {
    for (const auto& entry : response.header_entries) {
      if (entry.first != "location") continue;
      std::string candidate = query_value(entry.second, "code");
      if (!candidate.empty()) {
        code = candidate;
        break;
      }
    }
  }
  if (code.empty()) {
    std::string failure = query_value(final_url, "error");
    if (failure.empty()) failure = query_value(final_url, "error_description");
    error.code = failure.empty() ? "authorization_failed" : failure;
    error.message = failure.empty()
                        ? "The Microsoft account cookie was rejected. Sign in again and export a fresh cookie."
                        : failure;
    error.status = 401;
    return false;
  }
  return true;
}

std::string AuthService::normalize_cookie_header(const std::string& raw) const {
  std::string trimmed = raw;
  while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r' || trimmed.back() == ' ')) {
    trimmed.pop_back();
  }
  if (trimmed.find('\t') != std::string::npos || trimmed.find('\n') != std::string::npos) {
    std::size_t position = 0;
    std::string host_cookie;
    std::vector<std::string> pairs;
    while (position <= trimmed.size()) {
      std::size_t newline = trimmed.find('\n', position);
      if (newline == std::string::npos) newline = trimmed.size();
      std::string line = trimmed.substr(position, newline - position);
      position = newline + 1;
      if (line.empty() || line[0] == '#') continue;
      std::vector<std::string> columns;
      std::size_t column_start = 0;
      while (true) {
        std::size_t tab = line.find('\t', column_start);
        if (tab == std::string::npos) {
          columns.push_back(line.substr(column_start));
          break;
        }
        columns.push_back(line.substr(column_start, tab - column_start));
        column_start = tab + 1;
      }
      if (columns.size() < 7) continue;
      std::string name = columns[5];
      std::string value = columns[6];
      if (name == "__Host-MSAAUTHP") host_cookie = name + "=" + value;
      else if (!name.empty()) pairs.push_back(name + "=" + value);
    }
    if (!host_cookie.empty()) return host_cookie;
    if (!pairs.empty()) {
      std::string joined;
      for (std::size_t index = 0; index < pairs.size(); ++index) {
        if (index > 0) joined.append("; ");
        joined.append(pairs[index]);
      }
      return joined;
    }
  }
  return trimmed;
}

bool AuthService::login_with_cookie(const std::string& cookie_header, AuthError& error) {
  std::string cookie = normalize_cookie_header(cookie_header);
  if (cookie.empty()) {
    error.code = "missing_cookie";
    error.message = "A Microsoft account cookie is required.";
    error.status = 400;
    return false;
  }
  std::string code;
  MsProfile profile = cookie_profile();
  if (!fetch_authorization_code(cookie, profile, code, error)) return false;
  if (!exchange_code_for_tokens(code, profile, error)) return false;
  return establish_session(error);
}

bool AuthService::store_ms_tokens(const std::string& access_token, const std::string& refresh_token,                                  long long expires_in, const std::string& client_id,
                                  const std::string& scope, const std::string& token_url,
                                  AuthError& error) {
  if (access_token.empty()) {
    error.code = "invalid_response";
    error.message = "Microsoft did not return an access token.";
    error.status = 502;
    return false;
  }
  {
    std::lock_guard<std::mutex> guard(mutex_);
    tokens_.access_token = access_token;
    if (!refresh_token.empty()) tokens_.refresh_token = refresh_token;
    tokens_.ms_client_id = client_id;
    tokens_.ms_scope = scope;
    tokens_.ms_token_url = token_url;
    tokens_.ms_expires_at = core::now_epoch_millis() + (expires_in > 0 ? expires_in : 3600) * 1000;
    tokens_.xbl_expires_at = 0;
    tokens_.xsts_expires_at = 0;
  }
  if (!persist()) {
    error.code = "store_failed";
    error.message = "Could not write the token store.";
    error.status = 500;
    return false;
  }
  return true;
}

bool AuthService::login_with_password(const std::string& email, const std::string& password,
                                      AuthError& error) {
  if (!config_.password_login_enabled) {
    error.code = "password_login_disabled";
    error.message = "Password sign-in is disabled. Set XBOX_PASSWORD_LOGIN=true to enable it.";
    error.status = 403;
    return false;
  }
  if (email.empty() || password.empty()) {
    error.code = "missing_credentials";
    error.message = "Send {\"email\": \"...\", \"password\": \"...\"} in the request body.";
    error.status = 400;
    return false;
  }

  msa::AccountClient account_client(http_, config_);
  msa::SignInResult result = account_client.sign_in(email, password);

  if (result.verification_required) {
    error.code = "verification_required";
    error.message = result.verification_message;
    error.status = 409;
    error.verification_url = result.verification_url;
    error.verification_message = result.verification_message;
    error.verification_channels = result.verification_channels;
    return false;
  }
  if (!result.completed) {
    error.code = result.error_code.empty() ? "login_failed" : result.error_code;
    error.message = result.error_message.empty() ? "Microsoft sign-in failed." : result.error_message;
    error.status = result.status != 0 ? static_cast<int>(result.status) : 401;
    return false;
  }

  if (!result.authorization_code.empty()) {
    MsProfile profile = active_profile();
    if (!exchange_code_for_tokens(result.authorization_code, profile, error)) return false;
    return establish_session(error);
  }

  if (!store_ms_tokens(result.access_token, result.refresh_token, result.expires_in,
                       config_.password_client_id, config_.password_scope,
                       config_.password_token_url, error)) {
    return false;
  }
  return establish_session(error);
}

bool AuthService::login_with_fragment(const std::string& fragment, AuthError& error) {
  std::string access_token;
  std::string refresh_token;
  long long expires_in = 0;
  if (!msa::parse_token_fragment(fragment, access_token, refresh_token, expires_in)) {
    error.code = "missing_fragment";
    error.message =
        "Send the browser redirect fragment as {\"fragment\": \"access_token=...&refresh_token=...\"}.";
    error.status = 400;
    return false;
  }
  if (!store_ms_tokens(access_token, refresh_token, expires_in, config_.password_client_id,
                       config_.password_scope, config_.password_token_url, error)) {
    return false;
  }
  return establish_session(error);
}

bool AuthService::apply_ms_token_response(const core::Json& payload, AuthError& error) {
  std::string access_token = core::json_string(payload, "access_token");
  if (access_token.empty()) {
    error.code = "invalid_response";
    error.message = "Microsoft token response did not contain an access token.";
    error.status = 502;
    return false;
  }
  long long expires_in = core::json_int(payload, "expires_in", 3600);
  {
    std::lock_guard<std::mutex> guard(mutex_);
    tokens_.access_token = access_token;
    std::string refresh_token = core::json_string(payload, "refresh_token");
    if (!refresh_token.empty()) tokens_.refresh_token = refresh_token;
    tokens_.ms_expires_at = core::now_epoch_millis() + expires_in * 1000;
    tokens_.xbl_expires_at = 0;
    tokens_.xsts_expires_at = 0;
  }
  return persist();
}

void AuthService::set_ms_profile(const std::string& client_id, const std::string& scope,
                                 const std::string& token_url) {
  std::lock_guard<std::mutex> guard(mutex_);
  tokens_.ms_client_id = client_id;
  tokens_.ms_scope = scope;
  tokens_.ms_token_url = token_url;
}
bool AuthService::import_refresh_token(const std::string& refresh_token, AuthError& error) {
  return import_refresh_token(refresh_token, std::string(), std::string(), std::string(), error);
}

bool AuthService::import_refresh_token(const std::string& refresh_token, const std::string& client_id,
                                       const std::string& scope, const std::string& token_url,
                                       AuthError& error) {
  if (refresh_token.empty()) {
    error.code = "missing_refresh_token";
    error.message = "A Microsoft refresh token is required for import.";
    error.status = 400;
    return false;
  }
  std::string target_client = client_id.empty() ? config_.client_id : client_id;
  std::string target_scope = scope.empty() ? config_.scope : scope;
  std::string target_token_url = token_url.empty() ? config_.token_url : token_url;

  core::Json payload;
  if (!request_ms_refresh(refresh_token, target_client, target_scope, target_token_url, payload,
                          error)) {
    return false;
  }

  {
    std::lock_guard<std::mutex> guard(mutex_);
    tokens_.refresh_token = refresh_token;
    tokens_.ms_client_id = target_client;
    tokens_.ms_scope = target_scope;
    tokens_.ms_token_url = target_token_url;
  }
  if (!apply_ms_token_response(payload, error)) return false;
  return establish_session(error);
}

bool AuthService::refresh_ms_token(AuthError& error) {
  std::string refresh_token;
  std::string client_id;
  std::string scope;
  std::string token_url;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    refresh_token = tokens_.refresh_token;
    client_id = tokens_.ms_client_id.empty() ? config_.client_id : tokens_.ms_client_id;
    scope = tokens_.ms_scope.empty() ? config_.scope : tokens_.ms_scope;
    token_url = tokens_.ms_token_url.empty() ? config_.token_url : tokens_.ms_token_url;
  }
  if (refresh_token.empty()) {
    error.code = "not_authenticated";
    error.message = "No Microsoft refresh token stored. Run the login flow or import a token.";
    error.status = 401;
    return false;
  }

  core::Json payload;
  if (!request_ms_refresh(refresh_token, client_id, scope, token_url, payload, error)) return false;
  return apply_ms_token_response(payload, error);
}

bool AuthService::request_ms_refresh(const std::string& refresh_token, const std::string& client_id,
                                     const std::string& scope, const std::string& token_url,
                                     core::Json& payload, AuthError& error) {
  std::vector<std::pair<std::string, std::string>> fields = {
      {"grant_type", "refresh_token"},
      {"client_id", client_id},
      {"refresh_token", refresh_token},
      {"scope", scope},
  };
  if (!config_.client_secret.empty()) fields.emplace_back("client_secret", config_.client_secret);

  core::HttpRequest request;
  request.method = "POST";
  request.url = token_url;
  request.content_type = "application/x-www-form-urlencoded";
  request.body = core::form_encode(fields);
  request.timeout_ms = config_.auth_request_timeout_ms;
  request.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.error.empty()) {
    error.code = "network_error";
    error.message = "Microsoft token endpoint unreachable: " + response.error;
    error.status = 502;
    return false;
  }
  payload = core::json_parse(response.body);
  if (!response.ok) {
    std::string code = ms_error_code(payload, response.status);
    if (code == "invalid_grant") {
      error.code = "reauth_required";
      error.message = "The Microsoft refresh token was rejected. Sign in again.";
      error.status = 401;
      return false;
    }
    error.code = code;
    error.message = ms_error_message(payload, response.status, "Token refresh failed");
    error.status = 400;
    return false;
  }
  return true;
}

bool AuthService::exchange_xbl_token(AuthError& error) {
  std::string access_token;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    access_token = tokens_.access_token;
  }
  if (access_token.empty()) {
    error.code = "not_authenticated";
    error.message = "No Microsoft access token available.";
    error.status = 401;
    return false;
  }

  std::vector<std::string> tickets;
  tickets.push_back(access_token);
  std::string prefixed = "d=" + access_token;
  if (prefixed != access_token) tickets.push_back(prefixed);

  core::Json last_payload = core::Json::object();
  long last_status = 0;
  for (std::size_t attempt = 0; attempt < tickets.size(); ++attempt) {
    core::Json properties = core::Json::object();
    properties["AuthMethod"] = "RPS";
    properties["SiteName"] = "user.auth.xboxlive.com";
    properties["RpsTicket"] = tickets[attempt];

    core::Json body = core::Json::object();
    body["RelyingParty"] = "http://auth.xboxlive.com";
    body["TokenType"] = "JWT";
    body["Properties"] = properties;

    core::HttpRequest request;
    request.method = "POST";
    request.url = kXblUrl;
    request.content_type = "application/json";
    request.body = body.dump();
    request.headers.push_back({"x-xbl-contract-version", "1"});
    request.timeout_ms = config_.auth_request_timeout_ms;
    request.connect_timeout_ms = config_.http_connect_timeout_ms;

    core::HttpResponse response = http_.request(request);
    if (!response.error.empty()) {
      error.code = "network_error";
      error.message = "Xbox Live authentication endpoint unreachable: " + response.error;
      error.status = 502;
      return false;
    }
    core::Json payload = core::json_parse(response.body);
    last_payload = payload;
    last_status = response.status;
    if (!response.ok) continue;

    std::string token = core::json_string(payload, "Token");
    if (token.empty()) {
      error.code = "xbl_invalid_response";
      error.message = "Xbox Live user authentication returned no token.";
      error.status = 502;
      return false;
    }
    core::Json claims = xui_claims(payload);
    std::string xuid = core::json_string(claims, "xid");
    long long expires_in = core::json_int(payload, "ExpiresIn", kXblDefaultLifetimeSeconds);
    {
      std::lock_guard<std::mutex> guard(mutex_);
      tokens_.xbl_token = token;
      tokens_.xbl_expires_at = core::now_epoch_millis() + expires_in * 1000;
      if (!xuid.empty()) tokens_.xuid = xuid;
    }
    return persist();
  }

  error.code = "xbl_auth_failed";
  error.message = "Xbox Live user authentication failed (" + std::to_string(last_status) + ")";
  error.status = last_status >= 400 && last_status < 600 ? static_cast<int>(last_status) : 502;
  if (last_payload.is_object() && !last_payload.empty()) {
    std::string reason = core::json_string(last_payload, "Reason");
    if (!reason.empty()) error.message += ": " + reason;
  }
  return false;
}

bool AuthService::exchange_xsts_token(AuthError& error) {
  std::string xbl_token;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    xbl_token = tokens_.xbl_token;
  }
  if (xbl_token.empty()) {
    error.code = "not_authenticated";
    error.message = "No Xbox Live user token available.";
    error.status = 401;
    return false;
  }

  core::Json properties = core::Json::object();
  properties["SandboxId"] = "RETAIL";
  properties["UserTokens"] = core::Json::array({xbl_token});

  core::Json body = core::Json::object();
  body["RelyingParty"] = "http://xboxlive.com";
  body["TokenType"] = "JWT";
  body["Properties"] = properties;

  core::HttpRequest request;
  request.method = "POST";
  request.url = kXstsUrl;
  request.content_type = "application/json";
  request.body = body.dump();
  request.headers.push_back({"x-xbl-contract-version", "1"});
  request.timeout_ms = config_.auth_request_timeout_ms;
  request.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.error.empty()) {
    error.code = "network_error";
    error.message = "Xbox Live security token endpoint unreachable: " + response.error;
    error.status = 502;
    return false;
  }
  core::Json payload = core::json_parse(response.body);
  if (!response.ok) {
    long xerr = 0;
    auto header = response.headers.find("xerr");
    if (header != response.headers.end()) {
      try {
        xerr = std::stol(header->second);
      } catch (...) {
        xerr = 0;
      }
    }
    if (xerr == 2148916233) {
      error.code = "no_xbox_profile";
      error.message = "This Microsoft account has no Xbox Live profile.";
      error.status = 403;
      return false;
    }
    if (xerr == 2148916238) {
      error.code = "child_account_restricted";
      error.message = "This child account is not allowed to sign in to Xbox Live.";
      error.status = 403;
      return false;
    }
    if (xerr == 2148916235 || xerr == 2148916236) {
      error.code = "account_region_restricted";
      error.message = "Xbox Live denied this account for its country or region.";
      error.status = 403;
      return false;
    }
    error.code = "xsts_auth_failed";
    error.message = "Xbox Live security token exchange failed (" + std::to_string(response.status) + ")";
    error.status = response.status >= 400 && response.status < 600 ? static_cast<int>(response.status) : 502;
    return false;
  }

  core::Json claims = xui_claims(payload);
  std::string user_hash = core::json_string(claims, "uhs");
  if (user_hash.empty()) {
    error.code = "xsts_invalid_response";
    error.message = "Xbox Live security token response did not contain a user hash.";
    error.status = 502;
    return false;
  }
  std::string xuid = core::json_string(claims, "xid");
  std::string token = core::json_string(payload, "Token");
  long long expires_in = core::json_int(payload, "ExpiresIn", 0);
  if (expires_in <= 0) {
    expires_in = kXstsDefaultLifetimeSeconds;
  }
  {
    std::lock_guard<std::mutex> guard(mutex_);
    tokens_.xsts_token = token;
    tokens_.user_hash = user_hash;
    tokens_.xsts_expires_at = core::now_epoch_millis() + expires_in * 1000;
    if (!xuid.empty()) tokens_.xuid = xuid;
  }
  return persist();
}

bool AuthService::renew(AuthError& error) {
  if (!refresh_ms_token(error)) return false;
  return establish_session(error);
}

bool AuthService::establish_session(AuthError& error) {
  if (!exchange_xbl_token(error)) return false;
  if (!exchange_xsts_token(error)) return false;

  std::string xuid;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    xuid = tokens_.xuid;
  }
  if (xuid.empty()) {
    error.code = "no_xuid";
    error.message = "Could not determine the XUID for this account.";
    error.status = 502;
    return false;
  }

  core::HttpRequest request;
  request.method = "GET";
  request.url = kProfileUrl;
  request.headers.push_back({"x-xbl-contract-version", "3"});
  request.headers.push_back({"Authorization", authorization_header()});
  request.timeout_ms = config_.http_timeout_ms;
  request.connect_timeout_ms = config_.http_connect_timeout_ms;

  core::HttpResponse response = http_.request(request);
  if (!response.ok) {
    error.code = "profile_lookup_failed";
    error.message = "Signed in, but the gamertag lookup failed.";
    error.status = 502;
    return false;
  }

  core::Json payload = core::json_parse(response.body);
  core::Json users = core::json_array(payload, "profileUsers");
  std::string gamertag;
  if (!users.empty() && users[0].is_object()) {
    core::Json settings = core::json_array(users[0], "settings");
    for (const core::Json& setting : settings) {
      if (core::json_string(setting, "id") == "Gamertag") {
        gamertag = core::json_string(setting, "value");
        break;
      }
    }
  }
  if (!gamertag.empty()) {
    {
      std::lock_guard<std::mutex> guard(mutex_);
      tokens_.gamertag = gamertag;
    }
    persist();
  }
  return true;
}

bool AuthService::ensure_session(AuthError& error) {
  bool need_ms = false;
  bool need_xbl = false;
  bool need_xsts = false;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    need_ms = !tokens_.ms_valid();
    need_xbl = !tokens_.xbl_valid();
    need_xsts = !tokens_.xsts_valid();
  }
  if (need_ms && !refresh_ms_token(error)) return false;
  if (need_xbl && !exchange_xbl_token(error)) return false;
  if (need_xsts && !exchange_xsts_token(error)) return false;
  return true;
}

bool AuthService::logout() {
  std::lock_guard<std::mutex> guard(mutex_);
  tokens_ = core::TokenBundle();
  tokens_.updated_at = core::now_epoch_seconds();
  pending_.clear();
  store_.save(tokens_);
  return true;
}

bool AuthService::authenticated() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return tokens_.has_session();
}

SessionInfo AuthService::session_info() const {
  std::lock_guard<std::mutex> guard(mutex_);
  SessionInfo info;
  info.xuid = tokens_.xuid;
  info.gamertag = tokens_.gamertag;
  info.user_hash = tokens_.user_hash;
  info.authenticated = tokens_.has_session();
  info.ms_valid = tokens_.ms_valid();
  info.xbl_valid = tokens_.xbl_valid();
  info.xsts_valid = tokens_.xsts_valid();
  return info;
}

std::string AuthService::authorization_header() const {
  std::lock_guard<std::mutex> guard(mutex_);
  if (tokens_.user_hash.empty() || tokens_.xsts_token.empty()) return std::string();
  return "XBL3.0 x=" + tokens_.user_hash + ";" + tokens_.xsts_token;
}

std::string AuthService::xuid() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return tokens_.xuid;
}

core::TokenBundle AuthService::tokens() const {
  std::lock_guard<std::mutex> guard(mutex_);
  return tokens_;
}

}  // namespace xbox::auth
