#include "core/base64.h"

#include <openssl/evp.h>

namespace xbox::core {
namespace {

const char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char kUrlAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string encode_with(const char* alphabet, const unsigned char* data, std::size_t size) {
  std::string out;
  out.reserve(((size + 2) / 3) * 4);
  std::size_t index = 0;
  while (index + 2 < size) {
    unsigned int triple = (static_cast<unsigned int>(data[index]) << 16) |
                         (static_cast<unsigned int>(data[index + 1]) << 8) |
                         static_cast<unsigned int>(data[index + 2]);
    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back(alphabet[(triple >> 6) & 0x3F]);
    out.push_back(alphabet[triple & 0x3F]);
    index += 3;
  }
  if (index + 1 == size) {
    unsigned int triple = static_cast<unsigned int>(data[index]) << 16;
    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back('=');
    out.push_back('=');
  } else if (index + 2 == size) {
    unsigned int triple = (static_cast<unsigned int>(data[index]) << 16) |
                         (static_cast<unsigned int>(data[index + 1]) << 8);
    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back(alphabet[(triple >> 6) & 0x3F]);
    out.push_back('=');
  }
  return out;
}

int value_of(char character) {
  if (character >= 'A' && character <= 'Z') return character - 'A';
  if (character >= 'a' && character <= 'z') return character - 'a' + 26;
  if (character >= '0' && character <= '9') return character - '0' + 52;
  if (character == '+' || character == '-') return 62;
  if (character == '/' || character == '_') return 63;
  return -1;
}

}  // namespace

std::string base64_encode(const unsigned char* data, std::size_t size) {
  return encode_with(kAlphabet, data, size);
}

std::string base64url_encode(const unsigned char* data, std::size_t size) {
  std::string out = encode_with(kUrlAlphabet, data, size);
  while (!out.empty() && out.back() == '=') out.pop_back();
  return out;
}

std::string base64url_encode(const std::string& text) {
  return base64url_encode(reinterpret_cast<const unsigned char*>(text.data()), text.size());
}

std::vector<unsigned char> base64_decode(const std::string& text) {
  std::vector<unsigned char> out;
  unsigned int buffer = 0;
  int bits = 0;
  for (char character : text) {
    if (character == '=' || character == '\n' || character == '\r') continue;
    int value = value_of(character);
    if (value < 0) continue;
    buffer = (buffer << 6) | static_cast<unsigned int>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

std::string base64url_decode(const std::string& text) {
  std::vector<unsigned char> raw = base64_decode(text);
  return std::string(raw.begin(), raw.end());
}

}  // namespace xbox::core
