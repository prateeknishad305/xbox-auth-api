#pragma once

#include <cstddef>
#include <string>

namespace xbox::core {

struct Config {
  std::string host = "0.0.0.0";
  int port = 3001;
  int threads = 4;
  std::string client_id;
  std::string client_secret;
  std::string redirect_uri;
  std::string scope = "XboxLive.signin XboxLive.offline_access offline_access";
  std::string authorize_url;
  std::string token_url;
  bool legacy_authorize = false;
  bool pkce = true;
  bool password_login_enabled = true;
  std::string password_client_id;
  std::string password_scope;
  std::string password_authorize_url;
  std::string password_token_url;
  std::string password_redirect_uri;
  std::string cookie_client_id;
  std::string cookie_scope;
  std::string cookie_authorize_url;
  std::string cookie_token_url;
  std::string cookie_redirect_uri;
  std::string token_file = "data/tokens.json";
  std::string refresh_token_env = "XBOX_REFRESH_TOKEN";
  bool open_browser = false;
  int http_timeout_ms = 15000;
  int http_connect_timeout_ms = 8000;
  int http_retries = 2;
  int auth_request_timeout_ms = 12000;
  std::string log_level = "info";
};

Config load_config();
std::string env_or(const char* name, const std::string& fallback);
long long env_number(const char* name, long long fallback);
bool env_flag(const char* name, bool fallback);

}  // namespace xbox::core
