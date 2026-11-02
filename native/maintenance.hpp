#pragma once
#include "windows.hpp"
#include <atomic>
namespace hr {
struct MaintenancePlan {bool enabled=false,cleanup=false,backups=false,on_battery=false,background=false;unsigned interval_minutes=1440,idle_minutes=10;};
MaintenancePlan maintenance_plan(const Store& store);
void save_maintenance_plan(Store& store,const MaintenancePlan& plan);
std::string maintenance_gate(const MaintenancePlan& plan,unsigned long long now,unsigned long long last,unsigned idle_seconds,bool gaming,bool battery);
unsigned idle_seconds();
bool gaming_now(const Store& store);
bool on_battery();
std::string maintenance_status();
std::string run_maintenance(const Store& store,std::atomic<bool>& cancel,bool manual=false);
void schedule_maintenance(const MaintenancePlan& plan,bool enable,const std::wstring& test_name=L"");
}
