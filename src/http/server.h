#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <memory>
#include <string>

#include "core/config.h"
#include "core/executor.h"
#include "xbox/auth.h"
#include "xbox/endpoints.h"

namespace xbox::http {

struct ServerState {
  core::Config config;
  auth::AuthService* auth_service = nullptr;
  api::Endpoints* endpoints = nullptr;
  core::Executor* executor = nullptr;
  boost::asio::io_context* io_context = nullptr;
};

class Server {
 public:
  Server(core::Config config, auth::AuthService& auth_service, api::Endpoints& endpoints);
  ~Server();

  int run();

 private:
  void accept_loop(boost::asio::ip::tcp::acceptor& acceptor);

  core::Config config_;
  auth::AuthService& auth_service_;
  api::Endpoints& endpoints_;
  std::unique_ptr<core::Executor> executor_;
  boost::asio::io_context io_context_;
  std::shared_ptr<ServerState> state_;
};

}  // namespace xbox::http
