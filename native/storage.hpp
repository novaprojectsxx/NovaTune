#pragma once
#include "tools.hpp"
#include <atomic>
namespace hr {
struct CacheTarget {std::string id,label,browser,warning;std::filesystem::path path;unsigned age_hours=24;bool single=false;};
struct StorageFile {std::filesystem::path path,root;std::string category,browser;unsigned age_hours=0;unsigned long long bytes=0,modified=0,id=0;DWORD volume=0;};
std::vector<CacheTarget> cache_targets();
std::vector<StorageFile> scan_caches(const std::vector<CacheTarget>& selected,std::atomic<bool>& cancel);
CleanupResult clean_caches(const std::vector<StorageFile>& files);
struct FolderUse {std::filesystem::path path;unsigned long long bytes=0,files=0;};
struct DuplicateSet {std::vector<StorageFile> files;std::string digest;};
struct StorageAnalysis {std::filesystem::path root;unsigned long long bytes=0,files=0;std::vector<FolderUse> folders;std::vector<StorageFile> largest;std::vector<DuplicateSet> duplicates;bool capped=false;};
StorageAnalysis analyze_storage(const std::filesystem::path& root,bool duplicates,std::atomic<bool>& cancel);
CleanupResult recycle_duplicates(HWND owner,const std::vector<std::pair<StorageFile,StorageFile>>& files);
void reveal(const std::filesystem::path& path);
}
