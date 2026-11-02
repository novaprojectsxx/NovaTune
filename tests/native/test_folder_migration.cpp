#include "windows.hpp"
#include "capture.hpp"
#include "commands.hpp"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
int wmain(int argc,wchar_t** argv){
 try{
  auto base=std::filesystem::temp_directory_path()/(L"NovaTuneFolderTest-"+std::to_wstring(GetCurrentProcessId()));
  std::filesystem::create_directories(base/L"Headroom");std::ofstream(base/L"Headroom"/L"kept.txt")<<"preserve";
  auto next=hr::migrate_app_folder(base);if(next.filename()!=L"NovaTune"||!std::filesystem::exists(next/L"kept.txt")||std::filesystem::exists(base/L"Headroom"))throw std::runtime_error("Legacy folder migration failed");
  std::filesystem::create_directories(base/L"Headroom");std::ofstream(base/L"Headroom"/L"conflict.txt")<<"preserve";
  hr::migrate_app_folder(base);if(!std::filesystem::exists(base/L"Headroom"/L"conflict.txt")||!std::filesystem::exists(next/L"kept.txt"))throw std::runtime_error("Existing destination was overwritten");
  std::filesystem::remove_all(base);std::cout<<"Isolated migration and conflict-preservation checks passed\n";
  if(argc>1&&std::wstring(argv[1])==L"--migrate-live"){
   auto app=FindWindowW(L"HeadroomNativeWindow",nullptr);if(app){PostMessageW(app,WM_COMMAND,hr::ui::Quit,0);for(int n=0;n<300&&IsWindow(app);++n)Sleep(100);if(IsWindow(app))throw std::runtime_error("Quit the running app before migration");}
   auto captures=hr::capture_folder();auto data=hr::data_dir();hr::Store store;
   if(captures.filename()!=L"NovaTune"||data.filename()!=L"NovaTune")throw std::runtime_error("Live folder names incorrect");
   std::cout<<"Settings loaded: "<<store.rows.size()<<" records\n"<<hr::utf8(data.wstring())<<"\n"<<hr::utf8(captures.wstring())<<"\n";
  }
  return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
