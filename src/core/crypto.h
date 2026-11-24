#pragma once

#include <string>
#include <vector>

namespace xbox::core {

std::string sha256(const std::string& text);
std::string sha256_base64url(const std::string& text);
std::vector<unsigned char> random_bytes(std::size_t size);
std::string random_base64url(std::size_t size);
std::string random_hex(std::size_t bytes);
bool constant_time_equals(const std::string& left, const std::string& right);
std::string url_encode(const std::string& text);
std::string url_decode(const std::string& text);
std::string now_epoch_seconds();
long long now_epoch_millis();

}  // namespace xbox::core
