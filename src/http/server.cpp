#include "http/server.h"

#include <boost/asio/post.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/beast/version.hpp>

#include <algorithm>
#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/crypto.h"
#include "core/json.h"
#include "core/log.h"

namespace xbox::http {
namespace {

namespace beast = boost::beast;
namespace beast_http = boost::beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;

struct Reply {
  int status = 200;
  std::string body;
  std::string content_type = "application/json";
};

Reply json_reply(int status, const core::Json& body) {
  Reply reply;
  reply.status = status;
  reply.body = body.dump();
  reply.content_type = "application/json";
  return reply;
}

Reply error_reply(int status, const std::string& code, const std::string& message) {
  core::Json body = core::Json::object();
  body["error"] = code;
  body["message"] = message;
  return json_reply(status, body);
}

std::map<std::string, std::string> parse_query(const std::string& query) {
  std::map<std::string, std::string> out;
  std::size_t start = 0;
  while (start <= query.size()) {
    std::size_t separator = query.find('&', start);
    if (separator == std::string::npos) separator = query.size();
    std::string pair = query.substr(start, separator - start);
    std::size_t equals = pair.find('=');
    if (equals != std::string::npos) {
      out[core::url_decode(pair.substr(0, equals))] = core::url_decode(pair.substr(equals + 1));
    }
    if (separator == query.size()) break;
    start = separator + 1;
  }
  return out;
}

int query_int(const std::map<std::string, std::string>& query, const std::string& key, int fallback) {
  auto it = query.find(key);
  if (it == query.end()) return fallback;
  try {
    return std::stoi(it->second);
  } catch (...) {
    return fallback;
  }
}

bool path_starts_with(const std::string& target, const std::string& prefix) {
  return target.rfind(prefix, 0) == 0;
}

class Session : public std::enable_shared_from_this<Session> {
 public:
  Session(tcp::socket socket, std::shared_ptr<ServerState> state)
      : socket_(std::move(socket)), state_(std::move(state)) {}

  void run() {
    do_read();
  }

 private:
  void do_read() {
    request_ = {};
    auto self = shared_from_this();
    beast_http::async_read(socket_, buffer_, request_,
                      [self](beast::error_code error, std::size_t) { self->on_read(error); });
  }

  void on_read(beast::error_code error) {
    if (error == beast_http::error::end_of_stream) {
      beast::error_code ignored;
      socket_.shutdown(tcp::socket::shutdown_both, ignored);
      return;
    }
    if (error) {
      log_message(core::LogLevel::kDebug, "connection error: " + error.message());
      beast::error_code ignored;
      socket_.shutdown(tcp::socket::shutdown_both, ignored);
      return;
    }
    dispatch();
  }

  bool is_heavy_route(const std::string& path) const {
    return path_starts_with(path, "/api/profile") || path_starts_with(path, "/api/titles") ||
           path_starts_with(path, "/api/achievements") || path_starts_with(path, "/api/activity") ||
           path_starts_with(path, "/api/subscriptions");
  }

  void dispatch() {
    std::string target(request_.target());
    std::string method(request_.method_string());
    std::string body(request_.body());
    std::string path = target;
    std::string query;
    std::size_t mark = target.find('?');
    if (mark != std::string::npos) {
      path = target.substr(0, mark);
      query = target.substr(mark + 1);
    }

    std::shared_ptr<ServerState> state = state_;
    auto responder = std::make_shared<std::function<Reply()>>();
    *responder = [state, method, path, query, body]() { return route(state, method, path, query, body); };

    if (is_heavy_route(path)) {
      auto self = shared_from_this();
      state_->executor->submit([self, responder]() {
        Reply reply;
        try {
          reply = (*responder)();
        } catch (const std::exception& error) {
          reply = error_reply(500, "internal_error", error.what());
        }
        net::post(*self->state_->io_context, [self, reply]() { self->send(reply); });
      });
      return;
    }
    send((*responder)());
  }

  static Reply complete_callback(const std::shared_ptr<ServerState>& state,
                                 std::map<std::string, std::string>& params) {
    std::string code = params.count("code") != 0 ? params["code"] : std::string();
    std::string state_value = params.count("state") != 0 ? params["state"] : std::string();
    if (code.empty()) {
      std::string description =
          params.count("error_description") != 0 ? params["error_description"] : std::string();
      std::string error_code = params.count("error") != 0 ? params["error"] : "missing_code";
      return error_reply(400, error_code,
                         description.empty() ? "Authorization code is missing from the callback."
                                             : description);
    }
    auth::AuthError error;
    if (!state->auth_service->complete_authorization(state_value, code, error)) {
      return error_reply(error.status, error.code, error.message);
    }
    auth::SessionInfo info = state->auth_service->session_info();
    core::Json out = core::Json::object();
    out["authenticated"] = true;
    out["xuid"] = info.xuid;
    out["gamertag"] = info.gamertag;
    return json_reply(200, out);
  }

  static Reply route(const std::shared_ptr<ServerState>& state, const std::string& method,
                     const std::string& path, const std::string& query, const std::string& body) {
    std::map<std::string, std::string> params = parse_query(query);

    if (path == "/health" && method == "GET") {
      core::Json out = core::Json::object();
      out["status"] = "ok";
      return json_reply(200, out);
    }

    if (path == "/api" && method == "GET") {
      core::Json out = core::Json::object();
      out["name"] = "xbox-live-api-cpp";
      out["version"] = "1.0.0";
      core::Json routes = core::Json::array();
      routes.push_back("GET /api/auth/status");
      routes.push_back("POST /api/auth/login");
      routes.push_back("POST /api/auth/code");
      routes.push_back("GET /api/auth/callback");
      routes.push_back("POST /api/auth/import");
      routes.push_back("POST /api/auth/cookie");
      routes.push_back("POST /api/auth/password");
      routes.push_back("POST /api/auth/fragment");
      routes.push_back("POST /api/auth/logout");
      routes.push_back("GET /api/profile");
      routes.push_back("GET /api/titles");
      routes.push_back("GET /api/achievements");
      routes.push_back("GET /api/achievements/{titleId}");
      routes.push_back("GET /api/activity");
      routes.push_back("GET /api/activity/presence");
      routes.push_back("GET /api/subscriptions");
      out["routes"] = routes;
      return json_reply(200, out);
    }

    if (path == "/api/auth/login" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string login_hint = core::json_string(payload, "email");
      if (login_hint.empty()) login_hint = core::json_string(payload, "loginHint");
      if (login_hint.empty()) login_hint = core::json_string(payload, "login_hint");
      auth::AuthError error;
      auth::AuthorizationRequest request = state->auth_service->start_authorization(login_hint, error);
      core::Json out = core::Json::object();
      out["authorizationUrl"] = request.authorization_url;
      out["state"] = request.state;
      out["expiresIn"] = request.expires_in;
      out["redirectUri"] = state->config.redirect_uri;
      out["loginWithCodeUrl"] = "/api/auth/code";
      out["message"] =
          "Open the authorization URL, sign in, then send the code back to POST /api/auth/code "
          "as {\"code\":\"...\",\"state\":\"...\"}.";
      return json_reply(200, out);
    }

    if (path == "/api/auth/code" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string code = core::json_string(payload, "code");
      if (code.empty()) code = core::json_string(payload, "authorizationCode");
      if (code.empty()) {
        return error_reply(400, "missing_code",
                           "Send {\"code\":\"...\",\"state\":\"...\"} in the request body.");
      }
      std::string state_value = core::json_string(payload, "state");
      auth::AuthError error;
      bool ok = state_value.empty()
                    ? state->auth_service->complete_authorization_any(code, error)
                    : state->auth_service->complete_authorization(state_value, code, error);
      if (!ok) {
        return error_reply(error.status, error.code, error.message);
      }
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = true;
      out["xuid"] = info.xuid;
      out["gamertag"] = info.gamertag;
      return json_reply(200, out);
    }

    if (path == "/api/auth/callback" && method == "GET") {
      return complete_callback(state, params);
    }

    if (path == "/" && method == "GET" && params.count("code") != 0) {
      return complete_callback(state, params);
    }

    if (path == "/" && method == "GET") {
      core::Json out = core::Json::object();
      out["name"] = "xbox-live-api-cpp";
      out["version"] = "1.0.0";
      out["message"] = "Xbox Live personal API. See /api for the route list.";
      return json_reply(200, out);
    }

    if (path == "/api/auth/status" && method == "GET") {
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = info.authenticated;
      out["xuid"] = info.xuid.empty() ? core::Json(nullptr) : core::Json(info.xuid);
      out["gamertag"] = info.gamertag.empty() ? core::Json(nullptr) : core::Json(info.gamertag);
      out["tokens"] = core::Json::object();
      out["tokens"]["microsoft"] = info.ms_valid;
      out["tokens"]["xboxLive"] = info.xbl_valid;
      out["tokens"]["xboxSecurity"] = info.xsts_valid;
      return json_reply(200, out);
    }

    if (path == "/api/auth/import" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string refresh_token = core::json_string(payload, "refreshToken");
      if (refresh_token.empty()) refresh_token = core::json_string(payload, "refresh_token");
      if (refresh_token.empty()) {
        return error_reply(400, "missing_refresh_token",
                           "Send {\"refreshToken\": \"...\"} in the request body.");
      }
      auth::AuthError error;
      std::string client_id = core::json_string(payload, "clientId");
      if (client_id.empty()) client_id = core::json_string(payload, "client_id");
      std::string scope = core::json_string(payload, "scope");
      std::string token_url = core::json_string(payload, "tokenUrl");
      if (token_url.empty()) token_url = core::json_string(payload, "token_url");
      if (!state->auth_service->import_refresh_token(refresh_token, client_id, scope, token_url, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = true;
      out["xuid"] = info.xuid;
      out["gamertag"] = info.gamertag;
      return json_reply(200, out);
    }

    if (path == "/api/auth/cookie" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string cookie = core::json_string(payload, "cookie");
      if (cookie.empty()) cookie = core::json_string(payload, "cookies");
      if (cookie.empty()) {
        return error_reply(400, "missing_cookie",
                           "Send {\"cookie\": \"__Host-MSAAUTHP=...\"} in the request body.");
      }
      auth::AuthError error;
      if (!state->auth_service->login_with_cookie(cookie, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = true;
      out["xuid"] = info.xuid;
      out["gamertag"] = info.gamertag;
      return json_reply(200, out);
    }

    if (path == "/api/auth/password" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string email = core::json_string(payload, "email");
      if (email.empty()) email = core::json_string(payload, "username");
      std::string password = core::json_string(payload, "password");
      if (email.empty() || password.empty()) {
        return error_reply(400, "missing_credentials",
                           "Send {\"email\": \"...\", \"password\": \"...\"} in the request body.");
      }
      auth::AuthError error;
      if (!state->auth_service->login_with_password(email, password, error)) {
        if (error.code == "verification_required") {
          core::Json out = core::Json::object();
          out["error"] = error.code;
          out["message"] = error.message;
          out["verificationUrl"] = error.verification_url;
          out["channels"] = core::Json::array();
          for (const std::string& channel : error.verification_channels) {
            out["channels"].push_back(channel);
          }
          out["resumeUrl"] = "/api/auth/fragment";
          out["resumeMessage"] =
              "Finish the verification in a browser, then copy the redirect fragment after '#' and "
              "send it to POST /api/auth/fragment as {\"fragment\": \"...\"}.";
          return json_reply(409, out);
        }
        return error_reply(error.status, error.code, error.message);
      }
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = true;
      out["xuid"] = info.xuid;
      out["gamertag"] = info.gamertag;
      return json_reply(200, out);
    }

    if (path == "/api/auth/fragment" && method == "POST") {
      core::Json payload = core::json_parse(body);
      std::string fragment = core::json_string(payload, "fragment");
      if (fragment.empty()) fragment = core::json_string(payload, "url");
      if (fragment.empty()) {
        return error_reply(400, "missing_fragment",
                           "Send {\"fragment\": \"access_token=...&refresh_token=...\"}.");
      }
      auth::AuthError error;
      if (!state->auth_service->login_with_fragment(fragment, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      auth::SessionInfo info = state->auth_service->session_info();
      core::Json out = core::Json::object();
      out["authenticated"] = true;
      out["xuid"] = info.xuid;
      out["gamertag"] = info.gamertag;
      return json_reply(200, out);
    }

    if (path == "/api/auth/logout" && method == "POST") {      state->auth_service->logout();
      core::Json out = core::Json::object();
      out["message"] = "Session cleared.";
      return json_reply(200, out);
    }

    if (path == "/api/profile" && method == "GET") {
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->profile(out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path == "/api/titles" && method == "GET") {
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->titles(out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path == "/api/achievements" && method == "GET") {
      int max_titles = query_int(params, "maxTitles", 20);
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->achievements(max_titles, out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path_starts_with(path, "/api/achievements/") && method == "GET") {
      std::string title_id = path.substr(std::string("/api/achievements/").size());
      if (title_id.empty()) return error_reply(400, "missing_title_id", "A title id is required.");
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->achievements_for_title(title_id, out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path == "/api/activity" && method == "GET") {
      int max_items = query_int(params, "maxItems", 20);
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->activity(max_items, out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path == "/api/activity/presence" && method == "GET") {
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->presence(out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    if (path == "/api/subscriptions" && method == "GET") {
      core::Json out;
      api::ApiError error;
      if (!state->endpoints->subscriptions(out, error)) {
        return error_reply(error.status, error.code, error.message);
      }
      return json_reply(200, out);
    }

    return error_reply(404, "not_found", "No route matches " + method + " " + path + ".");
  }

  void send(const Reply& reply) {
    response_ = {};
    response_.version(11);
    response_.result(static_cast<beast_http::status>(reply.status));
    response_.set(beast_http::field::server, "xbox-live-api-cpp");
    response_.set(beast_http::field::content_type, reply.content_type);
    response_.body() = reply.body;
    response_.prepare_payload();
    do_write();
  }

  void do_write() {
    auto self = shared_from_this();
    beast_http::async_write(socket_, response_,
                       [self](beast::error_code error, std::size_t) { self->on_write(error); });
  }

  void on_write(beast::error_code error) {
    if (error) {
      log_message(core::LogLevel::kDebug, "write error: " + error.message());
      beast::error_code ignored;
      socket_.shutdown(tcp::socket::shutdown_both, ignored);
      return;
    }
    beast::error_code ignored;
    socket_.shutdown(tcp::socket::shutdown_both, ignored);
  }

  tcp::socket socket_;
  beast::flat_buffer buffer_;
  beast_http::request<beast_http::string_body> request_;
  beast_http::response<beast_http::string_body> response_;
  std::shared_ptr<ServerState> state_;
};

}  // namespace

Server::Server(core::Config config, auth::AuthService& auth_service, api::Endpoints& endpoints)
    : config_(std::move(config)), auth_service_(auth_service), endpoints_(endpoints) {
  executor_ = std::make_unique<core::Executor>(static_cast<std::size_t>(config_.threads));
  state_ = std::make_shared<ServerState>();
  state_->config = config_;
  state_->auth_service = &auth_service_;
  state_->endpoints = &endpoints_;
  state_->executor = executor_.get();
  state_->io_context = &io_context_;
}

Server::~Server() {
  io_context_.stop();
  if (executor_) executor_->shutdown();
}

void Server::accept_loop(tcp::acceptor& acceptor) {
  acceptor.async_accept([this, &acceptor](beast::error_code error, tcp::socket socket) {
    if (!error) {
      std::make_shared<Session>(std::move(socket), state_)->run();
    } else if (error != net::error::operation_aborted) {
      log_message(core::LogLevel::kWarn, "accept error: " + error.message());
    }
    accept_loop(acceptor);
  });
}

int Server::run() {
  tcp::endpoint endpoint(net::ip::make_address(config_.host), static_cast<unsigned short>(config_.port));
  tcp::acceptor acceptor(io_context_);

  beast::error_code error;
  acceptor.open(endpoint.protocol(), error);
  if (error) {
    log_message(core::LogLevel::kError, "cannot open acceptor: " + error.message());
    return 1;
  }
  acceptor.set_option(net::socket_base::reuse_address(true), error);
  acceptor.bind(endpoint, error);
  if (error) {
    log_message(core::LogLevel::kError, "cannot bind acceptor: " + error.message());
    return 1;
  }
  acceptor.listen(net::socket_base::max_listen_connections, error);
  if (error) {
    log_message(core::LogLevel::kError, "cannot listen: " + error.message());
    return 1;
  }

  net::signal_set signals(io_context_, SIGINT, SIGTERM);
  signals.async_wait([this](beast::error_code, int) { io_context_.stop(); });

  accept_loop(acceptor);
  log_message(core::LogLevel::kInfo, "xbox-live-api-cpp listening on " + config_.host + ":" +
                                       std::to_string(config_.port));

  std::vector<std::thread> workers;
  std::size_t worker_count = static_cast<std::size_t>(std::max(2, config_.threads));
  workers.reserve(worker_count);
  for (std::size_t index = 0; index < worker_count; ++index) {
    workers.emplace_back([this]() { io_context_.run(); });
  }

  for (std::thread& worker : workers) {
    if (worker.joinable()) worker.join();
  }
  return 0;
}

}  // namespace xbox::http
