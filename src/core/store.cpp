#include "core/store.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

#include "core/crypto.h"
#include "core/log.h"

namespace xbox::core {
namespace {

std::string directory_of(const std::string& path) {
  std::size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

bool ensure_directory(const std::string& directory) {
  std::string current;
  std::size_t start = 0;
  if (!directory.empty() && directory[0] == '/') {
    current = "/";
    start = 1;
  }
  std::stringstream stream(directory.substr(start));
  std::string part;
  while (std::getline(stream, part, '/')) {
    if (part.empty()) continue;
    if (current.empty()) {
      current = part;
    } else if (current == "/") {
      current += part;
    } else {
      current += "/" + part;
    }
    if (::mkdir(current.c_str(), 0700) != 0 && errno != EEXIST) return false;
  }
  return true;
}

}  // namespace

bool TokenBundle::ms_valid() const {
  return !access_token.empty() && ms_expires_at > now_epoch_millis() + 60000;
}

bool TokenBundle::xbl_valid() const {
  return !xbl_token.empty() && xbl_expires_at > now_epoch_millis() + 60000;
}

bool TokenBundle::xsts_valid() const {
  return !xsts_token.empty() && xsts_expires_at > now_epoch_millis() + 60000;
}

bool TokenBundle::session_valid() const {
  return xbl_valid() && xsts_valid();
}

TokenStore::TokenStore(std::string path) : path_(std::move(path)) {}

bool TokenStore::exists() const {
  std::lock_guard<std::mutex> guard(mutex_);
  struct stat info {};
  return ::stat(path_.c_str(), &info) == 0;
}

bool TokenStore::load(TokenBundle& bundle) {
  std::lock_guard<std::mutex> guard(mutex_);
  std::ifstream stream(path_);
  if (!stream.is_open()) return false;
  std::stringstream buffer;
  buffer << stream.rdbuf();
  bool ok = false;
  Json node = json_parse(buffer.str(), &ok);
  if (!ok || !node.is_object()) return false;
  bundle.access_token = json_string(node, "access_token");
  bundle.refresh_token = json_string(node, "refresh_token");
  bundle.xbl_token = json_string(node, "xbl_token");
  bundle.xsts_token = json_string(node, "xsts_token");
  bundle.user_hash = json_string(node, "user_hash");
  bundle.xuid = json_string(node, "xuid");
  bundle.gamertag = json_string(node, "gamertag");
  bundle.ms_client_id = json_string(node, "ms_client_id");
  bundle.ms_scope = json_string(node, "ms_scope");
  bundle.ms_token_url = json_string(node, "ms_token_url");
  bundle.ms_expires_at = json_int(node, "ms_expires_at", 0);
  bundle.xbl_expires_at = json_int(node, "xbl_expires_at", 0);
  bundle.xsts_expires_at = json_int(node, "xsts_expires_at", 0);
  bundle.updated_at = json_string(node, "updated_at");
  return true;
}

bool TokenStore::write_locked(const TokenBundle& bundle) {
  Json node = Json::object();
  node["access_token"] = bundle.access_token;
  node["refresh_token"] = bundle.refresh_token;
  node["xbl_token"] = bundle.xbl_token;
  node["xsts_token"] = bundle.xsts_token;
  node["user_hash"] = bundle.user_hash;
  node["xuid"] = bundle.xuid;
  node["gamertag"] = bundle.gamertag;
  node["ms_client_id"] = bundle.ms_client_id;
  node["ms_scope"] = bundle.ms_scope;
  node["ms_token_url"] = bundle.ms_token_url;
  node["ms_expires_at"] = bundle.ms_expires_at;
  node["xbl_expires_at"] = bundle.xbl_expires_at;
  node["xsts_expires_at"] = bundle.xsts_expires_at;
  node["updated_at"] = bundle.updated_at;

  std::string directory = directory_of(path_);
  if (!ensure_directory(directory)) {
    log_message(LogLevel::kError, "cannot create token directory " + directory);
    return false;
  }

  std::string temporary = path_ + ".tmp." + random_hex(6);
  int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
  if (descriptor < 0) {
    log_message(LogLevel::kError, std::string("cannot open token file: ") + std::strerror(errno));
    return false;
  }
  if (::fchmod(descriptor, 0600) != 0) {
    log_message(LogLevel::kWarn, std::string("cannot chmod token file: ") + std::strerror(errno));
  }

  std::string payload = node.dump(2);
  ssize_t written = ::write(descriptor, payload.data(), payload.size());
  bool ok = written == static_cast<ssize_t>(payload.size());
  if (ok) ok = ::fsync(descriptor) == 0;
  ::close(descriptor);
  if (!ok) {
    log_message(LogLevel::kError, "cannot write token file");
    return false;
  }
  if (::rename(temporary.c_str(), path_.c_str()) != 0) {
    log_message(LogLevel::kError, std::string("cannot replace token file: ") + std::strerror(errno));
    return false;
  }
  return true;
}

bool TokenStore::save(const TokenBundle& bundle) {
  std::lock_guard<std::mutex> guard(mutex_);
  return write_locked(bundle);
}

bool TokenStore::clear() {
  std::lock_guard<std::mutex> guard(mutex_);
  TokenBundle empty;
  empty.updated_at = now_epoch_seconds();
  return write_locked(empty);
}

}  // namespace xbox::core
