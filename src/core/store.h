#pragma once

#include <mutex>
#include <string>

#include "core/json.h"

namespace xbox::core {

struct TokenBundle {
  std::string access_token;
  std::string refresh_token;
  std::string xbl_token;
  std::string xsts_token;
  std::string user_hash;
  std::string xuid;
  std::string gamertag;
  std::string ms_client_id;
  std::string ms_scope;
  std::string ms_token_url;
  long long ms_expires_at = 0;
  long long xbl_expires_at = 0;
  long long xsts_expires_at = 0;
  std::string updated_at;

  bool has_session() const { return !xbl_token.empty() && !xsts_token.empty(); }
  bool ms_valid() const;
  bool xbl_valid() const;
  bool xsts_valid() const;
  bool session_valid() const;
};

class TokenStore {
 public:
  explicit TokenStore(std::string path);
  TokenStore(const TokenStore&) = delete;
  TokenStore& operator=(const TokenStore&) = delete;
  TokenStore(TokenStore&&) = delete;
  TokenStore& operator=(TokenStore&&) = delete;

  bool load(TokenBundle& bundle);
  bool save(const TokenBundle& bundle);
  bool clear();
  bool exists() const;
  const std::string& path() const { return path_; }

 private:
  bool write_locked(const TokenBundle& bundle);
  std::string path_;
  mutable std::mutex mutex_;
};

}  // namespace xbox::core
