#pragma once

#include <mutex>
#include <string>
#include <vector>

#include "core/executor.h"
#include "core/json.h"
#include "xbox/client.h"

namespace xbox::api {

struct TitleRecord {
  std::string title_id;
  std::string name;
  std::string pfn;
  std::string store_id;
  std::string type;
  std::string icon;
  bool has_achievements = false;
  int current_achievements = 0;
  int total_achievements = 0;
  int current_gamerscore = 0;
  int total_gamerscore = 0;
  bool is_360 = false;
  double completion = 0.0;
  long long minutes_played = 0;
  std::string last_time_played;
};

struct AchievementRecord {
  std::string id;
  std::string name;
  std::string description;
  std::string locked_description;
  std::string progress_state;
  int progress = 0;
  bool unlocked = false;
  std::string time_unlocked;
  bool is_secret = false;
  std::string type;
  int gamerscore = 0;
  std::string icon;
  double rarity = -1.0;
  std::string title;
};

class Endpoints {
 public:
  explicit Endpoints(XboxClient& client);

  bool profile(core::Json& out, ApiError& error);
  bool titles(core::Json& out, ApiError& error);
  bool achievements(int max_titles, core::Json& out, ApiError& error);
  bool achievements_for_title(const std::string& title_id, core::Json& out, ApiError& error);
  bool activity(int max_items, core::Json& out, ApiError& error);
  bool presence(core::Json& out, ApiError& error);
  bool subscriptions(core::Json& out, ApiError& error);

  core::Json map_title(const core::Json& title) const;
  core::Json map_achievement(const core::Json& achievement, const std::string& title_name,
                             bool is_360) const;

 private:
  bool fetch_titles(std::vector<core::Json>& titles, ApiError& error);
  bool fetch_achievements(const std::string& title_id, bool is_360, std::vector<core::Json>& out,
                          ApiError& error);
  bool cached_titles(std::vector<core::Json>& titles, ApiError& error);

  XboxClient& client_;
  std::mutex cache_mutex_;
  std::vector<core::Json> title_cache_;
  long long title_cache_expires_at = 0;
  core::Executor achievement_pool_{6};
};

}  // namespace xbox::api
