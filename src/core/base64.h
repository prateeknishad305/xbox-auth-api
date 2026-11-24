#pragma once

#include <string>
#include <vector>

namespace xbox::core {

std::string base64_encode(const unsigned char* data, std::size_t size);
std::string base64url_encode(const unsigned char* data, std::size_t size);
std::string base64url_encode(const std::string& text);
std::string base64url_decode(const std::string& text);
std::vector<unsigned char> base64_decode(const std::string& text);

}  // namespace xbox::core
