#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/config.h"
#include "core/http.h"
#include "core/store.h"

namespace xbox::auth {

struct AuthError {
  std::string code = "auth_error";
  std::string message;
  int status = 401;
  std::string verification_url;
  std::string verification_message;
  std::vector<std::string> verification_channels;
};

struct AuthorizationRequest {
  std::string authorization_url;
  std::string state;
  int expires_in = 600;
};

struct SessionInfo {
  std::string xuid;
  std::string gamertag;
  std::string user_hash;
  bool authenticated = false;
  bool ms_valid = false;
  bool xbl_valid = false;
  bool xsts_valid = false;
};

class AuthService {
 public:
  AuthService(core::Config config, core::TokenStore& store, core::HttpClient http);
  AuthService(const AuthService&) = delete;
  AuthService& operator=(const AuthService&) = delete;

  AuthorizationRequest start_authorization(AuthError& error);
  AuthorizationRequest start_authorization(const std::string& login_hint, AuthError& error);
  bool complete_authorization(const std::string& state, const std::string& code, AuthError& error);
  bool complete_authorization_any(const std::string& code, AuthError& error);
  bool login_with_cookie(const std::string& cookie_header, AuthError& error);
  bool login_with_password(const std::string& email, const std::string& password, AuthError& error);
  bool login_with_fragment(const std::string& fragment, AuthError& error);
  bool import_refresh_token(const std::string& refresh_token, AuthError& error);
  bool import_refresh_token(const std::string& refresh_token, const std::string& client_id,
                            const std::string& scope, const std::string& token_url,
                            AuthError& error);
  bool ensure_session(AuthError& error);
  bool renew(AuthError& error);
  bool logout();

  bool authenticated() const;
  SessionInfo session_info() const;
  std::string authorization_header() const;
  std::string xuid() const;
  core::TokenBundle tokens() const;
  const core::Config& config() const { return config_; }

 private:
  struct PendingFlow {
    std::string verifier;
    long long expires_at = 0;
  };

  struct MsProfile {
    std::string client_id;
    std::string scope;
    std::string authorize_url;
    std::string token_url;
    std::string redirect_uri;
    bool send_scope = true;
  };

  MsProfile active_profile() const;
  MsProfile cookie_profile() const;
  bool exchange_code_for_tokens(const std::string& code, const std::string& verifier,
                                const MsProfile& profile, AuthError& error);
  bool exchange_code_for_tokens(const std::string& code, const MsProfile& profile, AuthError& error);
  bool fetch_authorization_code(const std::string& cookie_header, const MsProfile& profile,
                                std::string& code, AuthError& error);
  std::string query_value(const std::string& url, const std::string& key) const;
  std::string normalize_cookie_header(const std::string& raw) const;
  bool refresh_ms_token(AuthError& error);
  bool request_ms_refresh(const std::string& refresh_token, const std::string& client_id,
                          const std::string& scope, const std::string& token_url, core::Json& payload,
                          AuthError& error);
  bool store_ms_tokens(const std::string& access_token, const std::string& refresh_token,
                       long long expires_in, const std::string& client_id, const std::string& scope,
                       const std::string& token_url, AuthError& error);
  bool exchange_xbl_token(AuthError& error);
  bool exchange_xsts_token(AuthError& error);
  bool establish_session(AuthError& error);
  bool apply_ms_token_response(const core::Json& payload, AuthError& error);
  void set_ms_profile(const std::string& client_id, const std::string& scope,
                      const std::string& token_url);
  bool load_tokens();
  bool persist();
  void prune_flows();

  core::Config config_;
  core::TokenStore& store_;
  core::HttpClient http_;
  mutable std::mutex mutex_;
  core::TokenBundle tokens_;
  std::unordered_map<std::string, PendingFlow> pending_;
};

}  // namespace xbox::auth
