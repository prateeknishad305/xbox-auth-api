#include "core/config.h"

#include <cstdlib>
#include <string>

namespace xbox::core {
namespace {

const char kDefaultClientId[] = "1950a258-227b-4e31-a9cf-717495945fc2";
const char kDefaultAuthorizeUrl[] =
    "https://login.microsoftonline.com/consumers/oauth2/v2.0/authorize";
const char kDefaultTokenUrl[] = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
const char kLegacyClientId[] = "00000000402b5328";
const char kLegacyAuthorizeUrl[] = "https://login.live.com/oauth20_authorize.srf";
const char kLegacyTokenUrl[] = "https://login.live.com/oauth20_token.srf";
const char kLegacyRedirectUri[] = "https://login.live.com/oauth20_desktop.srf";
const char kLegacyScope[] = "service::user.auth.xboxlive.com::MBI_SSL";
const char kPasswordClientId[] = "000000004C12AE6F";
const char kPasswordScope[] = "service::user.auth.xboxlive.com::MBI_SSL";
const char kPasswordAuthorizeUrl[] = "https://login.live.com/oauth20_authorize.srf";
const char kPasswordTokenUrl[] = "https://login.live.com/oauth20_token.srf";
const char kPasswordRedirectUri[] = "https://login.live.com/oauth20_desktop.srf";

}  // namespace

std::string env_or(const char* name, const std::string& fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr) return fallback;
  std::string value(raw);
  while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')) {
    value.pop_back();
  }
  if (value.empty()) return fallback;
  return value;
}

long long env_number(const char* name, long long fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr) return fallback;
  try {
    return std::stoll(std::string(raw));
  } catch (...) {
    return fallback;
  }
}

bool env_flag(const char* name, bool fallback) {
  const char* raw = std::getenv(name);
  if (raw == nullptr) return fallback;
  std::string value(raw);
  for (char& character : value) {
    character = static_cast<char>(std::tolower(character));
  }
  if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
  if (value == "0" || value == "false" || value == "no" || value == "off") return false;
  return fallback;
}

Config load_config() {
  Config config;
  config.host = env_or("XBOX_HOST", config.host);
  config.port = static_cast<int>(env_number("PORT", config.port));
  config.threads = static_cast<int>(env_number("XBOX_THREADS", config.threads));
  config.legacy_authorize = env_flag("XBOX_LEGACY_AUTHORIZE", false);
  config.client_id = env_or(
      "XBOX_CLIENT_ID", config.legacy_authorize ? kLegacyClientId : kDefaultClientId);
  config.client_secret = env_or("XBOX_CLIENT_SECRET", "");
  config.scope = env_or("XBOX_SCOPE", config.legacy_authorize ? kLegacyScope : config.scope);
  config.authorize_url = env_or(
      "XBOX_AUTHORIZE_URL", config.legacy_authorize ? kLegacyAuthorizeUrl : kDefaultAuthorizeUrl);
  config.token_url = env_or(
      "XBOX_TOKEN_URL", config.legacy_authorize ? kLegacyTokenUrl : kDefaultTokenUrl);
  config.pkce = env_flag("XBOX_PKCE", !config.legacy_authorize);
  config.password_login_enabled = env_flag("XBOX_PASSWORD_LOGIN", config.password_login_enabled);
  config.password_client_id = env_or("XBOX_PASSWORD_CLIENT_ID", kPasswordClientId);
  config.password_scope = env_or("XBOX_PASSWORD_SCOPE", kPasswordScope);
  config.password_authorize_url = env_or("XBOX_PASSWORD_AUTHORIZE_URL", kPasswordAuthorizeUrl);
  config.password_token_url = env_or("XBOX_PASSWORD_TOKEN_URL", kPasswordTokenUrl);
  config.password_redirect_uri = env_or("XBOX_PASSWORD_REDIRECT_URI", kPasswordRedirectUri);
  config.cookie_client_id = env_or("XBOX_COOKIE_CLIENT_ID", kLegacyClientId);
  config.cookie_scope = env_or("XBOX_COOKIE_SCOPE", kLegacyScope);
  config.cookie_authorize_url = env_or("XBOX_COOKIE_AUTHORIZE_URL", kLegacyAuthorizeUrl);
  config.cookie_token_url = env_or("XBOX_COOKIE_TOKEN_URL", kLegacyTokenUrl);
  config.cookie_redirect_uri = env_or("XBOX_COOKIE_REDIRECT_URI", kLegacyRedirectUri);
  config.token_file = env_or("TOKEN_FILE", config.token_file);
  config.redirect_uri = env_or(
      "XBOX_REDIRECT_URI",
      config.legacy_authorize ? std::string(kLegacyRedirectUri)
                               : "http://localhost:" + std::to_string(config.port) + "/");
  config.open_browser = env_flag("XBOX_OPEN_BROWSER", false);
  config.http_timeout_ms = static_cast<int>(env_number("XBOX_HTTP_TIMEOUT_MS", config.http_timeout_ms));
  config.http_connect_timeout_ms =
      static_cast<int>(env_number("XBOX_HTTP_CONNECT_TIMEOUT_MS", config.http_connect_timeout_ms));
  config.http_retries = static_cast<int>(env_number("XBOX_HTTP_RETRIES", config.http_retries));
  config.auth_request_timeout_ms =
      static_cast<int>(env_number("XBOX_AUTH_TIMEOUT_MS", config.auth_request_timeout_ms));
  config.log_level = env_or("XBOX_LOG_LEVEL", config.log_level);
  return config;
}

}  // namespace xbox::core
