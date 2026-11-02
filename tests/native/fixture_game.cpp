#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
namespace {
std::filesystem::path root;
LRESULT CALLBACK proc(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_CREATE){CreateWindowW(L"STATIC",L"Owned Headroom integration-test fixture.\nNo real game performance is simulated.",WS_CHILD|WS_VISIBLE,16,18,350,60,window,nullptr,GetModuleHandleW(nullptr),nullptr);SetTimer(window,1,3000,nullptr);SetTimer(window,2,10000,nullptr);return 0;}
    if(message==WM_TIMER){if(w==1){KillTimer(window,1);std::ifstream marker(root/L"fixture-owned.dat");std::string value;std::getline(marker,value);wchar_t temporary[32768]{};GetTempPathW(32768,temporary);bool owned=root.filename().wstring().rfind(L"Headroom.Test.",0)==0&&std::filesystem::weakly_canonical(root.parent_path())==std::filesystem::weakly_canonical(temporary);if(owned&&value=="Headroom.Test.Game"){std::ofstream save(root/L"Saves/save.dat",std::ios::binary|std::ios::trunc);save<<"after-game";}}else DestroyWindow(window);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}return DefWindowProcW(window,message,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);root=std::filesystem::path(path).parent_path();WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=instance;cls.lpszClassName=L"HeadroomOwnedGameFixture";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&cls);HWND window=CreateWindowW(cls.lpszClassName,L"Headroom owned game fixture",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,420,160,nullptr,nullptr,instance,nullptr);ShowWindow(window,SW_SHOW);MSG message;while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return 0;}
