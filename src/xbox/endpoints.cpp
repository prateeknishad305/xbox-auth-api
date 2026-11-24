#include "xbox/endpoints.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>

#include "core/crypto.h"
#include "core/log.h"

namespace xbox::api {
namespace {

const char kProfileUrl[] =
    "https://profile.xboxlive.com/users/me/profile/settings?settings=GameDisplayPicRaw,Gamertag,"
    "Gamerscore,AccountTier,PreferredColor,Motto,RealName,TenureLevel,Watermarks,Location,Bio";
const char kTitlesUrl[] =
    "https://titlehub.xboxlive.com/users/xuid(%s)/titles/titleHistory/decoration/Achievement,Image";
const char kPresenceUrl[] = "https://userpresence.xboxlive.com/users/me?level=all";
const char kSubscriptionsUrl[] =
    "https://peoplehub.xboxlive.com/users/me/people/xuids(%s)/decoration/detail";

std::string format_url(const char* pattern, const std::string& value) {
  std::string out(pattern);
  std::size_t position = out.find("%s");
  if (position != std::string::npos) out.replace(position, 2, value);
  return out;
}

std::string first_setting(const core::Json& user, const std::string& id) {
  core::Json settings = core::json_array(user, "settings");
  for (const core::Json& setting : settings) {
    if (core::json_string(setting, "id") == id) return core::json_string(setting, "value");
  }
  return std::string();
}

core::Json nullable_string(const std::string& value) {
  if (value.empty()) return core::Json(nullptr);
  return core::Json(value);
}

core::Json nullable_number(double value) {
  if (value < 0) return core::Json(nullptr);
  return core::Json(value);
}

}  // namespace

Endpoints::Endpoints(XboxClient& client) : client_(client) {}

core::Json Endpoints::map_title(const core::Json& title) const {
  core::Json achievement = core::json_object(title, "achievement");
  core::Json history = core::json_object(title, "titleHistory");

  std::string icon;
  core::Json images = core::json_array(title, "images");
  for (const core::Json& image : images) {
    std::string type = core::json_string(image, "type");
    if (type == "Icon" || type == "Tile" || type == "BrandedKeyArt") {
      icon = core::json_string(image, "url");
      break;
    }
  }
  if (icon.empty()) icon = core::json_string(title, "displayImage");

  bool has_achievements = title.contains("achievement");
  int current_achievements = static_cast<int>(core::json_int(achievement, "currentAchievements", 0));
  int total_achievements = static_cast<int>(core::json_int(achievement, "totalAchievements", 0));
  double completion = 0.0;
  if (total_achievements > 0) {
    completion = std::round((static_cast<double>(current_achievements) / total_achievements) * 1000.0) /
                10.0;
  }

  core::Json out = core::Json::object();
  out["titleId"] = core::json_string(title, "titleId");
  out["pfn"] = nullable_string(core::json_string(title, "pfn"));
  out["storeId"] = nullable_string(core::json_string(title, "storeId"));
  out["name"] = core::json_string(title, "name");
  out["type"] = core::json_string(title, "titleType");
  out["icon"] = nullable_string(icon);

  if (has_achievements) {
    core::Json mapped = core::Json::object();
    mapped["currentAchievements"] = current_achievements;
    mapped["totalAchievements"] = total_achievements;
    mapped["currentGamerscore"] = static_cast<int>(core::json_int(achievement, "currentGamerscore", 0));
    mapped["totalGamerscore"] = static_cast<int>(core::json_int(achievement, "totalGamerscore", 0));
    mapped["is360"] = core::json_int(achievement, "sourceVersion", 0) == 1;
    mapped["completion"] = completion;
    out["achievement"] = mapped;
  } else {
    out["achievement"] = core::Json(nullptr);
  }

  core::Json mapped_history = core::Json::object();
  mapped_history["minutesPlayed"] = core::json_int(history, "minutesPlayed", 0);
  mapped_history["lastTimePlayed"] = nullable_string(core::json_string(history, "lastTimePlayed"));
  out["history"] = mapped_history;
  return out;
}

core::Json Endpoints::map_achievement(const core::Json& achievement, const std::string& title_name,
                                      bool is_360) const {
  int gamerscore = -1;
  core::Json rewards = core::json_array(achievement, "rewards");
  for (const core::Json& reward : rewards) {
    if (core::json_string(reward, "type") == "Gamerscore") {
      gamerscore = static_cast<int>(core::json_int(reward, "value", 0));
      break;
    }
  }
  if (gamerscore < 0 && achievement.contains("gamerscore")) {
    gamerscore = static_cast<int>(core::json_int(achievement, "gamerscore", 0));
  }

  std::string icon;
  core::Json media = core::json_array(achievement, "mediaAssets");
  for (const core::Json& asset : media) {
    if (core::json_string(asset, "type") == "Icon") {
      icon = core::json_string(asset, "url");
      break;
    }
  }

  core::Json progression = core::json_object(achievement, "progression");
  std::string time_unlocked = is_360 ? core::json_string(achievement, "timeUnlocked")
                                     : core::json_string(progression, "timeUnlocked");
  bool unlocked = false;
  if (is_360) {
    unlocked = !time_unlocked.empty() && time_unlocked.rfind("0001", 0) != 0;
  } else {
    unlocked = core::json_string(achievement, "progressState") == "Achieved";
  }

  std::string progress_state = core::json_string(achievement, "progressState");
  if (progress_state.empty()) progress_state = unlocked ? "Achieved" : "NotStarted";

  core::Json rarity = core::json_object(achievement, "rarity");
  double rarity_percentage = -1.0;
  if (rarity.contains("currentPercentage")) {
    rarity_percentage = core::json_double(rarity, "currentPercentage", -1.0);
  }

  core::Json out = core::Json::object();
  out["id"] = core::json_string(achievement, "id");
  out["name"] = core::json_string(achievement, "name");
  out["description"] = core::json_string(achievement, "description");
  out["lockedDescription"] = nullable_string(core::json_string(achievement, "lockedDescription"));
  out["progressState"] = progress_state;
  out["progress"] = static_cast<int>(core::json_int(achievement, "progress", 0));
  out["unlocked"] = unlocked;
  out["timeUnlocked"] = unlocked ? nullable_string(time_unlocked) : core::Json(nullptr);
  out["isSecret"] = core::json_bool(achievement, "isSecret", false);
  out["type"] = core::json_string(achievement, "type");
  out["gamerscore"] = gamerscore < 0 ? core::Json(nullptr) : core::Json(gamerscore);
  out["icon"] = nullable_string(icon);
  out["rarity"] = nullable_number(rarity_percentage);
  out["title"] = title_name.empty() ? core::Json(nullptr) : core::Json(title_name);
  return out;
}

bool Endpoints::cached_titles(std::vector<core::Json>& titles, ApiError& error) {
  {
    std::lock_guard<std::mutex> guard(cache_mutex_);
    if (!title_cache_.empty() && title_cache_expires_at > core::now_epoch_millis()) {
      titles = title_cache_;
      return true;
    }
  }
  if (!fetch_titles(titles, error)) return false;
  std::lock_guard<std::mutex> guard(cache_mutex_);
  title_cache_ = titles;
  title_cache_expires_at = core::now_epoch_millis() + 60000;
  return true;
}

bool Endpoints::fetch_titles(std::vector<core::Json>& titles, ApiError& error) {
  std::string xuid = client_.auth_service().xuid();
  if (xuid.empty()) {
    error.status = 400;
    error.code = "missing_xuid";
    error.message = "No XUID is cached. Call GET /api/profile first.";
    return false;
  }
  core::Json payload;
  std::vector<std::pair<std::string, std::string>> headers = {{"x-xbl-contract-version", "2"}};
  if (!client_.get(format_url(kTitlesUrl, xuid), headers, payload, error)) return false;
  core::Json items = core::json_array(payload, "titles");
  titles.clear();
  titles.reserve(items.size());
  for (const core::Json& title : items) titles.push_back(title);
  return true;
}

bool Endpoints::profile(core::Json& out, ApiError& error) {
  core::Json payload;
  std::vector<std::pair<std::string, std::string>> headers = {{"x-xbl-contract-version", "3"}};
  if (!client_.get(kProfileUrl, headers, payload, error)) return false;

  core::Json users = core::json_array(payload, "profileUsers");
  if (users.empty()) {
    error.status = 404;
    error.code = "profile_not_found";
    error.message = "Profile not found";
    return false;
  }
  const core::Json& user = users[0];

  std::string gamerscore = first_setting(user, "Gamerscore");
  std::string tenure = first_setting(user, "TenureLevel");

  out = core::Json::object();
  out["xuid"] = core::json_string(user, "id");
  out["gamertag"] = nullable_string(first_setting(user, "Gamertag"));
  out["gamerscore"] = gamerscore.empty() ? core::Json(nullptr) : core::Json(std::stod(gamerscore));
  out["accountTier"] = nullable_string(first_setting(user, "AccountTier"));
  out["motto"] = nullable_string(first_setting(user, "Motto"));
  out["realName"] = nullable_string(first_setting(user, "RealName"));
  out["displayPic"] = nullable_string(first_setting(user, "GameDisplayPicRaw"));
  out["preferredColor"] = nullable_string(first_setting(user, "PreferredColor"));
  out["tenureLevel"] = tenure.empty() ? core::Json(nullptr) : core::Json(std::stod(tenure));
  out["watermarks"] = nullable_string(first_setting(user, "Watermarks"));
  out["location"] = nullable_string(first_setting(user, "Location"));
  out["bio"] = nullable_string(first_setting(user, "Bio"));
  return true;
}

bool Endpoints::titles(core::Json& out, ApiError& error) {
  std::vector<core::Json> titles;
  if (!fetch_titles(titles, error)) return false;
  core::Json mapped = core::Json::array();
  for (const core::Json& title : titles) mapped.push_back(map_title(title));
  out = core::Json::object();
  out["count"] = static_cast<long long>(mapped.size());
  out["titles"] = mapped;
  return true;
}

bool Endpoints::fetch_achievements(const std::string& title_id, bool is_360,
                                   std::vector<core::Json>& out, ApiError& error) {
  std::string xuid = client_.auth_service().xuid();
  if (xuid.empty()) {
    error.status = 400;
    error.code = "missing_xuid";
    error.message = "No XUID is cached. Call GET /api/profile first.";
    return false;
  }
  core::Json payload;
  std::vector<std::pair<std::string, std::string>> headers = {
      {"x-xbl-contract-version", is_360 ? "1" : "2"}};
  std::string url = "https://achievements.xboxlive.com/users/xuid(" + xuid +
                    ")/achievements?titleId=" + core::url_encode(title_id) + "&maxItems=1000";
  if (!client_.get(url, headers, payload, error)) return false;
  core::Json items = core::json_array(payload, "achievements");
  out.clear();
  out.reserve(items.size());
  for (const core::Json& item : items) out.push_back(item);
  return true;
}

bool Endpoints::achievements(int max_titles, core::Json& out, ApiError& error) {
  std::vector<core::Json> titles;
  if (!cached_titles(titles, error)) return false;

  std::sort(titles.begin(), titles.end(), [](const core::Json& left, const core::Json& right) {
    std::string left_time = core::json_string(core::json_object(left, "titleHistory"), "lastTimePlayed");
    std::string right_time =
        core::json_string(core::json_object(right, "titleHistory"), "lastTimePlayed");
    return left_time > right_time;
  });

  if (max_titles < 1) max_titles = 1;
  if (max_titles > 50) max_titles = 50;
  if (static_cast<int>(titles.size()) > max_titles) titles.resize(static_cast<std::size_t>(max_titles));

  struct TitleJob {
    std::string title_id;
    std::string title_name;
    bool is_360 = false;
    std::vector<core::Json> achievements;
    ApiError error;
    bool ok = false;
  };

  std::vector<std::shared_ptr<TitleJob>> jobs;
  jobs.reserve(titles.size());
  for (const core::Json& title : titles) {
    auto job = std::make_shared<TitleJob>();
    job->title_id = core::json_string(title, "titleId");
    job->title_name = core::json_string(title, "name");
    job->is_360 =
        core::json_int(core::json_object(title, "achievement"), "sourceVersion", 0) == 1;
    jobs.push_back(job);
  }

  std::vector<std::future<void>> futures;
  futures.reserve(jobs.size());
  for (const auto& job : jobs) {
    futures.push_back(achievement_pool_.submit([this, job]() {
      job->ok = fetch_achievements(job->title_id, job->is_360, job->achievements, job->error);
    }));
  }
  for (auto& future : futures) {
    try {
      future.get();
    } catch (const std::exception&) {
    }
  }

  core::Json all = core::Json::array();
  for (const auto& job : jobs) {
    if (!job->ok) {
      if (job->error.status == 0) continue;
      error = job->error;
      return false;
    }
    for (const core::Json& achievement : job->achievements) {
      all.push_back(map_achievement(achievement, job->title_name, job->is_360));
      if (all.size() >= 200) break;
    }
    if (all.size() >= 200) break;
  }

  std::vector<core::Json> sorted(all.begin(), all.end());
  std::sort(sorted.begin(), sorted.end(), [](const core::Json& left, const core::Json& right) {
    std::string left_time = core::json_string(left, "timeUnlocked");
    std::string right_time = core::json_string(right, "timeUnlocked");
    return left_time > right_time;
  });

  out = core::Json::object();
  out["count"] = static_cast<long long>(sorted.size());
  out["achievements"] = sorted;
  return true;
}

bool Endpoints::achievements_for_title(const std::string& title_id, core::Json& out,
                                        ApiError& error) {
  std::vector<core::Json> titles;
  if (!cached_titles(titles, error)) return false;

  core::Json title = core::Json::object();
  bool found = false;
  for (const core::Json& candidate : titles) {
    if (core::json_string(candidate, "titleId") == title_id) {
      title = candidate;
      found = true;
      break;
    }
  }
  bool is_360 = found && core::json_int(core::json_object(title, "achievement"), "sourceVersion", 0) == 1;

  std::vector<core::Json> list;
  if (!fetch_achievements(title_id, is_360, list, error)) return false;

  std::string title_name = found ? core::json_string(title, "name") : title_id;
  core::Json mapped = core::Json::array();
  for (const core::Json& achievement : list) mapped.push_back(map_achievement(achievement, title_name, is_360));

  core::Json title_payload = found ? map_title(title) : map_title(core::Json::object());
  if (!found) {
    title_payload["titleId"] = title_id;
    title_payload["name"] = title_name;
  }

  out = core::Json::object();
  out["count"] = static_cast<long long>(mapped.size());
  out["title"] = title_payload;
  out["achievements"] = mapped;
  return true;
}

bool Endpoints::activity(int max_items, core::Json& out, ApiError& error) {
  std::vector<core::Json> titles;
  if (!cached_titles(titles, error)) return false;

  std::vector<core::Json> recent;
  for (const core::Json& title : titles) {
    core::Json history = core::json_object(title, "titleHistory");
    if (core::json_string(history, "lastTimePlayed").empty()) continue;
    recent.push_back(title);
  }
  std::sort(recent.begin(), recent.end(), [](const core::Json& left, const core::Json& right) {
    std::string left_time = core::json_string(core::json_object(left, "titleHistory"), "lastTimePlayed");
    std::string right_time =
        core::json_string(core::json_object(right, "titleHistory"), "lastTimePlayed");
    return left_time > right_time;
  });

  if (max_items < 1) max_items = 1;
  if (max_items > 50) max_items = 50;
  if (static_cast<int>(recent.size()) > max_items) recent.resize(static_cast<std::size_t>(max_items));

  core::Json mapped = core::Json::array();
  for (const core::Json& title : recent) {
    core::Json history = core::json_object(title, "titleHistory");
    core::Json item = core::Json::object();
    item["titleId"] = core::json_string(title, "titleId");
    item["name"] = core::json_string(title, "name");
    item["lastPlayed"] = nullable_string(core::json_string(history, "lastTimePlayed"));
    item["minutesPlayed"] = core::json_int(history, "minutesPlayed", 0);
    mapped.push_back(item);
  }

  out = core::Json::object();
  out["count"] = static_cast<long long>(mapped.size());
  out["titles"] = mapped;
  return true;
}

bool Endpoints::presence(core::Json& out, ApiError& error) {
  core::Json payload;
  std::vector<std::pair<std::string, std::string>> headers = {{"x-xbl-contract-version", "3"}};
  if (!client_.get(kPresenceUrl, headers, payload, error)) return false;

  core::Json devices = core::Json::array();
  if (payload.contains("devices") && payload["devices"].is_array()) {
    for (const core::Json& device : payload["devices"]) {
      core::Json mapped = core::Json::object();
      mapped["type"] = core::json_string(device, "type");
      mapped["state"] = core::json_string(device, "state");
      mapped["presenceText"] = nullable_string(core::json_string(device, "presenceText"));
      mapped["titleIds"] = device.contains("titleIds") ? device["titleIds"] : core::Json(nullptr);
      devices.push_back(mapped);
    }
  }

  out = core::Json::object();
  out["xuid"] = core::json_string(payload, "xuid");
  out["state"] = core::json_string(payload, "state");
  out["presenceText"] = nullable_string(core::json_string(payload, "presenceText"));
  out["lastSeen"] = nullable_string(core::json_string(payload, "lastSeen"));
  out["devices"] = devices;
  return true;
}

bool Endpoints::subscriptions(core::Json& out, ApiError& error) {
  std::string xuid = client_.auth_service().xuid();
  if (xuid.empty()) {
    error.status = 400;
    error.code = "missing_xuid";
    error.message = "No XUID is cached. Call GET /api/profile first.";
    return false;
  }
  core::Json payload;
  std::vector<std::pair<std::string, std::string>> headers = {{"x-xbl-contract-version", "7"}};
  if (!client_.get(format_url(kSubscriptionsUrl, xuid), headers, payload, error)) return false;

  core::Json people = core::json_array(payload, "people");
  if (people.empty()) {
    error.status = 404;
    error.code = "profile_detail_not_found";
    error.message = "No profile detail returned by Xbox Live";
    return false;
  }
  const core::Json& person = people[0];
  core::Json detail = core::json_object(person, "detail");

  std::string modern = core::json_string(person, "uniqueModernGamertag");
  if (modern.empty()) modern = core::json_string(person, "modernGamertag");
  std::string tenure = core::json_string(detail, "tenure");
  core::Json tenure_value = core::Json(nullptr);
  if (!tenure.empty()) {
    try {
      tenure_value = core::Json(std::stod(tenure));
    } catch (...) {
      tenure_value = core::Json(nullptr);
    }
  }

  core::Json presence = core::Json::object();
  presence["state"] = nullable_string(core::json_string(person, "presenceState"));
  presence["text"] = nullable_string(core::json_string(person, "presenceText"));
  presence["lastSeen"] = nullable_string(core::json_string(person, "lastSeenDateTimeUtc"));

  out = core::Json::object();
  out["gamertag"] = core::json_string(person, "gamertag");
  out["xuid"] = core::json_string(person, "xuid");
  out["modernGamertag"] = nullable_string(modern);
  out["accountTier"] = nullable_string(core::json_string(detail, "accountTier"));
  out["gamePass"] = core::json_bool(detail, "hasGamePass", false);
  out["followers"] = core::json_int(detail, "followerCount", 0);
  out["following"] = core::json_int(detail, "followingCount", 0);
  out["friends"] = core::json_int(detail, "friendCount", 0);
  out["tenure"] = tenure_value;
  out["bio"] = nullable_string(core::json_string(detail, "bio"));
  out["location"] = nullable_string(core::json_string(detail, "location"));
  out["badges"] = detail.contains("badges") ? detail["badges"] : core::Json::array();
  out["rep"] = person.contains("xboxOneRep") ? person["xboxOneRep"] : core::Json(nullptr);
  out["isVerified"] = core::json_bool(detail, "isVerified", false);
  out["isBroadcasting"] = core::json_bool(person, "isBroadcasting", false);
  out["presence"] = presence;
  return true;
}

}  // namespace xbox::api
