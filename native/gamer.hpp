#pragma once
#include "windows.hpp"
#include <atomic>
#include <functional>
namespace hr {
struct GamerTool {std::string id,title,category,description,first,second;bool needs_review=false;};
std::vector<GamerTool> gamer_tools();
Records discover_games(const std::filesystem::path& steam_root={});
std::string gamer_report(const std::string& id,const std::string& first,const std::string& second,std::atomic<bool>& cancel);
std::string gamer_action(const std::string& id,const std::string& first,const std::string& second,std::atomic<bool>& cancel);
std::filesystem::path snapshot_folder(const std::filesystem::path& source,const std::string& label,std::atomic<bool>& cancel,bool dedicated_mod=false,bool strict=false);
std::string restore_snapshot(const std::filesystem::path& snapshot,const std::filesystem::path& target,std::atomic<bool>& cancel);
struct ModPlan {std::filesystem::path snapshot,target;std::string fingerprint;Records changes;};
void validate_backup_folder(const std::filesystem::path& root);
void validate_mod_folder(const std::filesystem::path& root);
ModPlan plan_mod_switch(const std::filesystem::path& snapshot);
std::string apply_mod_switch(const ModPlan& plan,std::atomic<bool>& cancel,const std::function<bool()>& can_write={});
std::string mod_switch_history();
Records automatic_backup_versions(const std::string& game);
std::string automatic_backup(const std::string& game,const std::filesystem::path& folder,unsigned retention,std::atomic<bool>& cancel);
std::string readiness(const std::string& game="");
std::string compatibility();
}
