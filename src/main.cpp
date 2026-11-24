#include <iostream>
#include <string>

#include "core/config.h"
#include "core/http.h"
#include "core/log.h"
#include "core/store.h"
#include "http/server.h"
#include "xbox/auth.h"
#include "xbox/client.h"
#include "xbox/endpoints.h"

namespace {

xbox::core::LogLevel parse_log_level(const std::string& value) {
  if (value == "debug") return xbox::core::LogLevel::kDebug;
  if (value == "warn") return xbox::core::LogLevel::kWarn;
  if (value == "error") return xbox::core::LogLevel::kError;
  return xbox::core::LogLevel::kInfo;
}

}  // namespace

int main() {
  xbox::core::Config config = xbox::core::load_config();
  xbox::core::set_log_level(parse_log_level(config.log_level));

  xbox::core::HttpClient http;
  xbox::core::TokenStore store(config.token_file);
  xbox::auth::AuthService auth_service(config, store, http);
  xbox::api::XboxClient client(auth_service, http);
  xbox::api::Endpoints endpoints(client);

  xbox::http::Server server(config, auth_service, endpoints);
  return server.run();
}
