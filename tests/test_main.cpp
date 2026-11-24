#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>

#include "core/base64.h"
#include "core/cookies.h"
#include "core/crypto.h"
#include "core/http.h"
#include "core/json.h"
#include "core/log.h"
#include "core/store.h"
#include "xbox/auth.h"
#include "xbox/client.h"
#include "xbox/endpoints.h"
#include "xbox/msa.h"

namespace {

int failures = 0;

void check(bool condition, const std::string& label) {
  if (!condition) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s\n", label.c_str());
  }
}

void check_equal(const std::string& actual, const std::string& expected, const std::string& label) {
  if (actual != expected) {
    ++failures;
    std::fprintf(stderr, "FAIL: %s (expected '%s', got '%s')\n", label.c_str(), expected.c_str(),
                 actual.c_str());
  }
}

void test_base64() {
  check_equal(xbox::core::base64url_encode(std::string("hello")), "aGVsbG8", "base64url encode");
  check_equal(xbox::core::base64url_decode("aGVsbG8"), "hello", "base64url decode");
  check_equal(xbox::core::base64url_encode(std::string("a")), "YQ", "base64url single byte");
  check_equal(xbox::core::base64url_encode(std::string("ab")), "YWI", "base64url two bytes");
  check_equal(xbox::core::base64url_decode("YWI"), "ab", "base64url decode two bytes");
  check_equal(xbox::core::base64url_decode(std::string("")), "", "base64url empty");
}

void test_sha256() {
  std::string digest = xbox::core::sha256("abc");
  check(digest.size() == 32, "sha256 length");
  check_equal(xbox::core::sha256_base64url("abc"),
              "ungWv48Bz-pBQUDeXa4iI7ADYaOWF3qctBD_YfIAFa0", "sha256 base64url vector");
}

void test_pkce_challenge() {
  std::string verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  std::string challenge = xbox::core::sha256_base64url(verifier);
  check_equal(challenge, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM", "pkce challenge vector");
}

void test_url_encoding() {
  check_equal(xbox::core::url_encode("a b&c=d"), "a%20b%26c%3Dd", "url encode");
  check_equal(xbox::core::url_decode("a%20b%26c%3Dd"), "a b&c=d", "url decode");
}

void test_json_helpers() {
  bool ok = false;
  xbox::core::Json parsed = xbox::core::json_parse("{\"a\":1,\"b\":\"two\",\"c\":true}", &ok);
  check(ok, "json parse ok");
  check_equal(xbox::core::json_string(parsed, "b"), "two", "json string");
  check(xbox::core::json_int(parsed, "a") == 1, "json int");
  check(xbox::core::json_bool(parsed, "c"), "json bool");
  check(xbox::core::json_string(parsed, "missing", "fallback") == "fallback", "json fallback");

  xbox::core::Json broken = xbox::core::json_parse("{not json", &ok);
  check(!ok, "json parse failure detected");
  check(broken.is_object(), "json fallback object");
}

void test_store_round_trip() {
  std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "xbox-api-cpp-test-store";
  std::filesystem::create_directories(directory);
  std::string path = (directory / "tokens.json").string();

  xbox::core::TokenStore store(path);
  xbox::core::TokenBundle bundle;
  bundle.access_token = "access-value";
  bundle.refresh_token = "refresh-value";
  bundle.xbl_token = "xbl-value";
  bundle.xsts_token = "xsts-value";
  bundle.user_hash = "uhs-value";
  bundle.xuid = "1234567890";
  bundle.gamertag = "Tester";
  bundle.ms_expires_at = xbox::core::now_epoch_millis() + 3600000;
  bundle.xbl_expires_at = xbox::core::now_epoch_millis() + 3600000;
  bundle.xsts_expires_at = xbox::core::now_epoch_millis() + 3600000;
  bundle.updated_at = xbox::core::now_epoch_seconds();

  check(store.save(bundle), "store save");

  auto permissions = std::filesystem::status(path).permissions();
  check((permissions & std::filesystem::perms::owner_read) != std::filesystem::perms::none,
        "store file readable");
  check((permissions & (std::filesystem::perms::group_read | std::filesystem::perms::others_read)) ==
            std::filesystem::perms::none,
        "store file not group or world readable");

  xbox::core::TokenBundle loaded;
  check(store.load(loaded), "store load");
  check_equal(loaded.access_token, bundle.access_token, "stored access token");
  check_equal(loaded.refresh_token, bundle.refresh_token, "stored refresh token");
  check_equal(loaded.xuid, bundle.xuid, "stored xuid");
  check_equal(loaded.gamertag, bundle.gamertag, "stored gamertag");
  check(loaded.session_valid(), "stored session valid");
}

void test_expiry_helpers() {
  xbox::core::TokenBundle bundle;
  bundle.xbl_token = "x";
  bundle.xsts_token = "x";
  check(!bundle.session_valid(), "empty session invalid");
  bundle.xbl_expires_at = xbox::core::now_epoch_millis() + 3600000;
  bundle.xsts_expires_at = xbox::core::now_epoch_millis() + 3600000;
  check(bundle.session_valid(), "fresh session valid");
  bundle.xsts_expires_at = xbox::core::now_epoch_millis() - 1000;
  check(!bundle.session_valid(), "expired session invalid");
}

void test_mapping_helpers() {
  xbox::core::Config config;
  config.client_id = "test-client";
  config.token_file = "/tmp/opencode/xbox-api-cpp-tests/tokens.json";
  xbox::core::TokenStore store(config.token_file);
  xbox::core::HttpClient http;
  xbox::auth::AuthService auth_service(config, store, http);
  xbox::api::XboxClient client(auth_service, http);
  xbox::api::Endpoints endpoints(client);

  xbox::core::Json title = xbox::core::Json::object();
  title["titleId"] = "title-1";
  title["name"] = "Sample Game";
  title["titleType"] = "Game";
  xbox::core::Json achievement = xbox::core::Json::object();
  achievement["currentAchievements"] = 5;
  achievement["totalAchievements"] = 10;
  achievement["currentGamerscore"] = 50;
  achievement["totalGamerscore"] = 100;
  achievement["sourceVersion"] = 2;
  title["achievement"] = achievement;
  xbox::core::Json history = xbox::core::Json::object();
  history["minutesPlayed"] = 120;
  history["lastTimePlayed"] = "2024-05-05T00:00:00Z";
  title["titleHistory"] = history;
  xbox::core::Json images = xbox::core::Json::array();
  xbox::core::Json image = xbox::core::Json::object();
  image["type"] = "Icon";
  image["url"] = "https://example.invalid/icon.png";
  images.push_back(image);
  title["images"] = images;

  xbox::core::Json mapped = endpoints.map_title(title);
  check_equal(xbox::core::json_string(mapped, "titleId"), "title-1", "mapped title id");
  check_equal(xbox::core::json_string(mapped, "icon"), "https://example.invalid/icon.png",
              "mapped title icon");
  check(std::fabs(xbox::core::json_double(xbox::core::json_object(mapped, "achievement"),
                                           "completion") - 50.0) < 0.001,
        "mapped title completion");
  check(xbox::core::json_int(xbox::core::json_object(mapped, "history"), "minutesPlayed") == 120,
        "mapped title history");

  xbox::core::Json achievement_item = xbox::core::Json::object();
  achievement_item["id"] = "ach-1";
  achievement_item["name"] = "First Win";
  achievement_item["description"] = "Win once";
  achievement_item["progressState"] = "Achieved";
  achievement_item["progress"] = 100;
  xbox::core::Json progression = xbox::core::Json::object();
  progression["timeUnlocked"] = "2024-06-01T10:00:00Z";
  achievement_item["progression"] = progression;
  xbox::core::Json rewards = xbox::core::Json::array();
  xbox::core::Json reward = xbox::core::Json::object();
  reward["type"] = "Gamerscore";
  reward["value"] = "25";
  rewards.push_back(reward);
  achievement_item["rewards"] = rewards;

  xbox::core::Json mapped_achievement = endpoints.map_achievement(achievement_item, "Sample Game", false);
  check(xbox::core::json_bool(mapped_achievement, "unlocked"), "mapped achievement unlocked");
  check(xbox::core::json_int(mapped_achievement, "gamerscore") == 25, "mapped achievement gamerscore");
  check_equal(xbox::core::json_string(mapped_achievement, "title"), "Sample Game",
              "mapped achievement title");
}

void test_form_encoding() {
  std::vector<std::pair<std::string, std::string>> fields = {{"grant_type", "refresh_token"},
                                                              {"refresh_token", "a b"}};
  check_equal(xbox::core::form_encode(fields), "grant_type=refresh_token&refresh_token=a%20b",
              "form encoding");
}

void test_log_redaction() {
  std::string message =
      "{\"access_token\":\"secret-value\",\"refresh_token\":\"another-secret\",\"xuid\":\"123\"}";
  std::string redacted = xbox::core::redact(message);
  check(redacted.find("secret-value") == std::string::npos, "access token redacted");
  check(redacted.find("another-secret") == std::string::npos, "refresh token redacted");
  check(redacted.find("123") != std::string::npos, "non secret value kept");
}

void test_cookie_jar_merge() {
  xbox::core::HttpResponse response;
  response.header_entries.emplace_back("set-cookie", "MSPPre=cookie-one; path=/; secure");
  response.header_entries.emplace_back("set-cookie", "uaid=cookie-two; path=/; secure");

  std::string jar = xbox::core::merge_set_cookies(std::string(), response);
  check(jar.find("MSPPre=cookie-one") != std::string::npos, "first cookie stored");
  check(jar.find("uaid=cookie-two") != std::string::npos, "second cookie stored");

  xbox::core::HttpResponse update;
  update.header_entries.emplace_back("set-cookie", "MSPPre=cookie-three; path=/");
  std::string updated = xbox::core::merge_set_cookies(jar, update);
  check(updated.find("MSPPre=cookie-three") != std::string::npos, "cookie replaced");
  check(updated.find("MSPPre=cookie-one") == std::string::npos, "old cookie value dropped");
  check(updated.find("uaid=cookie-two") != std::string::npos, "other cookie kept");
}

void test_token_fragment_parsing() {
  std::string access_token;
  std::string refresh_token;
  long long expires_in = 0;

  bool ok = xbox::msa::parse_token_fragment(
      "access_token=abc.def&refresh_token=M.R3_B_token&expires_in=3600", access_token, refresh_token,
      expires_in);
  check(ok, "fragment parsed");
  check_equal(access_token, "abc.def", "fragment access token");
  check_equal(refresh_token, "M.R3_B_token", "fragment refresh token");
  check(expires_in == 3600, "fragment expiry");

  ok = xbox::msa::parse_token_fragment(
      "https://login.live.com/oauth20_desktop.srf#access_token=url-token&refresh_token=url-refresh",
      access_token, refresh_token, expires_in);
  check(ok, "redirect url fragment parsed");
  check_equal(access_token, "url-token", "redirect url access token");
  check_equal(refresh_token, "url-refresh", "redirect url refresh token");

  ok = xbox::msa::parse_token_fragment("no token here", access_token, refresh_token, expires_in);
  check(!ok, "invalid fragment rejected");
}

void test_password_profile_config() {
  xbox::core::Config config = xbox::core::load_config();
  check_equal(config.password_client_id, "000000004C12AE6F", "password client id");
  check_equal(config.password_scope, "service::user.auth.xboxlive.com::MBI_SSL", "password scope");
  check_equal(config.password_authorize_url, "https://login.live.com/oauth20_authorize.srf",
              "password authorize url");
  check_equal(config.password_token_url, "https://login.live.com/oauth20_token.srf",
              "password token url");
  check_equal(config.password_redirect_uri, "https://login.live.com/oauth20_desktop.srf",
              "password redirect uri");
}

void test_cookie_profile_config() {
  xbox::core::Config config = xbox::core::load_config();
  check_equal(config.cookie_client_id, "00000000402b5328", "cookie client id");
  check_equal(config.cookie_scope, "service::user.auth.xboxlive.com::MBI_SSL", "cookie scope");
  check_equal(config.cookie_authorize_url, "https://login.live.com/oauth20_authorize.srf",
              "cookie authorize url");
  check_equal(config.cookie_token_url, "https://login.live.com/oauth20_token.srf",
              "cookie token url");
  check_equal(config.cookie_redirect_uri, "https://login.live.com/oauth20_desktop.srf",
              "cookie redirect uri");
}

}  // namespace

int main() {
  test_base64();
  test_sha256();
  test_pkce_challenge();
  test_url_encoding();
  test_json_helpers();
  test_store_round_trip();
  test_expiry_helpers();
  test_mapping_helpers();
  test_form_encoding();
  test_log_redaction();
  test_cookie_jar_merge();
  test_token_fragment_parsing();
  test_password_profile_config();
  test_cookie_profile_config();

  if (failures > 0) {
    std::fprintf(stderr, "%d test(s) failed\n", failures);
    return 1;
  }
  std::fprintf(stdout, "all tests passed\n");
  return 0;
}
