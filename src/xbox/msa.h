#pragma once

#include <string>
#include <vector>

#include "core/config.h"
#include "core/http.h"

namespace xbox::msa {

struct SignInResult {
  bool completed = false;
  bool verification_required = false;
  bool invalid_credentials = false;
  bool throttled = false;
  std::string access_token;
  std::string refresh_token;
  std::string authorization_code;
  long long expires_in = 0;
  std::string verification_url;
  std::string verification_message;
  std::vector<std::string> verification_channels;
  std::string error_code;
  std::string error_message;
  long status = 0;
};

bool parse_token_fragment(const std::string& fragment, std::string& access_token,
                          std::string& refresh_token, long long& expires_in);

class AccountClient {
 public:
  AccountClient(core::HttpClient http, core::Config config);

  SignInResult sign_in(const std::string& email, const std::string& password) const;

 private:
  core::HttpClient http_;
  core::Config config_;
};

}  // namespace xbox::msa
