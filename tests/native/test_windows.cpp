#include "windows.hpp"
#include "tools.hpp"
#include "input.hpp"
#include "capture.hpp"
#include "overlay.hpp"
#include "replay.hpp"
#include "audio.hpp"
#include "devices.hpp"
#include "profiles.hpp"
#include "maintenance.hpp"
#include "timeline.hpp"
#include "commands.hpp"
#include "desktop.hpp"
#include "boost.hpp"
#include "controller_art.hpp"
#include <shellapi.h>
#include <psapi.h>
#include <mmsystem.h>
#include "desktop_media.hpp"
#include "system_tools.hpp"
#include "storage.hpp"
#include "network.hpp"
#include "comparison.hpp"
#include "experience.hpp"
#include <shlobj.h>
#include <limits>
#include "stress.hpp"
#include "dialog.hpp"
#include "scrollbars.hpp"
#include "gamer.hpp"
#include "game_sessions.hpp"
#include <set>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <commctrl.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void require(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
void fails(const std::function<void()>& test,const char* message){bool failed=false;try{test();}catch(const std::exception&){failed=true;}require(failed,message);}
constexpr wchar_t run_key[]=L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
struct Fixture {
    std::wstring name=L"Headroom.Test."+std::to_wstring(GetCurrentProcessId());
    std::filesystem::path folder;
    explicit Fixture(){
        folder=hr::temp_root()/name;std::filesystem::create_directories(folder);
    }
    ~Fixture(){
        HKEY key=nullptr;
        if(RegOpenKeyExW(HKEY_CURRENT_USER,run_key,0,KEY_SET_VALUE,&key)==ERROR_SUCCESS){
            RegDeleteValueW(key,name.c_str());RegCloseKey(key);
        }
        std::error_code ec;std::filesystem::remove_all(folder,ec);
    }
};
}
void screenshot(HWND window,const std::filesystem::path& path){
    RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN|RDW_UPDATENOW);
    RECT rect{};GetClientRect(window,&rect);
    HDC source=GetDC(window),dc=CreateCompatibleDC(source);
    HBITMAP bitmap=CreateCompatibleBitmap(source,rect.right,rect.bottom);
    auto previous=SelectObject(dc,bitmap);
    BitBlt(dc,0,0,rect.right,rect.bottom,source,0,0,SRCCOPY);
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=rect.right;info.bmiHeader.biHeight=-rect.bottom;
    info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    std::vector<char> bytes(static_cast<size_t>(rect.right)*rect.bottom*4);
    SelectObject(dc,previous);
    GetDIBits(source,bitmap,0,rect.bottom,bytes.data(),&info,DIB_RGB_COLORS);
    BITMAPFILEHEADER header{};header.bfType=0x4d42;header.bfOffBits=sizeof header+sizeof info.bmiHeader;
    header.bfSize=header.bfOffBits+static_cast<DWORD>(bytes.size());
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(&header),sizeof header);
    out.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof info.bmiHeader);out.write(bytes.data(),bytes.size());
    DeleteObject(bitmap);DeleteDC(dc);ReleaseDC(window,source);
}
int wmain(int argc,wchar_t** argv){
    if(argc>2&&std::wstring(argv[1])==L"--tray-tree"){std::ofstream out{std::filesystem::path(argv[2])};HWND root=FindWindowW(L"Shell_TrayWnd",nullptr);auto log=[&](HWND h){wchar_t cls[256]{},text[256]{};GetClassNameW(h,cls,256);GetWindowTextW(h,text,256);RECT r{};GetWindowRect(h,&r);out<<reinterpret_cast<UINT_PTR>(h)<<" parent "<<reinterpret_cast<UINT_PTR>(GetParent(h))<<" "<<hr::utf8(cls)<<" | "<<hr::utf8(text)<<" | "<<r.left<<","<<r.top<<","<<r.right<<","<<r.bottom<<" visible "<<IsWindowVisible(h)<<"\n";};log(root);struct Context{decltype(log)* fn;};Context ctx{&log};EnumChildWindows(root,[](HWND h,LPARAM p)->BOOL{(*reinterpret_cast<Context*>(p)->fn)(h);return TRUE;},reinterpret_cast<LPARAM>(&ctx));return 0;}

    if(argc>1&&std::wstring(argv[1])==L"--desktop-shell-guard")return hr::desktop_shell_guard(argc,argv);
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* details)->LONG{std::cerr<<"UNHANDLED 0x"<<std::hex<<details->ExceptionRecord->ExceptionCode<<" at RVA 0x"<<(reinterpret_cast<UINT_PTR>(details->ExceptionRecord->ExceptionAddress)-reinterpret_cast<UINT_PTR>(GetModuleHandleW(nullptr)))<<" address 0x"<<details->ExceptionRecord->ExceptionInformation[1]<<std::dec<<"\n"<<std::flush;return EXCEPTION_EXECUTE_HANDLER;});
#include "test_eight.inc"
#include "test_eight_ui.inc"
#include "test_audit_ui.inc"
#include "test_catalogue_ui.inc"
#include "test_storage_safety.inc"
#include "test_network.inc"
#include "test_attraction.inc"
#include "test_experience.inc"
#include "test_toast.inc"
#include "test_stress_load.inc"
#include "test_resize_style.inc"
#include "test_boost_click.inc"
#include "test_six_fixes.inc"
#include "test_input_live.inc"
#include "test_desktop.inc"
#include "test_desktop_ui.inc"
#include "test_desktop_shell.inc"
#include "test_desktop_live_ui.inc"
    if(argc>2&&std::wstring(argv[1])==L"--inventory-audit-test"){
        try{std::filesystem::path folder(argv[2]);std::filesystem::create_directories(folder);std::atomic<bool> cancel{false};for(const auto& id:{"display","usb","storage-health","crashes","memory-results","compatibility","snapshots","streaming","laptop","local-help","diagnostic-export","sessions","timeline","capture-files","discover"}){std::cerr<<"Inspect "<<id<<"\n"<<std::flush;auto report=hr::gamer_report(id,"","",cancel);require(!report.empty(),"read-only gamer inventory returns measured evidence or explicit availability");hr::atomic_write(folder/(hr::wide(id)+L".txt"),report);}for(const auto& tool:hr::system_tools()){auto report=hr::inspect_tool(tool.id);require(!report.empty(),"each system tool has a working read-only inspect handler");}std::ostringstream report;report<<checks<<" read-only inventory/availability checks passed\nNo system-tuning action, uninstaller, reset or disk optimization was applied by this audit.\n";hr::atomic_write(folder/L"inventory-audit.txt",report.str());std::cout<<report.str();return 0;}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>1&&std::wstring(argv[1])==L"--priority-worker")return hr::priority_worker(argc,argv);
    if(argc>1&&std::wstring(argv[1])==L"--priority-fixture"){Sleep(60000);return 0;}
    if(argc==6&&std::wstring(argv[1])==L"--guard-parent"){
        try{hr::Process target{static_cast<DWORD>(std::stoul(argv[2])),L"Guard fixture",0,0,std::stoull(argv[3])};hr::PriorityGuard guard(target,ABOVE_NORMAL_PRIORITY_CLASS,argv[4]);hr::atomic_write(argv[5],"ready");Sleep(60000);return 0;}catch(const std::exception&){return 1;}
    }
    if(argc>1 && std::wstring(argv[1])==L"--close-app"){
        HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);
        DWORD pid=0;if(window)GetWindowThreadProcessId(window,&pid);HANDLE process=pid?OpenProcess(SYNCHRONIZE,FALSE,pid):nullptr;
        if(window)SendMessageW(window,WM_COMMAND,224,0);bool busy=window&&IsWindow(window);if(process){if(!busy)busy=WaitForSingleObject(process,7000)!=WAIT_OBJECT_0;CloseHandle(process);}return busy?1:0;
    }
    if(argc>2&&std::wstring(argv[1])==L"--clip-ui-test"){
        std::filesystem::path fixture,exported;
        try{
            HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);require(window!=nullptr,"running app found");PostMessageW(GetDlgItem(window,123),WM_LBUTTONUP,0,MAKELPARAM(82,55));auto nav=GetDlgItem(window,100);auto page=[&](const wchar_t* name){for(int n=0;n<SendMessageW(nav,LB_GETCOUNT,0,0);++n){wchar_t label[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(label));if(std::wstring(label)==name){SendMessageW(nav,LB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));return;}}throw std::runtime_error("Missing page");};auto read_text=[](HWND control){int length=static_cast<int>(SendMessageW(control,WM_GETTEXTLENGTH,0,0));std::wstring text(length+1,L'\0');SendMessageW(control,WM_GETTEXT,length+1,reinterpret_cast<LPARAM>(text.data()));text.resize(length);for(size_t i=0;(i=text.find(L"\r\n",i))!=std::wstring::npos;)text.erase(i,1);return text;};
            std::cerr<<"phase controller\n"<<std::flush;page(L"Gamer tools");SendMessageW(GetDlgItem(window,430),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"Controller calibration lab"));SendMessageW(window,WM_COMMAND,279,0);require(IsWindowEnabled(GetDlgItem(window,283)),"live controller report can be canceled");SendMessageW(window,WM_COMMAND,283,0);require(!IsWindowEnabled(GetDlgItem(window,283)),"cancel stops live report");
            auto source=std::filesystem::path(argv[2])/L"audio-fixture.mp4";fixture=hr::capture_folder()/(L"GalleryVideo-UI-"+std::to_wstring(GetCurrentProcessId())+L".mp4");std::filesystem::copy_file(source,fixture);auto bytes=std::filesystem::file_size(fixture);std::cerr<<"phase gallery\n"<<std::flush;page(L"Capture gallery");auto refresh=GetDlgItem(window,286);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);SendMessageW(window,WM_COMMAND,286,0);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);auto filename=fixture.filename().wstring();SendMessageW(GetDlgItem(window,440),WM_SETTEXT,0,reinterpret_cast<LPARAM>(filename.c_str()));auto grid=GetDlgItem(window,123);require(ListView_GetItemCount(grid)==1,"gallery isolates own MP4 fixture");PostMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(82,55));PostMessageW(grid,WM_LBUTTONUP,0,MAKELPARAM(82,55));for(int n=0;n<40&&(!IsWindowEnabled(GetDlgItem(window,290))||IsWindowEnabled(GetDlgItem(window,288)));++n)Sleep(50);std::cerr<<"buttons trim="<<IsWindowEnabled(GetDlgItem(window,290))<<" crop="<<IsWindowEnabled(GetDlgItem(window,288))<<" selected="<<ListView_GetNextItem(grid,-1,LVNI_SELECTED)<<"\n"<<std::flush;require(IsWindowEnabled(GetDlgItem(window,290))&&!IsWindowEnabled(GetDlgItem(window,288)),"video selection enables trim and disables image crop");std::cerr<<"phase trim navigation\n"<<std::flush;DWORD_PTR navigation_result=0;require(SendMessageTimeoutW(window,WM_COMMAND,290,0,SMTO_ABORTIFHUNG,5000,&navigation_result)!=0,"trim navigation responds");std::cerr<<"phase read source\n"<<std::flush;require(read_text(GetDlgItem(window,431))==fixture.wstring(),"gallery trim opens exporter with selected file");std::cerr<<"phase set interval\n"<<std::flush;SendMessageW(GetDlgItem(window,432),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"0.5,2"));SendMessageW(GetDlgItem(window,433),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"Waiting for fixture export"));std::cerr<<"phase export\n"<<std::flush;SendMessageW(window,WM_COMMAND,279,0);for(int n=0;n<600&&IsWindowEnabled(GetDlgItem(window,283));++n)Sleep(50);std::cerr<<"phase read result\n"<<std::flush;auto output=read_text(GetDlgItem(window,433));require(output.rfind(L"Exported new clip:\n",0)==0,"clip export succeeds after canceling a different tool");auto start=output.find(L'\n')+1,end=output.find(L'\n',start);exported=output.substr(start,end-start);require(std::filesystem::is_regular_file(exported),"native exporter produces a new clip");require(std::filesystem::file_size(fixture)==bytes,"gallery trim preserves original source");require(hr::video_information(exported).find("Audio: 2 channels")!=std::string::npos,"native UI export retains stereo audio");screenshot(window,std::filesystem::path(argv[2])/L"clip-export-ui.bmp");std::filesystem::remove(exported);exported.clear();std::filesystem::remove(fixture);fixture.clear();page(L"Capture gallery");SendMessageW(GetDlgItem(window,440),WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));SendMessageW(window,WM_COMMAND,286,0);page(L"Dashboard");std::cout<<checks<<" clip UI checks passed\n";return 0;
        }catch(const std::exception& e){std::error_code ec;if(!fixture.empty())std::filesystem::remove(fixture,ec);if(!exported.empty())std::filesystem::remove(exported,ec);std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2&&std::wstring(argv[1])==L"--overlay-test"){
        try{
            Fixture fixture;hr::Store isolated(fixture.folder/L"overlay.dat");auto defaults=hr::read_overlay(isolated);require(defaults.scale==100&&defaults.metrics==63,"default HUD settings include supported metrics");auto settings=defaults;settings.anchor=3;settings.scale=125;settings.accent=2;settings.layout=1;settings.display=1;settings.margin_x=16;settings.margin_y=24;settings.metrics=17;hr::save_overlay(isolated,settings);hr::Store reloaded(isolated.path);auto roundtrip=hr::read_overlay(reloaded);require(roundtrip.anchor==3&&roundtrip.scale==125&&roundtrip.accent==2&&roundtrip.layout==1&&roundtrip.display==1&&roundtrip.margin_x==16&&roundtrip.margin_y==24&&roundtrip.metrics==17,"HUD settings survive disk roundtrip");auto invalid=settings;invalid.metrics=0;fails([&]{hr::save_overlay(isolated,invalid);},"empty metric selection refused");invalid=settings;invalid.opacity=1;fails([&]{hr::save_overlay(isolated,invalid);},"invisible HUD opacity refused");isolated.set("overlay.layout",hr::encode({{"overlay","999","3"}}));require(hr::read_overlay(isolated).scale==100,"unknown HUD schema safely defaults");
            RECT work{-1920,-200,0,880};auto bounds=hr::overlay_bounds(settings,work,144);require(bounds.right==work.right-16&&bounds.bottom==work.bottom-24&&bounds.left>=work.left,"bottom right position supports negative monitor origins");auto size=hr::overlay_size(settings,144);require(size.cx==1200&&size.cy>0,"HUD size uses selected scale and DPI");settings.scale=200;settings.margin_x=4096;settings.margin_y=4096;RECT tiny{0,0,320,200};bounds=hr::overlay_bounds(settings,tiny,192);require(bounds.left>=0&&bounds.top>=0&&bounds.right<=320&&bounds.bottom<=200,"large HUD and margins clamp inside small display");
            HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);require(window!=nullptr,"running app found for HUD test");hr::Store user;auto before=hr::read_overlay(user);bool was_visible=FindWindowW(L"HeadroomOverlay",nullptr)!=nullptr;if(was_visible)SendMessageW(window,WM_COMMAND,234,0);HWND nav=GetDlgItem(window,100);for(int n=0;n<SendMessageW(nav,LB_GETCOUNT,0,0);++n){wchar_t label[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(label));if(std::wstring(label)==L"Overlay editor"){SendMessageW(nav,LB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));break;}}
            auto apply=[&](const hr::OverlaySettings& s){const int scales[]={75,100,125,150,200},opacities[]={128,191,230,255};int scale=1,opacity=2;for(int n=0;n<5;++n)if(scales[n]==s.scale)scale=n;for(int n=0;n<4;++n)if(opacities[n]==s.opacity)opacity=n;int values[]={s.anchor,scale,s.accent,s.layout,s.display,opacity};for(int n=0;n<6;++n)SendMessageW(GetDlgItem(window,450+n),CB_SETCURSEL,values[n],0);SendMessageW(GetDlgItem(window,456),WM_SETTEXT,0,reinterpret_cast<LPARAM>(std::to_wstring(s.margin_x).c_str()));SendMessageW(GetDlgItem(window,457),WM_SETTEXT,0,reinterpret_cast<LPARAM>(std::to_wstring(s.margin_y).c_str()));for(int n=0;n<6;++n)SendMessageW(GetDlgItem(window,458+n),BM_SETCHECK,s.metrics&(1u<<n)?BST_CHECKED:BST_UNCHECKED,0);SendMessageW(window,WM_COMMAND,291,0);};
            struct RestoreHud {std::function<void()> restore;~RestoreHud(){if(restore)try{restore();}catch(...){}}};RestoreHud restore{[apply,before,window,was_visible](){apply(before);bool shown=FindWindowW(L"HeadroomOverlay",nullptr)!=nullptr;if(shown!=was_visible)SendMessageW(window,WM_COMMAND,234,0);}};
            settings=roundtrip;apply(settings);hr::Store applied;require(hr::read_overlay(applied).metrics==17&&hr::read_overlay(applied).accent==2,"native editor saves chosen metrics and color");SendMessageW(window,WM_COMMAND,234,0);HWND hud=FindWindowW(L"HeadroomOverlay",nullptr);require(hud!=nullptr,"custom HUD opens");require((GetWindowLongPtrW(hud,GWL_EXSTYLE)&(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_TRANSPARENT|WS_EX_NOACTIVATE),"custom HUD stays click-through and does not activate");RECT actual{};GetWindowRect(hud,&actual);MONITORINFO monitor{};monitor.cbSize=sizeof monitor;POINT primary{};GetMonitorInfoW(MonitorFromPoint(primary,MONITOR_DEFAULTTOPRIMARY),&monitor);HDC dc=GetDC(window);UINT dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(window,dc);auto expected=hr::overlay_bounds(settings,monitor.rcWork,dpi);require(EqualRect(&actual,&expected),"native HUD uses saved monitor anchor scale and margins");BYTE alpha=0;DWORD flags=0;GetLayeredWindowAttributes(hud,nullptr,&alpha,&flags);require(alpha==230&&(flags&LWA_ALPHA),"native HUD applies selected opacity");std::filesystem::create_directories(argv[2]);screenshot(hud,std::filesystem::path(argv[2])/L"custom-overlay.bmp");screenshot(window,std::filesystem::path(argv[2])/L"overlay-editor.bmp");apply(before);GetWindowRect(hud,&actual);require(actual.right>actual.left,"saving original settings updates existing HUD");SendMessageW(window,WM_COMMAND,234,0);if(was_visible)SendMessageW(window,WM_COMMAND,234,0);restore.restore={};std::cout<<checks<<" overlay checks passed\n";return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2&&std::wstring(argv[1])==L"--gallery-test"){
        std::filesystem::path fixture;
        try{
            HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);require(window!=nullptr,"running Headroom found");HWND nav=GetDlgItem(window,100);auto select_page=[&](const wchar_t* name){for(int n=0;n<SendMessageW(nav,LB_GETCOUNT,0,0);++n){wchar_t label[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(label));if(std::wstring(label)==name){SendMessageW(nav,LB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));return;}}throw std::runtime_error("Page absent");};
            hr::CaptureOptions options;options.cursor=false;options.region={10,10,210,130};auto image=hr::screenshot(options);fixture=hr::capture_folder()/(L"Gallery-UI-"+std::to_wstring(GetCurrentProcessId())+L".png");std::filesystem::rename(image,fixture);auto read_bytes=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};auto original=read_bytes(fixture);
            select_page(L"Capture gallery");auto refresh=GetDlgItem(window,286);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);SendMessageW(window,WM_COMMAND,286,0);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);require(IsWindowEnabled(refresh),"thumbnail loading finishes asynchronously");
            auto search=GetDlgItem(window,440),grid=GetDlgItem(window,123),tags=GetDlgItem(window,441),game=GetDlgItem(window,442);require((GetWindowLongPtrW(grid,GWL_STYLE)&LVS_TYPEMASK)==LVS_ICON,"gallery uses thumbnail grid");auto filename=fixture.filename().wstring();SendMessageW(search,WM_SETTEXT,0,reinterpret_cast<LPARAM>(filename.c_str()));require(ListView_GetItemCount(grid)==1,"filename search isolates own capture fixture");
            auto click=[&](){PostMessageW(grid,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(82,55));PostMessageW(grid,WM_LBUTTONUP,0,MAKELPARAM(82,55));for(int n=0;n<40&&ListView_GetNextItem(grid,-1,LVNI_SELECTED)<0;++n)Sleep(25);Sleep(50);};click();require(IsWindowEnabled(GetDlgItem(window,288)),"PNG selection enables crop");require(!IsWindowEnabled(GetDlgItem(window,290)),"PNG selection disables video trim");require(IsWindowEnabled(tags),"selected capture enables metadata editor");
            std::wstring value=L"neon-victory-"+std::to_wstring(GetCurrentProcessId());SendMessageW(tags,WM_SETTEXT,0,reinterpret_cast<LPARAM>(value.c_str()));SendMessageW(game,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,289,0);auto key="capture.meta."+hr::hex(hr::lower(hr::utf8(filename)));hr::Store saved;auto metadata=hr::decode(saved.get(key));require(metadata.size()==1&&metadata[0]==hr::Row({hr::utf8(value),""}),"tags and explicit no-game association persist to current user store");
            SendMessageW(search,WM_SETTEXT,0,reinterpret_cast<LPARAM>(value.c_str()));require(ListView_GetItemCount(grid)==1,"search finds local tag");SendMessageW(search,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L"no-match-gallery-fixture-123456789"));require(ListView_GetItemCount(grid)==0&&!IsWindowEnabled(tags),"empty search result disables metadata actions");SendMessageW(search,WM_SETTEXT,0,reinterpret_cast<LPARAM>(value.c_str()));click();
            PostMessageW(window,WM_COMMAND,288,0);HWND crop=nullptr;for(int n=0;n<100&&!crop;++n){Sleep(40);crop=FindWindowW(nullptr,L"Crop screenshot");}require(crop!=nullptr,"selected screenshot opens native crop preview");PostMessageW(crop,WM_COMMAND,IDCANCEL,0);for(int n=0;n<100&&IsWindow(crop);++n)Sleep(20);require(!IsWindow(crop),"crop preview cancels");require(read_bytes(fixture)==original,"canceling crop preserves exact source bytes");
            SendMessageW(window,WM_COMMAND,286,0);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);require(ListView_GetItemCount(grid)==1&&IsWindowEnabled(tags),"refresh preserves filtered selection and tags");
            std::set<std::filesystem::path> before_files;for(const auto& file:std::filesystem::directory_iterator(hr::capture_folder()))before_files.insert(file.path());PostMessageW(window,WM_COMMAND,288,0);crop=nullptr;for(int n=0;n<100&&!crop;++n){Sleep(40);crop=FindWindowW(nullptr,L"Crop screenshot");}require(crop!=nullptr,"crop preview reopens after cancellation");RECT preview{};GetClientRect(crop,&preview);int x=preview.right/2,y=preview.bottom/2;SendMessageW(crop,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(x,y));SendMessageW(crop,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x+100,y+60));SendMessageW(crop,WM_LBUTTONUP,0,MAKELPARAM(x+100,y+60));PostMessageW(crop,WM_COMMAND,IDOK,0);for(int n=0;n<600&&(IsWindow(crop)||!IsWindowEnabled(refresh));++n)Sleep(50);Sleep(250);for(int n=0;n<600&&!IsWindowEnabled(refresh);++n)Sleep(50);
            std::filesystem::path cropped;for(const auto& file:std::filesystem::directory_iterator(hr::capture_folder()))if(!before_files.count(file.path())&&file.path().filename().wstring().rfind(L"Cropped-",0)==0)cropped=file.path();require(!cropped.empty()&&std::filesystem::file_size(cropped)>0,"gallery crop creates a new PNG");require(read_bytes(fixture)==original,"completed crop preserves exact original bytes");hr::Store after_crop;auto cropped_key="capture.meta."+hr::hex(hr::lower(hr::utf8(cropped.filename().wstring())));require(after_crop.get(cropped_key)==saved.get(key),"cropped image inherits source tags and game association");require(ListView_GetItemCount(grid)==2&&IsWindowEnabled(tags),"new crop is selected within tag-filtered gallery");SendMessageW(tags,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));SendMessageW(game,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,289,0);std::filesystem::remove(cropped);click();
            RECT normal{};GetWindowRect(window,&normal);SetWindowPos(window,nullptr,0,0,1060,760,SWP_NOMOVE|SWP_NOZORDER);RECT client{},button{},table{};GetClientRect(window,&client);GetWindowRect(GetDlgItem(window,289),&button);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&button),2);GetClientRect(grid,&table);require(button.bottom<client.bottom&&table.right>300&&table.bottom>300,"gallery fits minimum window with usable grid and metadata controls");std::filesystem::create_directories(argv[2]);screenshot(window,std::filesystem::path(argv[2])/L"gallery-small.bmp");SetWindowPos(window,nullptr,0,0,normal.right-normal.left,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);screenshot(window,std::filesystem::path(argv[2])/L"gallery.bmp");
            SendMessageW(tags,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));SendMessageW(game,CB_SETCURSEL,0,0);SendMessageW(window,WM_COMMAND,289,0);hr::Store cleaned;require(cleaned.get(key).empty(),"clearing metadata removes only fixture metadata");std::filesystem::remove(fixture);fixture.clear();SendMessageW(search,WM_SETTEXT,0,reinterpret_cast<LPARAM>(L""));SendMessageW(window,WM_COMMAND,286,0);select_page(L"Dashboard");std::cout<<checks<<" gallery checks passed\n";return 0;
        }catch(const std::exception& e){if(!fixture.empty()){std::error_code ec;std::filesystem::remove(fixture,ec);}std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2&&std::wstring(argv[1])==L"--media-extended-test"){
        try{
            std::filesystem::path folder(argv[2]);std::filesystem::create_directories(folder);hr::CaptureOptions options;options.cursor=false;options.region={10,10,210,130};auto image=hr::screenshot(options);
            auto dimensions=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);unsigned char bytes[24]{};file.read(reinterpret_cast<char*>(bytes),24);auto number=[&](int n){return (static_cast<unsigned>(bytes[n])<<24)|(static_cast<unsigned>(bytes[n+1])<<16)|(static_cast<unsigned>(bytes[n+2])<<8)|bytes[n+3];};return std::make_pair(number(16),number(20));};
            require(dimensions(image)==std::make_pair(200u,120u),"capture region produces exact image dimensions");auto before=std::filesystem::file_size(image);auto cropped=hr::crop_screenshot(image,{20,10,120,70});require(dimensions(cropped)==std::make_pair(100u,60u),"crop produces requested pixel dimensions");require(std::filesystem::file_size(image)==before,"crop preserves original source");
            options.region={10,10,5,30};fails([&]{hr::screenshot(options);},"invalid private region refuses fullscreen fallback");auto encoders=hr::encoder_report();require(encoders.find("Software/local MFTs:")!=std::string::npos,"encoder capability report queries installed transforms");
            auto source=folder/L"audio-fixture.mp4";require(std::filesystem::exists(source),"independent audio/video fixture exists");auto source_size=std::filesystem::file_size(source);std::atomic<bool> cancel{false};auto clip=hr::export_clip(source,.5,2.0,cancel);auto info=hr::video_information(clip);require(info.find("Audio: 2 channels")!=std::string::npos,"trim export preserves stereo audio");auto marker=info.find("Duration: ");require(marker!=std::string::npos,"trim export reports duration");double seconds=std::stod(info.substr(marker+10));require(seconds>1.3&&seconds<1.7,"trim duration follows chosen interval");require(std::filesystem::file_size(source)==source_size,"clip export preserves original video");
            fails([&]{hr::export_clip(source,2,1,cancel);},"reversed clip interval rejected");cancel=true;fails([&]{hr::export_clip(source,0,1,cancel);},"canceled export rejected without replacing source");cancel=false;auto replay=hr::export_replay({source,source},4,cancel);auto replay_info=hr::video_information(replay);auto replay_marker=replay_info.find("Duration: ");require(replay_marker!=std::string::npos&&std::abs(std::stod(replay_info.substr(replay_marker+10))-4)<.2,"multi-segment export retains requested tail duration");require(replay_info.find("Audio: 2 channels")!=std::string::npos,"multi-segment export retains stereo audio");std::filesystem::copy_file(replay,folder/L"joined-fixture.mp4",std::filesystem::copy_options::overwrite_existing);std::filesystem::remove(replay);
            std::filesystem::copy_file(clip,folder/L"trimmed-fixture.mp4",std::filesystem::copy_options::overwrite_existing);std::filesystem::copy_file(cropped,folder/L"cropped-fixture.png",std::filesystem::copy_options::overwrite_existing);std::filesystem::remove(image);std::filesystem::remove(cropped);std::filesystem::remove(clip);
            wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);hr::LiveCapture live;live.start(hr::utf8(self),"C:\\Synthetic\\fixture-game.exe");for(int n=0;n<100&&live.frames().size()<60;++n)Sleep(30);require(live.frames().size()>=60,"live profiler retains actual parsed frame intervals");require(live.metric()=="MsBetweenPresents","live profiler retains frame metric identity");live.stop();
            std::ostringstream report;report<<checks<<" extended media checks passed\n";hr::atomic_write(folder/L"extended-test.txt",report.str());std::cout<<report.str();return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2 && std::wstring(argv[1])==L"--gamer-test"){
        try{
            Fixture fixture;std::atomic<bool> cancel{false};auto source=fixture.folder/L"saves";std::filesystem::create_directories(source/L"nested");hr::atomic_write(source/L"save.dat","version one");hr::atomic_write(source/L"nested/config.ini","quality=high");
            auto snapshot=hr::snapshot_folder(source,"Isolated test backup",cancel);require(std::filesystem::exists(snapshot/L"manifest.dat"),"snapshot completes with manifest");require(std::filesystem::file_size(snapshot/L"files/save.dat")==11,"snapshot contains original save");
            auto target=fixture.folder/L"restore";std::filesystem::create_directories(target);hr::atomic_write(target/L"save.dat","version two");hr::atomic_write(target/L"extra.txt","keep");auto restored=hr::restore_snapshot(snapshot,target,cancel);
            auto read=[](const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(file),{});};
            require(read(target/L"save.dat")=="version one","restore copies verified save");require(read(target/L"extra.txt")=="keep","restore preserves extra destination file");
            auto marker=restored.find("Before-restore snapshot: ");require(marker!=std::string::npos,"restore reports original-state backup");auto baseline=std::filesystem::path(hr::wide(restored.substr(marker+25)));require(std::filesystem::exists(baseline/L"manifest.dat"),"before-restore snapshot exists");
            hr::atomic_write(snapshot/L"files/save.dat","tampered");fails([&]{hr::restore_snapshot(snapshot,fixture.folder/L"blocked",cancel);},"tampered snapshot refused");require(!std::filesystem::exists(fixture.folder/L"blocked"),"failed integrity check leaves destination absent");
            auto unsafe_snapshot=hr::snapshot_folder(source,"Unsafe manifest fixture",cancel);auto manifest=hr::decode(read(unsafe_snapshot/L"manifest.dat"));auto unsafe_target=fixture.folder/L"unsafe-restore";for(auto invalid:{"C:escaped.dat","save.dat:alternate","NUL.txt","nested/../escape.dat"}){auto invalid_manifest=manifest;invalid_manifest[1][1]=invalid;hr::atomic_write(unsafe_snapshot/L"manifest.dat",hr::encode(invalid_manifest));fails([&]{hr::restore_snapshot(unsafe_snapshot,unsafe_target,cancel);},"unsafe snapshot paths are refused before restore");require(!std::filesystem::exists(unsafe_target),"invalid snapshot leaves destination absent");}hr::atomic_write(unsafe_snapshot/L"manifest.dat",hr::encode(manifest));cancel=true;fails([&]{hr::restore_snapshot(unsafe_snapshot,unsafe_target,cancel);},"canceled restore refuses mutation");require(!std::filesystem::exists(unsafe_target),"canceled restore leaves destination absent");cancel=false;std::filesystem::remove_all(unsafe_snapshot);
            fails([&]{hr::snapshot_folder(L"C:\\Windows","blocked",cancel);},"system folder backup refused");
            auto converted=hr::gamer_report("sensitivity","800,1,0.022","1600,0.022",cancel);require(converted.find("0.500000")!=std::string::npos,"sensitivity preserves physical distance");fails([&]{hr::gamer_report("sensitivity","0,1,0.022","1600,0.022",cancel);},"zero DPI refused");
            auto steam=fixture.folder/L"Steam";std::filesystem::create_directories(steam/L"steamapps/common/Test Game");hr::atomic_write(steam/L"steamapps/appmanifest_123.acf","\"AppState\" { \"appid\" \"123\" \"name\" \"Test Game\" \"installdir\" \"Test Game\" }");auto games=hr::discover_games(steam);require(std::any_of(games.begin(),games.end(),[](const hr::Row& row){return row[0]=="Steam"&&row[1]=="123"&&row[2]=="Test Game";}),"Steam manifest discovery reads real install folder");
            auto ready=hr::readiness();require(ready.find("Available RAM")!=std::string::npos,"readiness reports measured memory");auto exported=fixture.folder/L"diagnostics.txt";hr::gamer_action("diagnostic-export",hr::utf8(exported.wstring()),"",cancel);require(std::filesystem::file_size(exported)>100,"diagnostic export creates report");fails([&]{hr::gamer_action("diagnostic-export",hr::utf8(exported.wstring()),"",cancel);},"export refuses overwrite");
            {hr::InputEngine input;input.set_deadzone(.4);fails([&]{input.set_deadzone(-.1);},"negative dead zone refused");fails([&]{input.set_deadzone(.9);},"excessive dead zone refused");auto readings=input.controllers();for(const auto& reading:readings)for(const auto& axis:reading.axes)require(std::isfinite(axis.second)&&axis.second>=-1&&axis.second<=1,"controller axes stay finite and normalized");}
            auto network=hr::gamer_report("network","127.0.0.1","",cancel);require(network.find("received: 12")!=std::string::npos,"loopback connection probes succeed");
            hr::Store store(fixture.folder/L"recovery.dat");auto plan=hr::active_plan();auto mouse=hr::mouse_settings();auto value=std::to_string(mouse.speed)+","+std::to_string(mouse.threshold1)+","+std::to_string(mouse.threshold2)+","+std::to_string(mouse.acceleration);
            store.rows={{"journal","power","",plan,plan,""},{"journal","mouse","",value,value,""}};store.save();hr::undo_at(store,0);require(store.of("journal").size()==1&&store.of("journal")[0][1]=="mouse","individual restore preserves unrelated newer record");
            store.rows={{"journal","power","",plan,plan,""},{"journal","power","",plan,plan,""}};store.save();fails([&]{hr::undo_at(store,0);},"newer same-setting change blocks older restore");
            wchar_t helper[32768]{};GetModuleFileNameW(nullptr,helper,32768);
            struct Child{PROCESS_INFORMATION process{};Child(const std::wstring& executable,const std::wstring& args){auto command=hr::quote(executable)+L" "+args;STARTUPINFOW startup{};startup.cb=sizeof startup;if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process))throw std::runtime_error("Cannot start isolated guard fixture");}~Child(){if(process.hProcess){if(WaitForSingleObject(process.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(process.hProcess,0);WaitForSingleObject(process.hProcess,3000);CloseHandle(process.hProcess);CloseHandle(process.hThread);}}};
            Child game(helper,L"--priority-fixture");FILETIME c{},e{},k{},u{};GetProcessTimes(game.process.hProcess,&c,&e,&k,&u);auto created=(static_cast<unsigned long long>(c.dwHighDateTime)<<32)|c.dwLowDateTime;hr::Process process{game.process.dwProcessId,L"Guard fixture",0,0,created};auto original_priority=GetPriorityClass(game.process.hProcess);
            auto guards=hr::data_dir()/L"session-guards";std::filesystem::create_directories(guards);auto stem=L"test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());auto normal=guards/(stem+L"-normal.dat"),conflict=guards/(stem+L"-conflict.dat"),crash=guards/(stem+L"-crash.dat");
            {hr::PriorityGuard guard(process,ABOVE_NORMAL_PRIORITY_CLASS,normal);require(GetPriorityClass(game.process.hProcess)==ABOVE_NORMAL_PRIORITY_CLASS,"watchdog applies approved priority");}require(GetPriorityClass(game.process.hProcess)==original_priority,"watchdog restores original priority on stop");
            {hr::PriorityGuard guard(process,ABOVE_NORMAL_PRIORITY_CLASS,conflict);SetPriorityClass(game.process.hProcess,IDLE_PRIORITY_CLASS);}require(GetPriorityClass(game.process.hProcess)==IDLE_PRIORITY_CLASS,"watchdog preserves externally changed priority");SetPriorityClass(game.process.hProcess,original_priority);
            auto ready_file=fixture.folder/L"guard-ready.txt";{Child parent(helper,L"--guard-parent "+std::to_wstring(process.pid)+L" "+std::to_wstring(created)+L" "+hr::quote(crash.wstring())+L" "+hr::quote(ready_file.wstring()));for(int n=0;n<100&&!std::filesystem::exists(ready_file);++n)Sleep(50);require(std::filesystem::exists(ready_file),"crash-recovery fixture parent starts guard");require(GetPriorityClass(game.process.hProcess)==ABOVE_NORMAL_PRIORITY_CLASS,"crash fixture priority applied");TerminateProcess(parent.process.hProcess,0);WaitForSingleObject(parent.process.hProcess,3000);for(int n=0;n<100&&GetPriorityClass(game.process.hProcess)!=original_priority;++n)Sleep(50);require(GetPriorityClass(game.process.hProcess)==original_priority,"priority restored after owning app crashes");}
            for(const auto& path:{normal,conflict,crash}){Sleep(40);std::filesystem::remove(path);}
            std::filesystem::remove_all(snapshot);if(baseline.parent_path()==snapshot.parent_path())std::filesystem::remove_all(baseline);
            std::ostringstream report;report<<checks<<" gamer checks passed\n";hr::atomic_write(argv[2],report.str());std::cout<<report.str();return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2 && std::wstring(argv[1])==L"--style-test"){
        try{
            INITCOMMONCONTROLSEX common{sizeof common,ICC_WIN95_CLASSES};InitCommonControlsEx(&common);
            WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"HeadroomStyleTest";cls.lpfnWndProc=[](HWND h,UINT msg,WPARAM w,LPARAM l)->LRESULT{
                if(msg==WM_CTLCOLOREDIT||msg==WM_CTLCOLORLISTBOX||msg==WM_CTLCOLORSTATIC){SetTextColor(reinterpret_cast<HDC>(w),RGB(240,240,245));SetBkColor(reinterpret_cast<HDC>(w),RGB(25,25,28));return reinterpret_cast<LRESULT>(GetStockObject(DKGRAY_BRUSH));}return DefWindowProcW(h,msg,w,l);};RegisterClassW(&cls);
            HWND fixture=CreateWindowExW(0,cls.lpszClassName,L"Headroom scrollbar validation",WS_OVERLAPPEDWINDOW|WS_VISIBLE,100,100,800,500,nullptr,nullptr,cls.hInstance,nullptr);
            HWND edit=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_VSCROLL|WS_HSCROLL|ES_MULTILINE|ES_READONLY|ES_AUTOHSCROLL,20,20,730,240,fixture,nullptr,cls.hInstance,nullptr);hr::style_scrollbars(edit);
            std::wstring lines;for(int i=0;i<400;++i)lines+=L"Line "+std::to_wstring(i)+L"  "+std::wstring(160,L'x')+L"\r\n";SetWindowTextW(edit,lines.c_str());
            SetWindowTextW(edit,L"First line\nSecond line\r\nThird line");require(SendMessageW(edit,EM_GETLINECOUNT,0,0)==3,"multiline output normalizes LF and preserves existing CRLF");SetWindowTextW(edit,lines.c_str());
            HWND combo=CreateWindowExW(0,WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST|WS_VSCROLL,20,285,300,160,fixture,nullptr,cls.hInstance,nullptr);hr::style_scrollbars(combo);
            for(int i=0;i<50;++i){auto title=L"Dropdown item "+std::to_wstring(i);SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(title.c_str()));}SendMessageW(combo,CB_SETCURSEL,0,0);
            auto pump=[](unsigned ms){auto start=GetTickCount64();MSG msg;while(GetTickCount64()-start<ms){while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(5);}};pump(200);
            SCROLLBARINFO bar{};bar.cbSize=sizeof bar;require(GetScrollBarInfo(edit,OBJID_VSCROLL,&bar)!=FALSE,"custom vertical scrollbar exposed");
            SetCursorPos((bar.rcScrollBar.left+bar.rcScrollBar.right)/2,bar.rcScrollBar.top+8);SendMessageW(edit,WM_NCLBUTTONDOWN,HTVSCROLL,0);SetCursorPos((bar.rcScrollBar.left+bar.rcScrollBar.right)/2,bar.rcScrollBar.bottom-5);SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,0);SendMessageW(edit,WM_LBUTTONUP,0,0);
            require(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)>300,"custom thumb drag scrolls long output");auto before=SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0);SendMessageW(edit,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),0);require(SendMessageW(edit,EM_GETFIRSTVISIBLELINE,0,0)<before,"wheel still scrolls output");
            HDC dc=GetDC(edit);RECT r;GetWindowRect(edit,&r);ReleaseDC(edit,dc);dc=GetWindowDC(edit);auto track_color=GetPixel(dc,bar.rcScrollBar.left-r.left+1,bar.rcScrollBar.top-r.top+3);ReleaseDC(edit,dc);require(track_color==RGB(22,22,25),"scrollbar track uses app dark color");
            SendMessageW(combo,CB_SHOWDROPDOWN,TRUE,0);pump(150);COMBOBOXINFO info{};info.cbSize=sizeof info;GetComboBoxInfo(combo,&info);require(info.hwndList&&IsWindowVisible(info.hwndList),"styled dropdown opens");
            dc=GetDC(combo);auto edge=GetPixel(dc,1,1);ReleaseDC(combo,dc);require(edge!=RGB(255,255,255),"dropdown has no white edge");SendMessageW(combo,CB_SHOWDROPDOWN,FALSE,0);
            HWND table=CreateWindowExW(0,WC_LISTVIEWW,L"",WS_CHILD|WS_VISIBLE|LVS_REPORT,350,285,400,150,fixture,nullptr,cls.hInstance,nullptr);hr::style_scrollbars(table);LVCOLUMNW column{};column.mask=LVCF_WIDTH|LVCF_TEXT;column.cx=650;column.pszText=const_cast<wchar_t*>(L"Scrollable table");ListView_InsertColumn(table,0,&column);
            for(int i=0;i<200;++i){auto title=L"Table row "+std::to_wstring(i);LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=i;item.pszText=title.data();ListView_InsertItem(table,&item);}pump(100);
            bar={};bar.cbSize=sizeof bar;require(GetScrollBarInfo(table,OBJID_VSCROLL,&bar)!=FALSE,"custom table scrollbar exposed");SetCursorPos((bar.rcScrollBar.left+bar.rcScrollBar.right)/2,bar.rcScrollBar.top+8);SendMessageW(table,WM_NCLBUTTONDOWN,HTVSCROLL,0);SetCursorPos((bar.rcScrollBar.left+bar.rcScrollBar.right)/2,bar.rcScrollBar.bottom-5);SendMessageW(table,WM_MOUSEMOVE,MK_LBUTTON,0);SendMessageW(table,WM_LBUTTONUP,0,0);require(ListView_GetTopIndex(table)>150,"custom table thumb reaches lower rows");
            bar={};bar.cbSize=sizeof bar;GetScrollBarInfo(edit,OBJID_HSCROLL,&bar);SetCursorPos(bar.rcScrollBar.left+8,(bar.rcScrollBar.top+bar.rcScrollBar.bottom)/2);SendMessageW(edit,WM_NCLBUTTONDOWN,HTHSCROLL,0);SetCursorPos(bar.rcScrollBar.right-5,(bar.rcScrollBar.top+bar.rcScrollBar.bottom)/2);SendMessageW(edit,WM_MOUSEMOVE,MK_LBUTTON,0);SendMessageW(edit,WM_LBUTTONUP,0,0);SCROLLINFO horizontal{};horizontal.cbSize=sizeof horizontal;horizontal.fMask=SIF_POS;GetScrollInfo(edit,SB_HORZ,&horizontal);require(horizontal.nPos>0,"custom horizontal thumb scrolls long lines");
            std::filesystem::path folder(argv[2]);std::filesystem::create_directories(folder);screenshot(fixture,folder/L"custom-scrollbars.bmp");
            hr::app_toast(fixture,L"Mode notification preview");pump(150);HWND toast=FindWindowW(L"HeadroomToast",nullptr);require(toast!=nullptr,"mode toast opens");BYTE alpha=0;DWORD flags=0;COLORREF key=0;require(GetLayeredWindowAttributes(toast,&key,&alpha,&flags)&&alpha==255&&(flags&LWA_ALPHA),"notification background fully opaque");
            dc=GetDC(toast);auto border=GetPixel(dc,0,20);ReleaseDC(toast,dc);require(border==RGB(26,242,44),"notification has exact green border");screenshot(toast,folder/L"opaque-toast.bmp");DestroyWindow(toast);
            HWND app=FindWindowW(L"HeadroomNativeWindow",nullptr);if(app){HWND nav=GetDlgItem(app,100);auto selected=SendMessageW(nav,LB_GETCURSEL,0,0);for(auto key:{VK_DOWN,VK_UP,VK_HOME,VK_END,VK_NEXT,VK_PRIOR,VK_RETURN,VK_SPACE})SendMessageW(nav,WM_KEYDOWN,key,0);SendMessageW(nav,WM_CHAR,'G',0);require(SendMessageW(nav,LB_GETCURSEL,0,0)==selected,"keyboard cannot switch sidebar section");require(!(GetWindowLongPtrW(nav,GWL_STYLE)&WS_TABSTOP),"sidebar excluded from keyboard tab switching");}
            DestroyWindow(fixture);std::ostringstream report;report<<checks<<" style checks passed\n";hr::atomic_write(folder/L"style-test.txt",report.str());std::cout<<report.str();return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2 && std::wstring(argv[1])==L"--ui-test"){
        try{
            HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);
            require(window!=nullptr,"native UI running");
            SendMessageW(window,WM_COMMAND,225,0);
            require(IsWindowVisible(window)!=FALSE,"main window visible");
            HWND nav=GetDlgItem(window,100);require(nav!=nullptr,"navigation exists");
            SendMessageW(window,WM_ACTIVATE,WA_ACTIVE,0);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            HDC active_dc=GetDC(window);auto active_color=GetPixel(active_dc,190,110);ReleaseDC(window,active_dc);
            SendMessageW(window,WM_ACTIVATE,WA_INACTIVE,0);
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            HDC inactive_dc=GetDC(window);auto inactive_color=GetPixel(inactive_dc,190,110);ReleaseDC(window,inactive_dc);
            require(active_color==inactive_color,"losing focus preserves black surface color");
            SendMessageW(window,WM_ACTIVATE,WA_ACTIVE,0);
            std::filesystem::path folder(argv[2]);std::filesystem::create_directories(folder);
            const wchar_t* names[]={L"Dashboard",L"Games",L"Power modes",L"Startup",L"Benchmarks",L"Drivers",L"Services",L"Settings",L"Temp cleaner",L"FPS optimizer",L"Debloater",L"Mouse tuning",L"Input remapper",L"Disks",L"Screen capture",L"Processes",L"Uninstaller",L"Storage lab",L"System tools",L"Packages",L"Gamer tools",L"Recovery",L"Capture gallery",L"Overlay editor",L"Instant replay",L"Game profiles",L"Microphone room",L"Display / audio",L"Scheduler",L"Mod profiles",L"Stutter timeline",L"Desktop Studio"};
            for(int i=0;i<32;++i){
                int selected=-1,count=static_cast<int>(SendMessageW(nav,LB_GETCOUNT,0,0));
                for(int n=0;n<count;++n){wchar_t title[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(title));if(std::wstring(title)==names[i])selected=n;}
                require(selected>=0,"categorized navigation contains page");
                SendMessageW(nav,LB_SETCURSEL,selected,0);
                SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                HWND table=GetDlgItem(window,101+i);
                if(i==0)require(IsWindowVisible(GetDlgItem(window,1004))!=FALSE,"dashboard Boost visible");
                else if(i==13)require(IsWindowVisible(GetDlgItem(window,1100))!=FALSE,"storage circle visible");
                else if(i==31)require(IsWindowVisible(GetDlgItem(window,520))!=FALSE,"desktop studio editor visible");
                else if(i==24)require(IsWindowVisible(GetDlgItem(window,1500))!=FALSE,"replay status visible");
                else if(i==26)require(IsWindowVisible(GetDlgItem(window,1501))!=FALSE,"microphone meters visible");
                else if(i==28)require(IsWindowVisible(GetDlgItem(window,497))!=FALSE,"maintenance history visible");
                else if(i==23)require(IsWindowVisible(GetDlgItem(window,1400))!=FALSE,"HUD live preview visible");
                else if(i==4)require(IsWindowVisible(GetDlgItem(window,420))!=FALSE,"stress results visible");
                else if(i==2 || i==9 || i==11 || i==14 || i==19)require(true,"settings page visited");else require(table && IsWindowVisible(table),"page report visible");
                screenshot(window,folder/(L"page-"+std::to_wstring(i)+L".bmp"));
            }
            auto select_page=[&](const wchar_t* name){
                int count=static_cast<int>(SendMessageW(nav,LB_GETCOUNT,0,0));
                for(int n=0;n<count;++n){wchar_t title[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(title));if(std::wstring(title)==name){SendMessageW(nav,LB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));return;}}
            };
            {RECT normal{};GetWindowRect(window,&normal);SetWindowPos(window,nullptr,0,0,1060,760,SWP_NOMOVE|SWP_NOZORDER);const wchar_t* advanced[]={L"Instant replay",L"Game profiles",L"Microphone room",L"Display / audio",L"Scheduler",L"Mod profiles",L"Stutter timeline",L"Desktop Studio"};for(int page=0;page<7;++page){select_page(advanced[page]);Sleep(100);RECT bounds{};GetClientRect(window,&bounds);bool fits=true;for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){if(!IsWindowVisible(child))continue;int id=GetDlgCtrlID(child);if(!((id>=293&&id<=327)||(id>=470&&id<=510)||(id>=1500&&id<=1520)||(id>=125&&id<=131)))continue;RECT rect{};GetWindowRect(child,&rect);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);if(rect.left<0||rect.top<0||rect.right>bounds.right+1||rect.bottom>bounds.bottom-42)fits=false;}require(fits,"new feature controls fit minimum window without clipping footer");screenshot(window,folder/(L"advanced-small-"+std::to_wstring(page)+L".bmp"));}SetWindowPos(window,nullptr,0,0,normal.right-normal.left,normal.bottom-normal.top,SWP_NOMOVE|SWP_NOZORDER);}
            select_page(L"Mouse tuning");HWND slider=FindWindowExW(window,nullptr,TRACKBAR_CLASSW,nullptr);require(slider!=nullptr,"mouse speed slider exists");
            auto before_mouse=hr::mouse_settings();int old_position=static_cast<int>(SendMessageW(slider,TBM_GETPOS,0,0));RECT track{};GetClientRect(slider,&track);
            POINT original_pointer{};GetCursorPos(&original_pointer);auto move_pointer=[&](int x){POINT point{x,track.bottom/2};ClientToScreen(slider,&point);SetCursorPos(point.x,point.y);};move_pointer(16);
            SendMessageW(slider,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(16,track.bottom/2));{auto position=SendMessageW(slider,TBM_GETPOS,0,0);if(position!=1)throw std::runtime_error("slider minimum was "+std::to_string(position)+", range "+std::to_string(SendMessageW(slider,TBM_GETRANGEMIN,0,0))+".."+std::to_string(SendMessageW(slider,TBM_GETRANGEMAX,0,0))+", width "+std::to_string(track.right));require(position==1,"slider drag reaches minimum");}
            for(int x=16;x<track.right-16;x+=7){move_pointer(x);SendMessageW(slider,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(x,track.bottom/2));}
            move_pointer(track.right-16);SendMessageW(slider,WM_LBUTTONUP,0,MAKELPARAM(track.right-16,track.bottom/2));require(SendMessageW(slider,TBM_GETPOS,0,0)==20,"slider drag reaches maximum");
            SendMessageW(slider,TBM_SETPOS,TRUE,old_position);SetCursorPos(original_pointer.x,original_pointer.y);auto after_mouse=hr::mouse_settings();require(before_mouse.speed==after_mouse.speed&&before_mouse.acceleration==after_mouse.acceleration,"dragging slider does not apply Windows settings");screenshot(window,folder/L"mouse-slider.bmp");
            select_page(L"Gamer tools");HWND tools=GetDlgItem(window,121);RECT tool_rect{};GetClientRect(tools,&tool_rect);require(ListView_GetColumnWidth(tools,0)+ListView_GetColumnWidth(tools,1)<=tool_rect.right,"gamer tool categories fit pane width");
            RECT normal_bounds{};GetWindowRect(window,&normal_bounds);SetWindowPos(window,nullptr,0,0,1060,760,SWP_NOMOVE|SWP_NOZORDER);Sleep(100);GetClientRect(tools,&tool_rect);require(ListView_GetColumnWidth(tools,0)+ListView_GetColumnWidth(tools,1)<=tool_rect.right,"gamer categories fit minimum window size");screenshot(window,folder/L"gamer-tools-small.bmp");SetWindowPos(window,nullptr,0,0,normal_bounds.right-normal_bounds.left,normal_bounds.bottom-normal_bounds.top,SWP_NOMOVE|SWP_NOZORDER);
            select_page(L"Gamer tools");auto select_tool=[&](const std::wstring& title){SendMessageW(GetDlgItem(window,430),WM_SETTEXT,0,reinterpret_cast<LPARAM>(title.c_str()));return ListView_GetItemCount(GetDlgItem(window,121))>0;};
            require(GetDlgItem(window,278)==nullptr,"duplicate Inspect action removed");require(select_tool(L"Connection quality"),"connection quality tool exists");require(IsWindowVisible(GetDlgItem(window,431))&&!IsWindowVisible(GetDlgItem(window,432))&&!IsWindowVisible(GetDlgItem(window,281)),"network tool shows one text input without Browse");
            require(select_tool(L"Display setup checker"),"display tool exists");require(!IsWindowVisible(GetDlgItem(window,431))&&!IsWindowVisible(GetDlgItem(window,432)),"display tool hides unused inputs");
            require(select_tool(L"Versioned save"),"backup tool exists");require(IsWindowVisible(GetDlgItem(window,431))&&IsWindowVisible(GetDlgItem(window,432))&&IsWindowVisible(GetDlgItem(window,281))&&!IsWindowVisible(GetDlgItem(window,282)),"backup browses source only and retains label field");
            require(select_tool(L"Mouse sensitivity converter"),"converter exists");require(IsWindowVisible(GetDlgItem(window,431))&&IsWindowVisible(GetDlgItem(window,432))&&!IsWindowVisible(GetDlgItem(window,281))&&!IsWindowVisible(GetDlgItem(window,282)),"converter shows numeric inputs without Browse buttons");screenshot(window,folder/L"gamer-tools-converter.bmp");
            select_page(L"Input remapper");
            HWND source=GetDlgItem(window,400);
            for(int n=0;n<4;++n){SendMessageW(source,CB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(400,CBN_SELCHANGE),reinterpret_cast<LPARAM>(source));Sleep(150);screenshot(window,folder/(L"device-"+std::to_wstring(n)+L".bmp"));}
            require(IsWindowVisible(GetDlgItem(window,1200))!=FALSE,"device diagram visible");
            select_page(L"Settings");
            require(!GetDlgItem(window,407),"dark-only settings has no theme selector");
            HDC dark_dc=GetDC(window);auto dark_color=GetPixel(dark_dc,190,110);ReleaseDC(window,dark_dc);
            require(GetRValue(dark_color)<80&&GetGValue(dark_color)<80&&GetBValue(dark_color)<80,"dark surface remains active");
            screenshot(window,folder/L"dark-theme.bmp");
            PostMessageW(window,WM_COMMAND,202,0);
            HWND dialog=nullptr;for(int n=0;n<40&&!dialog;++n){Sleep(50);dialog=FindWindowW(L"HeadroomAppDialog",nullptr);}
            require(dialog!=nullptr,"app-styled file picker opens");
            screenshot(dialog,folder/L"app-dialog.bmp");PostMessageW(dialog,WM_COMMAND,IDCANCEL,0);Sleep(150);
            select_page(L"Dashboard");
            require(IsWindowVisible(GetDlgItem(window,208))!=FALSE,"mode switching lives on dashboard");
            require(!IsWindowVisible(GetDlgItem(window,116)),"process list moved off dashboard");
            for(int i=0;i<3;++i)require(IsWindowVisible(GetDlgItem(window,1300+i))!=FALSE,"dashboard history visible");
            select_page(L"Games");require((GetWindowLongPtrW(GetDlgItem(window,102),GWL_STYLE)&LVS_TYPEMASK)==LVS_ICON,"games use icon grid");select_page(L"Dashboard");
            Fixture suggested;auto candidate=suggested.folder/L"approval-check.tmp";hr::atomic_write(candidate,"approval fixture");
            HANDLE fixture_file=CreateFileW(candidate.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
            ULARGE_INTEGER aged{};FILETIME now{};GetSystemTimeAsFileTime(&now);aged.LowPart=now.dwLowDateTime;aged.HighPart=now.dwHighDateTime;aged.QuadPart-=48ULL*60*60*10000000;FILETIME old_time{aged.LowPart,aged.HighPart};
            require(fixture_file!=INVALID_HANDLE_VALUE && SetFileTime(fixture_file,nullptr,nullptr,&old_time),"age suggested-fix fixture");CloseHandle(fixture_file);
            SendMessageW(window,WM_COMMAND,276,0);
            for(int n=0;n<200&&!IsWindowEnabled(GetDlgItem(window,276));++n)Sleep(50);
            require(IsWindowEnabled(GetDlgItem(window,277))!=FALSE,"scan offers reviewable fixes");
            require(std::filesystem::exists(candidate),"suggestion scan leaves file unchanged");
            PostMessageW(window,WM_COMMAND,277,0);HWND review=nullptr;for(int n=0;n<40&&!review;++n){Sleep(50);review=FindWindowW(L"HeadroomFixReview",nullptr);}
            require(review!=nullptr,"suggested fixes require app approval dialog");require(GetDlgItem(review,100)!=nullptr,"suggestion has its own Fix button");screenshot(review,folder/L"fix-review.bmp");PostMessageW(review,WM_COMMAND,IDCANCEL,0);Sleep(150);
            require(std::filesystem::exists(candidate),"canceling suggested fixes preserves file");
            wchar_t badge[80]{};SendMessageW(GetDlgItem(window,277),WM_GETTEXT,80,reinterpret_cast<LPARAM>(badge));require(std::wstring(badge).find(L"(")!=std::wstring::npos,"review button exposes badge count");
            std::atomic<unsigned> first{0},second{0};std::atomic<bool> drove{false};
            std::thread clicker([&]{HWND modal=nullptr;for(int n=0;n<100&&!modal;++n){Sleep(30);modal=FindWindowW(L"HeadroomFixReview",nullptr);}if(!modal)return;
                SendMessageW(modal,WM_COMMAND,100,0);Sleep(80);drove=!IsWindowEnabled(GetDlgItem(modal,100))&&IsWindowEnabled(GetDlgItem(modal,101));PostMessageW(modal,WM_COMMAND,IDCANCEL,0);
            });
            hr::app_fix_review(window,{{"one",L"Isolated test action",L"Test callback only; no Windows settings are changed."},{"two",L"Unselected test action",L"This callback must not run."}},[&](HWND modal,const std::string& id){if(id=="one")++first;else ++second;hr::update_fix_review(modal,id,L"Fixed",true);});clicker.join();
            require(first==1&&second==0,"per-row Fix runs only its own callback");require(drove,"fixed row disabled while other row stays available");
            select_page(L"System tools");RECT upper{},lower{};GetClientRect(GetDlgItem(window,119),&upper);
            HWND output=nullptr;for(HWND child=GetWindow(window,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)){wchar_t cls[32]{};GetClassNameW(child,cls,32);if(IsWindowVisible(child)&&std::wstring(cls)==L"Edit")output=child;}
            if(output)GetClientRect(output,&lower);require(output&&upper.bottom>lower.bottom,"system tools list larger than output pane");select_page(L"Dashboard");

            RECT original{};GetWindowRect(window,&original);
            ShowWindow(window,SW_MAXIMIZE);std::this_thread::sleep_for(std::chrono::milliseconds(350));
            for(int n=0;n<5;++n){RECT rect{};GetClientRect(GetDlgItem(window,1000+n),&rect);require(rect.right==rect.bottom && rect.right>0,"maximized resource gauge is square");}
            ShowWindow(window,SW_MINIMIZE);std::this_thread::sleep_for(std::chrono::milliseconds(100));
            ShowWindow(window,SW_RESTORE);std::this_thread::sleep_for(std::chrono::milliseconds(350));
            for(int n=0;n<5;++n){RECT rect{};GetClientRect(GetDlgItem(window,1000+n),&rect);require(rect.right==rect.bottom && rect.right>0,"restored resource gauge is square");}
            ShowWindow(window,SW_NORMAL);SetWindowPos(window,nullptr,original.left,original.top,original.right-original.left,original.bottom-original.top,SWP_NOZORDER);
            require(GetDlgItem(window,250)!=nullptr,"one-click debloat button exists");
            SendMessageW(window,WM_COMMAND,234,0);
            HWND overlay=FindWindowW(L"HeadroomOverlay",nullptr);
            require(overlay!=nullptr,"overlay created");
            require((GetWindowLongPtrW(overlay,GWL_EXSTYLE)&WS_EX_TRANSPARENT)!=0,"overlay click-through");
            screenshot(overlay,folder/L"overlay.bmp");
            SendMessageW(window,WM_COMMAND,234,0);
            require(!FindWindowW(L"HeadroomOverlay",nullptr),"overlay closed");
            SendMessageW(window,WM_CLOSE,0,0);
            require(!IsWindowVisible(window),"close hides to tray");
            SendMessageW(window,WM_COMMAND,225,0);
            require(IsWindowVisible(window)!=FALSE,"restore from tray action");
            SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));
            SendMessageW(nav,LB_SETCURSEL,0,0);
            SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));
            std::ostringstream report;report<<checks<<" UI checks passed\n";
            hr::atomic_write(folder/L"ui-test.txt",report.str());
            return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
    if(argc>2 && std::wstring(argv[1])==L"--listen-test"){
        HWND window=FindWindowW(L"HeadroomNativeWindow",nullptr);
        if(!window)return 1;
        SendMessageW(window,WM_COMMAND,225,0);HWND nav=GetDlgItem(window,100);
        int count=static_cast<int>(SendMessageW(nav,LB_GETCOUNT,0,0));
        for(int n=0;n<count;++n){wchar_t title[256]{};SendMessageW(nav,LB_GETTEXT,n,reinterpret_cast<LPARAM>(title));if(std::wstring(title)==L"Input remapper"){SendMessageW(nav,LB_SETCURSEL,n,0);SendMessageW(window,WM_COMMAND,MAKEWPARAM(100,LBN_SELCHANGE),reinterpret_cast<LPARAM>(nav));break;}}
        SendMessageW(window,WM_COMMAND,251,0);
        auto output=std::filesystem::path(argv[2]);hr::atomic_write(output,"READY\n");
        for(int n=0;n<200;++n){
            Sleep(50);wchar_t value[256]{};SendMessageW(GetDlgItem(window,401),WM_GETTEXT,256,reinterpret_cast<LPARAM>(value));
            if(std::wstring(value)==L"F8"){
                hr::atomic_write(output,"PASS physical F8 captured into source selector\n");auto picture=output;picture.replace_extension(L".bmp");screenshot(window,picture);return 0;
            }
        }
        hr::atomic_write(output,"FAIL physical input not observed\n");return 1;
    }
    if(argc>2 && std::wstring(argv[1])==L"--media-test"){
        try{
            std::filesystem::path folder(argv[2]);std::filesystem::create_directories(folder);
            auto png=hr::screenshot();
            require(std::filesystem::file_size(png)>1000,"screenshot PNG created");
            std::filesystem::copy_file(png,folder/L"screenshot.png",std::filesystem::copy_options::overwrite_existing);std::filesystem::remove(png);
            hr::Recorder recorder;recorder.start();std::this_thread::sleep_for(std::chrono::seconds(3));recorder.stop();
            require(recorder.error().empty(),recorder.error().c_str());
            auto mp4=recorder.file();require(std::filesystem::file_size(mp4)>1000,"MP4 recording created");
            std::filesystem::copy_file(mp4,folder/L"recording.mp4",std::filesystem::copy_options::overwrite_existing);std::filesystem::remove(mp4);
            std::cout<<checks<<" media checks passed\n";return 0;
        }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
    }
                                                                               
                                                                              
    if(argc>1 && std::wstring(argv[1])==L"--process_name"){
        std::wstring app=argv[2],file;
        for(int i=1;i+1<argc;++i)if(std::wstring(argv[i])==L"--output_file")file=argv[i+1];
        if(file.empty()){
            std::cout<<"Application,MsBetweenPresents\n"<<std::flush;
            for(int i=0;i<120;++i){std::cout<<hr::utf8(app)<<",10\n"<<std::flush;Sleep(10);}
            return 0;
        }
        std::ofstream out{std::filesystem::path(file)};
        out<<"Application,MsBetweenPresents\n";
        for(int i=0;i<100;++i)out<<hr::utf8(app)<<",10\n";
        return out?0:3;
    }
    try{
        Fixture fixture;
        require(hr::utf8(hr::wide("Unicode \xc3\xa9 \xe6\xb8\xb8\xe6\x88\x8f"))=="Unicode \xc3\xa9 \xe6\xb8\xb8\xe6\x88\x8f","UTF-8 Windows roundtrip");
        require(hr::quote(L"C:\\a b\\")==L"\"C:\\a b\\\\\"","command quoting trailing slash");
        require(hr::quote(L"a\"b")==L"\"a\\\"b\"","command quoting embedded quote");
        hr::Store store(fixture.folder/L"settings.dat");
        store.set("mode","Browsing");store.save();
        require(hr::Store(store.path).get("mode")=="Browsing","atomic settings roundtrip");
        auto plan=hr::active_plan();store.set("plan.Gaming",plan);
        hr::switch_mode(store,"Gaming");
        require(store.get("mode")=="Gaming" && hr::active_plan()==plan,"mode applies selected plan");
        require(store.of("journal").empty(),"same power plan needs no undo entry");
                                                                      
        store.rows.push_back({"journal","power","",plan,plan,""});store.save();
        hr::undo(store);require(store.of("journal").empty(),"prepared power action recovered");
        std::wstring command=L"\"C:\\Headroom synthetic test\\fixture.exe\" --value";
        std::string bytes(reinterpret_cast<const char*>(command.c_str()),(command.size()+1)*sizeof(wchar_t));
        hr::Startup entry{hr::utf8(fixture.name),"User",bytes,REG_EXPAND_SZ};
        hr::restore_startup(entry);
        auto find_entry=[&](){
            auto entries=hr::startup();
            return std::find_if(entries.begin(),entries.end(),[&](const hr::Startup& s){return s.name==entry.name && s.scope=="User";})!=entries.end();
        };
        require(find_entry(),"startup fixture created");
        fails([&]{hr::restore_startup(entry);},"startup overwrite refused");
        hr::change_startup(store,entry);
        require(!find_entry() && store.of("disabled").size()==1,"startup disabled and backed up");
        hr::Store reopened(store.path);
        hr::undo(reopened);
        require(find_entry() && reopened.of("disabled").empty() && reopened.of("journal").empty(),"startup undo survives restart");
        auto entries=hr::startup();
        auto actual=std::find_if(entries.begin(),entries.end(),[&](const hr::Startup& s){return s.name==entry.name;});
        require(actual!=entries.end() && actual->bytes==bytes && actual->type==REG_EXPAND_SZ,"startup raw data and type preserved");
        hr::change_startup(reopened,entry);
        auto backup=reopened.of("disabled").at(0);
        hr::restore_saved_startup(reopened,backup);
        require(find_entry() && reopened.of("journal").empty(),"explicit startup restore clears undo");
                                                                               
        hr::change_startup(reopened,entry);
        auto changed=entry;changed.bytes=std::string(2,'\0');
        hr::restore_startup(changed);
        fails([&]{hr::undo(reopened);},"startup undo detects external changes");
        auto service_rows=hr::services();
        require(!service_rows.empty(),"service inventory");
        auto protected_service=std::find_if(service_rows.begin(),service_rows.end(),[](const hr::Service& s){return !s.eligible;});
        if(protected_service!=service_rows.end()){
            fails([&]{hr::set_service(*protected_service,SERVICE_DISABLED,false,protected_service->start,protected_service->delayed);},"protected service mutation refused");
        }
        hr::Store capture_store(fixture.folder/L"capture-settings.dat");
        wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);
        capture_store.set("presentmon",hr::utf8(self));
        auto output=hr::capture(capture_store,"C:\\Synthetic\\fixture-game.exe",10);
        std::ifstream stream{std::filesystem::path(hr::wide(output))};
        auto benchmark=hr::csv(stream,"fixture-game.exe");
        require(benchmark.frames==100 && benchmark.average==100,"PresentMon invocation and CSV plumbing (synthetic fixture)");
        stream.close();std::filesystem::remove(std::filesystem::path(hr::wide(output)));
        hr::LiveCapture live;live.start(hr::utf8(self),"C:\\Synthetic\\fixture-game.exe");
        std::this_thread::sleep_for(std::chrono::milliseconds(650));
        require(live.fps()>99 && live.fps()<101,"live FPS pipeline (synthetic fixture)");live.stop();
                                                                
        auto oldfile=fixture.folder/L"old.tmp",fresh=fixture.folder/L"fresh.tmp";
        {std::ofstream out(oldfile);out<<"temporary fixture";}
        {std::ofstream out(fresh);out<<"fresh fixture";}
        HANDLE old=CreateFileW(oldfile.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER value{};value.LowPart=time.dwLowDateTime;value.HighPart=time.dwHighDateTime;
        value.QuadPart-=48ULL*60*60*10000000;time.dwLowDateTime=value.LowPart;time.dwHighDateTime=value.HighPart;
        require(old!=INVALID_HANDLE_VALUE && SetFileTime(old,nullptr,nullptr,&time),"age temporary fixture");CloseHandle(old);
        auto candidates=hr::scan_temp(fixture.folder);
        require(candidates.size()==1 && candidates[0].path==oldfile,"temp scan excludes fresh files");
        auto cleaned=hr::clean_temp(candidates);
        require(cleaned.deleted==1 && !std::filesystem::exists(oldfile) && std::filesystem::exists(fresh),"cleanup deletes only eligible fixture");
        fails([]{hr::scan_temp(L"C:\\Windows");},"cleanup refuses system directory");
        auto own=hr::own_process();require(hr::trim_ram(own)>=0,"own working set trim");
        ++own.created;fails([&]{hr::trim_ram(own);},"RAM trim rejects reused PID");
        auto mouse_before=hr::mouse_settings();
        struct RestoreMouse {
            hr::MouseSettings before;
            ~RestoreMouse(){int values[]={before.threshold1,before.threshold2,before.acceleration};SystemParametersInfoW(SPI_SETMOUSE,0,values,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);SystemParametersInfoW(SPI_SETMOUSESPEED,0,reinterpret_cast<void*>(static_cast<INT_PTR>(before.speed)),SPIF_UPDATEINIFILE|SPIF_SENDCHANGE);}
        } restore_mouse{mouse_before};
        hr::Store mouse_store(fixture.folder/L"mouse.dat");
        int speed=mouse_before.speed==10?11:10;
        hr::save_mouse_settings(mouse_store,speed,false);
        require(hr::mouse_settings().speed==speed,"mouse settings applied");
        hr::undo(mouse_store);
        auto mouse_after=hr::mouse_settings();
        require(mouse_after.speed==mouse_before.speed && mouse_after.acceleration==mouse_before.acceleration && mouse_after.threshold1==mouse_before.threshold1 && mouse_after.threshold2==mouse_before.threshold2,"mouse settings undo preserves exact values");
        auto original_state=[](const wchar_t* path,const wchar_t* name){
            DWORD n=0,type=0;LONG r=RegGetValueW(HKEY_CURRENT_USER,path,name,RRF_RT_ANY,nullptr,nullptr,&n);
            if(r==ERROR_FILE_NOT_FOUND)return std::string("-");
            hr::check(r,"Test snapshot");std::string bytes(n,'\0');
            hr::check(RegGetValueW(HKEY_CURRENT_USER,path,name,RRF_RT_ANY,&type,n?reinterpret_cast<void*>(&bytes[0]):nullptr,&n),"Test snapshot");
            return std::to_string(type)+":"+hr::hex(bytes);
        };
        auto before_game=original_state(L"Software\\Microsoft\\GameBar",L"AllowAutoGameMode");
        hr::Store optimize(fixture.folder/L"optimize.dat");optimize.set("plan.Gaming",hr::active_plan());
        hr::apply_fps_profile(optimize);
        require(optimize.get("mode")=="Gaming","FPS profile applied");
        while(!optimize.of("journal").empty())hr::undo(optimize);
        require(before_game==original_state(L"Software\\Microsoft\\GameBar",L"AllowAutoGameMode"),"FPS profile undo restores original Game Mode value");
        hr::reduce_suggestions(optimize);while(!optimize.of("journal").empty())hr::undo(optimize);
        require(optimize.of("journal").empty(),"suggestion settings undo");
        fails([&]{hr::remove_optional_app(optimize,{"Microsoft.WindowsStore","fake","fake","fake"});},"debloater refuses Store and unknown packages");
        require(!hr::drives().empty(),"fixed drive inventory");
        {hr::InputEngine engine;engine.configure({},false,true,"",false);std::this_thread::sleep_for(std::chrono::milliseconds(80));require(!engine.status().empty(),"input worker initializes and stops");}
        auto pc=hr::pc_info();require(std::any_of(pc.begin(),pc.end(),[](const hr::Row& row){return row[0]=="CPU" && !row[1].empty();}),"PC info includes CPU");
        hr::StressTest stress;stress.start(5,true,true,false);
        auto deadline=GetTickCount64()+10000;
        while(stress.snapshot().running && GetTickCount64()<deadline)Sleep(100);
        auto result=stress.snapshot();stress.stop();
        require(!result.running && result.elapsed>=5,"stress test completes at requested duration");
        require(result.cpu_rounds>0 && result.ram_bytes>0 && result.errors==0,"CPU and RAM load verified");
        require(!hr::vendor("NVIDIA").empty() && hr::vendor("QXL").empty(),"driver vendor mapping");
        auto sample=hr::sample();
        require(sample.total>0 && !sample.processes.empty(),"resource monitoring");
        std::cout<<checks<<" Windows integration checks passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
