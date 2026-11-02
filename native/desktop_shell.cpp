#include "desktop.hpp"
#include <shellapi.h>
#include <vector>
namespace hr {
int desktop_shell_guard(int argc,wchar_t** argv){
    if(argc!=4)return 2;
    DWORD pid=0;try{pid=static_cast<DWORD>(std::stoul(argv[2]));}catch(...){return 2;}
    DWORD session=0,own=0;if(!ProcessIdToSessionId(pid,&session)||!ProcessIdToSessionId(GetCurrentProcessId(),&own)||session!=own)return 2;
    HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);if(!process)return 3;
    auto base=L"Local\\HeadroomShell."+std::to_wstring(pid)+L"."+argv[3];
    HANDLE stop=OpenEventW(SYNCHRONIZE,FALSE,(base+L".stop").c_str()),ready=OpenEventW(EVENT_MODIFY_STATE,FALSE,(base+L".ready").c_str());
    HANDLE pause=OpenEventW(SYNCHRONIZE,FALSE,(base+L".pause").c_str());
    HANDLE mutex=CreateMutexW(nullptr,TRUE,L"Local\\HeadroomTaskbarReplacement");bool exclusive=mutex&&GetLastError()!=ERROR_ALREADY_EXISTS;
    if(!stop||!ready||!exclusive){if(stop)CloseHandle(stop);if(ready)CloseHandle(ready);if(mutex)CloseHandle(mutex);CloseHandle(process);if(pause)CloseHandle(pause);return 4;}
    APPBARDATA bar{};bar.cbSize=sizeof bar;bar.hWnd=FindWindowW(L"Shell_TrayWnd",nullptr);auto state=SHAppBarMessage(ABM_GETSTATE,&bar);
    struct Saved{HWND window;DWORD pid;bool visible;};std::vector<Saved> windows;
    auto hide=[&]{EnumWindows([](HWND h,LPARAM data)->BOOL{wchar_t cls[80]{};GetClassNameW(h,cls,80);if(wcscmp(cls,L"Shell_TrayWnd")&&wcscmp(cls,L"Shell_SecondaryTrayWnd"))return TRUE;auto& saved=*reinterpret_cast<std::vector<Saved>*>(data);DWORD pid=0;GetWindowThreadProcessId(h,&pid);bool found=false;for(auto& row:saved)if(row.window==h&&row.pid==pid)found=true;if(!found)saved.push_back({h,pid,IsWindowVisible(h)!=FALSE});ShowWindow(h,SW_HIDE);return TRUE;},reinterpret_cast<LPARAM>(&windows));};
    bar.lParam=state|ABS_AUTOHIDE;SHAppBarMessage(ABM_SETSTATE,&bar);hide();SetEvent(ready);
    HANDLE waits[]={process,stop};while(WaitForMultipleObjects(2,waits,FALSE,200)==WAIT_TIMEOUT){if(pause&&WaitForSingleObject(pause,0)==WAIT_OBJECT_0){for(auto& row:windows)if(IsWindow(row.window))ShowWindow(row.window,SW_SHOWNOACTIVATE);}else hide();}
    bar.hWnd=FindWindowW(L"Shell_TrayWnd",nullptr);bar.lParam=state;SHAppBarMessage(ABM_SETSTATE,&bar);
    for(auto& row:windows){DWORD owner=0;GetWindowThreadProcessId(row.window,&owner);if(owner==row.pid&&IsWindow(row.window))ShowWindow(row.window,row.visible?SW_SHOWNOACTIVATE:SW_HIDE);}
    CloseHandle(stop);CloseHandle(ready);if(pause)CloseHandle(pause);CloseHandle(process);ReleaseMutex(mutex);CloseHandle(mutex);return 0;
}
}
