#include "core/crypto.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <chrono>
#include <cctype>
#include <cstdio>

#include "core/base64.h"

namespace xbox::core {

std::string sha256(const std::string& text) {
  unsigned char digest[SHA256_DIGEST_LENGTH];
  unsigned int length = 0;
  EVP_MD_CTX* context = EVP_MD_CTX_new();
  if (context == nullptr) return std::string();
  if (EVP_DigestInit_ex(context, EVP_sha256(), nullptr) == 1 &&
      EVP_DigestUpdate(context, text.data(), text.size()) == 1) {
    EVP_DigestFinal_ex(context, digest, &length);
  }
  EVP_MD_CTX_free(context);
  return std::string(reinterpret_cast<char*>(digest), length);
}

std::string sha256_base64url(const std::string& text) {
  std::string digest = sha256(text);
  return base64url_encode(reinterpret_cast<const unsigned char*>(digest.data()), digest.size());
}

std::vector<unsigned char> random_bytes(std::size_t size) {
  std::vector<unsigned char> buffer(size);
  if (size == 0) return buffer;
  if (RAND_bytes(buffer.data(), static_cast<int>(size)) != 1) {
    for (std::size_t index = 0; index < size; ++index) {
      buffer[index] = static_cast<unsigned char>(std::rand() % 256);
    }
  }
  return buffer;
}

std::string random_base64url(std::size_t size) {
  std::vector<unsigned char> buffer = random_bytes(size);
  return base64url_encode(buffer.data(), buffer.size());
}

std::string random_hex(std::size_t bytes) {
  std::vector<unsigned char> buffer = random_bytes(bytes);
  std::string out;
  out.reserve(bytes * 2);
  for (unsigned char value : buffer) {
    char chunk[3];
    std::snprintf(chunk, sizeof(chunk), "%02x", value);
    out.append(chunk);
  }
  return out;
}

bool constant_time_equals(const std::string& left, const std::string& right) {
  if (left.size() != right.size()) return false;
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string url_encode(const std::string& text) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  out.reserve(text.size() * 3);
  for (unsigned char character : text) {
    if (std::isalnum(character) != 0 || character == '-' || character == '_' || character == '.' ||
        character == '~') {
      out.push_back(static_cast<char>(character));
      continue;
    }
    out.push_back('%');
    out.push_back(hex[(character >> 4) & 0x0F]);
    out.push_back(hex[character & 0x0F]);
  }
  return out;
}

std::string url_decode(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '+') {
      out.push_back(' ');
      continue;
    }
    if (text[index] == '%' && index + 2 < text.size()) {
      auto digit = [](char character) -> int {
        if (character >= '0' && character <= '9') return character - '0';
        if (character >= 'a' && character <= 'f') return character - 'a' + 10;
        if (character >= 'A' && character <= 'F') return character - 'A' + 10;
        return -1;
      };
      int high = digit(text[index + 1]);
      int low = digit(text[index + 2]);
      if (high >= 0 && low >= 0) {
        out.push_back(static_cast<char>((high << 4) | low));
        index += 2;
        continue;
      }
    }
    out.push_back(text[index]);
  }
  return out;
}

long long now_epoch_millis() {
  auto since = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
}

std::string now_epoch_seconds() {
  auto since = std::chrono::system_clock::now().time_since_epoch();
  return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(since).count());
}

}  // namespace xbox::core
