#include "windows.hpp"
#include "network.hpp"
#include "comparison.hpp"
#include "experience.hpp"
#include "tools.hpp"
#include "boost.hpp"
#include "input.hpp"
#include "controller_art.hpp"
#include "capture.hpp"
#include "overlay.hpp"
#include "desktop.hpp"
#include "replay.hpp"
#include "audio.hpp"
#include "devices.hpp"
#include "profiles.hpp"
#include "maintenance.hpp"
#include "timeline.hpp"
#include "commands.hpp"
#include "dialog.hpp"
#include "scrollbars.hpp"
#include "stress.hpp"
#include "uninstall.hpp"
#include "storage.hpp"
#include "system_tools.hpp"
#include "autoruns.hpp"
#include "session.hpp"
#include "memory.hpp"
#include "gamer.hpp"
#include "game_sessions.hpp"
#include <cmath>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <algorithm>
#include <atomic>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <stdexcept>

namespace {
constexpr UINT tray_message=WM_APP+1;
constexpr int tab_count=35;
constexpr bool light_theme=false;
COLORREF bg=RGB(13,13,15),panel=RGB(19,19,22),ink=RGB(221,230,247),neon=RGB(26,242,44),purple=RGB(26,242,44),pink=RGB(0,216,255);
struct SavedDrawingState {HDC dc;int state;explicit SavedDrawingState(HDC value):dc(value),state(SaveDC(value)){}~SavedDrawingState(){if(state)RestoreDC(dc,state);}};
struct ButtonPaintBuffer {
    DRAWITEMSTRUCT original,local;HDC memory=nullptr;HBITMAP bitmap=nullptr;HGDIOBJ previous=nullptr;
    explicit ButtonPaintBuffer(const DRAWITEMSTRUCT& value):original(value),local(value){int w=value.rcItem.right-value.rcItem.left,h=value.rcItem.bottom-value.rcItem.top;if(w<=0||h<=0)return;memory=CreateCompatibleDC(value.hDC);if(memory)bitmap=CreateCompatibleBitmap(value.hDC,w,h);if(!bitmap){if(memory)DeleteDC(memory);memory=nullptr;return;}previous=SelectObject(memory,bitmap);local.hDC=memory;local.rcItem={0,0,w,h};}
    ~ButtonPaintBuffer(){if(memory){BitBlt(original.hDC,original.rcItem.left,original.rcItem.top,local.rcItem.right,local.rcItem.bottom,memory,0,0,SRCCOPY);SelectObject(memory,previous);DeleteObject(bitmap);DeleteDC(memory);}}
};
LRESULT CALLBACK owner_button_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    auto result=DefSubclassProc(h,msg,w,l);
    if(msg==WM_CANCELMODE||msg==WM_CAPTURECHANGED)SendMessageW(h,BM_SETSTATE,FALSE,0);
    if(msg==WM_SIZE||msg==WM_WINDOWPOSCHANGED||msg==WM_ENABLE||msg==WM_THEMECHANGED||msg==BM_SETSTATE||msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,owner_button_procedure,24);
    return result;
}
LRESULT CALLBACK overlay_procedure(HWND,UINT,WPARAM,LPARAM);
LRESULT CALLBACK sidebar_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_CHAR||msg==WM_SYSCHAR)return 0;
    return DefSubclassProc(h,msg,w,l);
}

LRESULT CALLBACK checkbox_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT rect;GetClientRect(h,&rect);
        HBRUSH brush=CreateSolidBrush(bg);FillRect(dc,&rect,brush);DeleteObject(brush);
        int size=14,y=(rect.bottom-size)/2;
        RECT box{2,y,2+size,y+size};brush=CreateSolidBrush(panel);FillRect(dc,&box,brush);DeleteObject(brush);
        brush=CreateSolidBrush(neon);FrameRect(dc,&box,brush);DeleteObject(brush);
        if(SendMessageW(h,BM_GETCHECK,0,0)==BST_CHECKED){
            HPEN pen=CreatePen(PS_SOLID,2,neon);auto old=SelectObject(dc,pen);
            MoveToEx(dc,5,y+7,nullptr);LineTo(dc,8,y+10);LineTo(dc,13,y+4);SelectObject(dc,old);DeleteObject(pen);
        }
        wchar_t label[512]{};GetWindowTextW(h,label,512);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);
        auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));
        rect.left=26;DrawTextW(dc,label,-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);SelectObject(dc,old);
        if(GetFocus()==h)DrawFocusRect(dc,&rect);
        EndPaint(h,&paint);return 0;
    }
    auto result=DefSubclassProc(h,msg,w,l);
    if(msg==BM_SETCHECK || msg==WM_LBUTTONUP || msg==WM_KEYUP || msg==WM_SETFOCUS || msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
    return result;
}
struct MouseSlider {bool dragging=false;double visual=10;};
LRESULT CALLBACK mouse_slider_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto state=reinterpret_cast<MouseSlider*>(data);
    auto move=[&](int x){RECT r;GetClientRect(h,&r);int inset=16;state->visual=1+19*std::clamp((x-inset)/static_cast<double>(std::max(1L,r.right-2*inset)),0.0,1.0);
        SendMessageW(h,TBM_SETPOS,FALSE,static_cast<LPARAM>(std::lround(state->visual)));InvalidateRect(h,nullptr,FALSE);UpdateWindow(h);};
    if(msg==WM_LBUTTONDOWN){SetFocus(h);state->dragging=true;SetCapture(h);move(static_cast<short>(LOWORD(l)));return 0;}
    if(msg==WM_MOUSEMOVE&&state->dragging){move(static_cast<short>(LOWORD(l)));return 0;}
    if(msg==WM_LBUTTONUP&&state->dragging){move(static_cast<short>(LOWORD(l)));state->dragging=false;ReleaseCapture();return 0;}
    if(msg==WM_CAPTURECHANGED||msg==WM_CANCELMODE){state->dragging=false;InvalidateRect(h,nullptr,FALSE);}
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT paint;auto target=BeginPaint(h,&paint);RECT r;GetClientRect(h,&r);
        HDC dc=CreateCompatibleDC(target);HBITMAP bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));auto old_bitmap=SelectObject(dc,bitmap);
        HBRUSH fill=CreateSolidBrush(bg);FillRect(dc,&r,fill);DeleteObject(fill);
        int cy=r.bottom/2,x=16+static_cast<int>((r.right-32)*(state->visual-1)/19);RECT track{16,cy-3,r.right-16,cy+3};
        fill=CreateSolidBrush(light_theme?RGB(207,207,216):RGB(48,48,56));FillRect(dc,&track,fill);DeleteObject(fill);track.right=x;fill=CreateSolidBrush(neon);FillRect(dc,&track,fill);DeleteObject(fill);
        HPEN edge=CreatePen(PS_SOLID,2,neon);auto pen=SelectObject(dc,edge);fill=CreateSolidBrush(light_theme?RGB(248,248,252):RGB(32,32,39));auto brush=SelectObject(dc,fill);Ellipse(dc,x-9,cy-9,x+10,cy+10);SelectObject(dc,pen);SelectObject(dc,brush);DeleteObject(edge);DeleteObject(fill);
        if(GetFocus()==h){RECT focus{2,2,r.right-2,r.bottom-2};DrawFocusRect(dc,&focus);}
        BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,old_bitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(h,&paint);return 0;
    }
    auto result=DefSubclassProc(h,msg,w,l);
    if(msg==TBM_SETPOS&&!state->dragging){state->visual=static_cast<double>(SendMessageW(h,TBM_GETPOS,0,0));InvalidateRect(h,nullptr,FALSE);}
    if(msg==WM_KEYDOWN||msg==WM_MOUSEWHEEL){state->visual=static_cast<double>(SendMessageW(h,TBM_GETPOS,0,0));InvalidateRect(h,nullptr,FALSE);}
    if(msg==WM_SETFOCUS||msg==WM_KILLFOCUS)InvalidateRect(h,nullptr,FALSE);
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(h,mouse_slider_procedure,1);delete state;}
    return result;
}
LRESULT CALLBACK combo_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    auto result=DefSubclassProc(h,msg,w,l);
    if(msg==WM_PAINT){
        auto dc=GetDC(h);RECT rect;GetClientRect(h,&rect);
        HBRUSH brush=CreateSolidBrush(RGB(35,76,43));FrameRect(dc,&rect,brush);DeleteObject(brush);
        RECT arrow{rect.right-22,rect.top+1,rect.right-1,rect.bottom-1};
        brush=CreateSolidBrush(panel);FillRect(dc,&arrow,brush);DeleteObject(brush);
        int cx=(arrow.left+arrow.right)/2,cy=(arrow.top+arrow.bottom)/2;
        POINT points[]={{cx-4,cy-2},{cx+4,cy-2},{cx,cy+3}};
        brush=CreateSolidBrush(neon);auto old=SelectObject(dc,brush);auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
        Polygon(dc,points,3);SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(brush);ReleaseDC(h,dc);
    }
    return result;
}
LRESULT CALLBACK header_procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT paint;HDC dc=BeginPaint(h,&paint);RECT rect;GetClientRect(h,&rect);
        HBRUSH brush=CreateSolidBrush(light_theme?RGB(230,234,239):RGB(29,29,32));FillRect(dc,&rect,brush);DeleteObject(brush);
        SetTextColor(dc,neon);SetBkMode(dc,TRANSPARENT);
        auto old=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));
        for(int i=0;i<Header_GetItemCount(h);++i){
            RECT item;Header_GetItemRect(h,i,&item);item.left+=9;
            wchar_t text[256]{};HDITEMW info{};info.mask=HDI_TEXT;info.pszText=text;info.cchTextMax=256;Header_GetItem(h,i,&info);
            DrawTextW(dc,text,-1,&item,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        }
        SelectObject(dc,old);EndPaint(h,&paint);return 0;
    }
    return DefSubclassProc(h,msg,w,l);
}
constexpr int nav_id=100, table_id=101;
constexpr wchar_t class_name[]=L"HeadroomNativeWindow";
const char* modes[]={"Gaming","Browsing","Creating"};
const wchar_t* tabs[]={L"Dashboard",L"Games",L"Power modes",L"Startup",L"Benchmarks",L"Drivers",L"Services",L"Settings",L"Temp cleaner",L"FPS optimizer",L"Debloater",L"Mouse tuning",L"Input remapper",L"Disks",L"Screen capture",L"Processes",L"Uninstaller",L"Storage lab",L"System tools",L"Packages",L"Gamer tools",L"Recovery",L"Capture gallery",L"Overlay editor",L"Instant replay",L"Game profiles",L"Microphone room",L"Display / audio",L"Maintenance scheduler",L"Mod profiles",L"Stutter timeline",L"Desktop Studio",L"Internet speed",L"Start and compare",L"Game session"};
struct NavItem {const wchar_t* label;int page;};
const NavItem navigation[]={
    {L"HOME",-1},{L"Dashboard",0},{L"Start & compare",33},{L"Power modes",2},{L"Settings",7},{L"Recovery",21},
    {L"GAMING",-1},{L"Game session",34},{L"Games",1},{L"Game profiles",25},{L"Mod profiles",29},{L"Overlay editor",23},{L"Benchmarks",4},{L"Stutter timeline",30},{L"Gamer tools",20},{L"FPS optimizer",9},
    {L"INPUT & DEVICES",-1},{L"Input remapper",12},{L"Mouse tuning",11},{L"Display / audio",27},{L"Drivers",5},
    {L"SYSTEM",-1},{L"Internet speed",32},{L"Processes",15},{L"Startup",3},{L"Services",6},{L"System tools",18},{L"Scheduler",28},
    {L"STORAGE & APPS",-1},{L"Disks",13},{L"Temp cleaner",8},{L"Storage lab",17},{L"Debloater",10},{L"Uninstaller",16},{L"Packages",19},
    {L"DESKTOP & MEDIA",-1},{L"Desktop Studio",31},{L"Screen capture",14},{L"Instant replay",24},{L"Capture gallery",22},{L"Microphone room",26}
};
using namespace hr::ui;
struct Result {
    std::string kind, error, text, game, mode;
    std::shared_ptr<hr::BoostPlan> boost;
    std::vector<hr::AudioEndpoint> audio;
    std::vector<hr::DisplayMode> displays;
    std::shared_ptr<hr::ModPlan> mod_plan;
    std::vector<hr::MediaEntry> media;
    hr::Sample sample;
    std::vector<hr::Service> services;
    hr::Drivers drivers;
    hr::Benchmark benchmark;
    std::vector<hr::TempFile> temp;
    hr::CleanupResult cleanup;
    hr::Records apps;
    hr::Row record;
    std::vector<hr::InstalledApp> installed;
    std::vector<hr::Autorun> autoruns;
    std::vector<hr::CacheTarget> targets;
    std::vector<hr::StorageFile> cache_files;
    hr::StorageAnalysis storage;
    hr::CommandResult command;
};
struct Mailbox {
    std::mutex mutex;
    std::deque<Result> results;
    std::atomic<bool> alive{true};
};
HICON native_icon(){
    auto icon=static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(101),IMAGE_ICON,32,32,0));
    return icon?icon:CopyIcon(LoadIconW(nullptr,IDI_APPLICATION));
}
std::wstring number(double n,int decimals=1) {
    std::wostringstream out; out<<std::fixed<<std::setprecision(decimals)<<n; return out.str();
}
std::wstring text(HWND h) {
    int n=GetWindowTextLengthW(h); std::wstring s(n+1,L'\0');
    GetWindowTextW(h,&s[0],n+1); s.resize(n); for(size_t i=0;(i=s.find(L"\r\n",i))!=std::wstring::npos;)s.erase(i,1); return s;
}
std::string startup_command(const hr::Startup& entry) {
    if(entry.type!=REG_SZ && entry.type!=REG_EXPAND_SZ)return "[non-text registry value]";
    if(entry.bytes.size()%sizeof(wchar_t))return "[invalid registry text]";
    std::wstring value(entry.bytes.size()/sizeof(wchar_t),L'\0');
    if(!value.empty())memcpy(&value[0],entry.bytes.data(),entry.bytes.size());
    while(!value.empty() && value.back()==L'\0')value.pop_back();
    return hr::utf8(value);
}
std::string app_title(const std::string& name){
    const std::map<std::string,std::string> names={
        {"Microsoft.BingNews","Microsoft News"},{"Microsoft.BingWeather","Weather"},
        {"Microsoft.GetHelp","Get Help"},{"Microsoft.Getstarted","Tips"},
        {"Microsoft.MicrosoftSolitaireCollection","Solitaire Collection"},{"Microsoft.People","People"},
        {"Microsoft.WindowsFeedbackHub","Feedback Hub"},{"Microsoft.YourPhone","Phone Link"},
        {"Microsoft.ZuneMusic","Media Player"},{"Microsoft.ZuneVideo","Movies & TV"},
        {"MicrosoftTeams","Microsoft Teams (personal)"},{"MSTeams","Microsoft Teams"},{"Clipchamp.Clipchamp","Clipchamp"}
    };
    auto found=names.find(name);return found==names.end()?name:found->second;
}
std::string app_version(const std::string& full){
    auto start=full.find('_');if(start==std::string::npos)return "";
    auto end=full.find('_',start+1);return full.substr(start+1,end-start-1);
}
class App {
public:
    hr::Store store;
    HIMAGELIST gallery_icons=nullptr;
    HWND gallery_search=nullptr,gallery_info=nullptr,gallery_tags=nullptr,gallery_game=nullptr;
    std::vector<hr::MediaEntry> gallery_files;std::vector<size_t> gallery_visible;
    std::vector<std::string> gallery_games;bool gallery_busy=false,gallery_filling=false;std::wstring gallery_keep;
    HIMAGELIST game_icons=nullptr;
    HWND window=nullptr,nav=nullptr,status=nullptr,title=nullptr,subtitle=nullptr,mode_label=nullptr,page_description=nullptr,brand_note=nullptr,sidebar_footer=nullptr;
    std::unique_ptr<hr::DesktopStudio> desktop;HWND desktop_editor=nullptr;unsigned long long desktop_game_check=0;bool desktop_gaming=false;
    struct NetworkLive{std::mutex mutex;hr::NetworkProgress value;};std::shared_ptr<NetworkLive> network_live=std::make_shared<NetworkLive>();std::shared_ptr<std::atomic<bool>> network_cancel=std::make_shared<std::atomic<bool>>(false);bool network_busy=false;HWND network_go=nullptr,network_cards=nullptr,network_mode=nullptr,network_note=nullptr,network_report=nullptr;
    bool comparison_busy=false;std::shared_ptr<std::atomic<bool>> comparison_cancel=std::make_shared<std::atomic<bool>>(false);HWND comparison_intro=nullptr,comparison_output=nullptr,comparison_state=nullptr;
    HWND global_search=nullptr,center_game=nullptr,center_status=nullptr,center_report=nullptr,font_choice=nullptr,reduced_motion=nullptr,update_note=nullptr;std::vector<size_t> nav_visible;std::vector<std::string> center_paths;bool navigation_filtering=false;unsigned long long center_session_seen=0;
    HWND lists[tab_count]{},summary=nullptr,gpu_label=nullptr,metrics=nullptr,duration=nullptr;
    HWND plan_combo[3]{},key_edit[3]{},presentmon=nullptr,notifications=nullptr,hidden=nullptr;
    HWND driver_offers=nullptr,glass_toggle=nullptr;
    std::vector<HWND> controls[tab_count];
    HFONT font=nullptr,heading_font=nullptr,brand_font=nullptr,sidebar_font=nullptr;
    HWND hub_run=nullptr,hub_favorite=nullptr,hub_cancel_button=nullptr,hub_browse1=nullptr,hub_browse2=nullptr;
    bool hub_has_first=false,hub_has_second=false,hub_path_first=false,hub_path_second=false;std::string hub_selected,hub_live,hub_pending_id;std::map<std::string,std::wstring> hub_reports;std::map<std::string,std::pair<std::wstring,std::wstring>> hub_inputs;
    HWND hub_category=nullptr,hub_search=nullptr,hub_description=nullptr,hub_label1=nullptr,hub_label2=nullptr,hub_first=nullptr,hub_second=nullptr,hub_output=nullptr,recovery_note=nullptr;
    std::vector<hr::GamerTool> hub_tools=hr::gamer_tools(),visible_hub;std::vector<size_t> recovery_indices;
    bool hub_busy=false;hr::Records controller_archive;bool controller_log_loaded=false;std::string controller_event;
    std::map<std::string,std::string> battery_states;ULONGLONG next_input_alert=0,thermal_since=0,last_thermal_alert=0;std::shared_ptr<std::atomic<bool>> hub_cancel=std::make_shared<std::atomic<bool>>(false);
    HBRUSH background=CreateSolidBrush(bg),panel_brush=CreateSolidBrush(panel);
    int page=0,dpi=96;
    std::vector<hr::Plan> power_plans;
    std::vector<hr::Startup> startups;
    std::vector<hr::Service> service_rows;
    hr::Sample last;hr::GameSessions sessions;
    struct ResourcePoint{ULONGLONG tick;double cpu,ram,gpu,read,write,top_cpu;std::wstring process;std::vector<hr::Process> processes;};std::deque<ResourcePoint> resource_history;
#include "advanced_members.inc"
    hr::Drivers driver_rows;
    hr::Records games,benchmarks,disabled;
    std::shared_ptr<Mailbox> mailbox=std::make_shared<Mailbox>();
    bool monitoring=false,scanning_services=false,scanning_drivers=false,recording=false,tray=false,dirty=false;
    UINT taskbar_created=0;
    unsigned smoke_tick=0;
    hr::OverlaySettings overlay_settings;
    HWND overlay_anchor=nullptr,overlay_scale=nullptr,overlay_accent=nullptr,overlay_layout=nullptr,overlay_display=nullptr,overlay_opacity=nullptr,overlay_x=nullptr,overlay_y=nullptr,overlay_preview=nullptr;HWND overlay_metrics[6]{};
    HWND overlay=nullptr;
    hr::LiveCapture live;
    std::string overlay_game;
    std::vector<hr::TempFile> temp_files;
    hr::Records optional;
    bool temp_busy=false,app_busy=false,apps_loaded=false;
    HWND temp_summary=nullptr;
    HWND gauges[5]{},mouse_speed=nullptr,mouse_accel=nullptr,mouse_absolute=nullptr,mouse_readback=nullptr,remap_device=nullptr,remap_button=nullptr,target_device=nullptr,target_button=nullptr;
    HWND remap_enabled=nullptr,remap_game_only=nullptr,stick_mouse=nullptr,input_status=nullptr,capture_status=nullptr;
    std::vector<std::pair<std::string,unsigned>> source_buttons,target_buttons;
    hr::InputEngine input;
    hr::Recorder recorder;
    std::vector<hr::Drive> disk_rows;
    unsigned animation=0;
    std::deque<double> cpu_trend,ram_trend,gpu_trend;
    ULONGLONG boost_flash=0;bool boost_busy=false;std::wstring boost_phase=L"BOOST";
    HWND fix_review=nullptr;bool suggestions_scanned=false;
    HWND suggestion_note=nullptr,suggestion_scan=nullptr,suggestion_fix=nullptr;
    bool suggestions_busy=false,suggestion_capture=false;std::vector<hr::TempFile> suggestion_files;
    HWND dashboard_history[3]{},dashboard_modes[3]{},pc_label=nullptr,device_view=nullptr,listen_button=nullptr;
    HWND benchmark_kind=nullptr,stress_seconds=nullptr,stress_cpu=nullptr,stress_ram=nullptr,stress_gpu=nullptr,stress_status=nullptr,stress_list=nullptr;
    std::vector<HWND> fps_controls,stress_controls,disk_cards;
    struct DeviceRegion {RECT rect;unsigned code;};
    std::vector<DeviceRegion> device_regions;
    unsigned selected_source=0;ULONGLONG source_flash=0;
    int selected_disk=0;
    hr::StressTest stress;
    bool stress_saved=true,installed_busy=false;
    HWND app_search=nullptr;
    std::vector<hr::InstalledApp> installed,visible_installed;
    std::vector<hr::Autorun> autorun_rows;
    bool startup_busy=false,storage_busy=false,tool_busy=false,cache_preview=false; HWND active_command_output=nullptr;
    HWND storage_mode=nullptr,storage_path=nullptr,storage_summary=nullptr,tool_category=nullptr,tool_description=nullptr,tool_output=nullptr,package_query=nullptr,package_output=nullptr;
    std::vector<hr::CacheTarget> cache_roots;
    std::vector<hr::StorageFile> cache_files;
    hr::StorageAnalysis storage_result;
    std::vector<std::pair<size_t,size_t>> duplicate_rows;
    std::shared_ptr<std::atomic<bool>> storage_cancel=std::make_shared<std::atomic<bool>>(false);
    std::vector<hr::SystemTool> tools,visible_tools;
    std::filesystem::path active_log;std::string system_selected,system_pending;std::map<std::string,std::wstring> system_reports;
    std::map<DWORD,std::unique_ptr<hr::SleepSession>> sleeping;
    std::filesystem::path smoke_report;
    std::vector<std::string> smoke_errors;
    explicit App(std::filesystem::path smoke={}) : smoke_report(std::move(smoke)) {
        overlay_settings=hr::read_overlay(store);
        if(store.get("theme","dark")!="dark"){store.set("theme","dark");store.save();}
        hr::scrollbar_theme(false);
        bg=light_theme?RGB(245,246,248):RGB(13,13,15);panel=light_theme?RGB(255,255,255):RGB(19,19,22);ink=light_theme?RGB(30,35,43):RGB(231,235,242);
        neon=light_theme?RGB(0,128,46):RGB(26,242,44);
    }
    ~App() { *comparison_cancel=true;*network_cancel=true;mailbox->alive=false;if(gallery_icons)ImageList_Destroy(gallery_icons);if(game_icons)ImageList_Destroy(game_icons); if(sidebar_font)DeleteObject(sidebar_font);if(font)DeleteObject(font);if(heading_font)DeleteObject(heading_font);if(brand_font)DeleteObject(brand_font);DeleteObject(background);DeleteObject(panel_brush); }
    int px(int n)const{return MulDiv(n,dpi,96);}
    struct Placement{HWND control;int x,y,w,h;};std::vector<Placement> placements;bool collecting_layout=false;
    void position(HWND h,int x,int y,int w,int height){if(!h)return;Placement next{h,px(x),px(y),std::max(1,px(w)),std::max(1,px(height))};RECT before{};GetWindowRect(h,&before);MapWindowPoints(nullptr,GetParent(h),reinterpret_cast<POINT*>(&before),2);if(before.left==next.x&&before.top==next.y&&before.right-before.left==next.w&&before.bottom-before.top==next.h)return;if(collecting_layout)placements.push_back(next);else{SetWindowPos(h,nullptr,next.x,next.y,next.w,next.h,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOCOPYBITS);InvalidateRect(h,nullptr,FALSE);}}
    void ensure_navigation_visible(){if(!nav)return;int selected=static_cast<int>(SendMessageW(nav,LB_GETCURSEL,0,0));if(selected<0)return;int top=static_cast<int>(SendMessageW(nav,LB_GETTOPINDEX,0,0));RECT viewport{};GetClientRect(nav,&viewport);if(selected<top){SendMessageW(nav,LB_SETTOPINDEX,selected,0);return;}int used=0;for(int i=top;i<=selected;++i)used+=std::max(1,static_cast<int>(SendMessageW(nav,LB_GETITEMHEIGHT,i,0)));while(top<selected&&used>viewport.bottom){used-=std::max(1,static_cast<int>(SendMessageW(nav,LB_GETITEMHEIGHT,top,0)));++top;}SendMessageW(nav,LB_SETTOPINDEX,top,0);}
    void finish_layout(){collecting_layout=false;if(placements.empty())return;auto batch=BeginDeferWindowPos(static_cast<int>(placements.size()));for(auto p:placements){if(!batch)break;batch=DeferWindowPos(batch,p.control,nullptr,p.x,p.y,p.w,p.h,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOCOPYBITS);}bool applied=batch&&EndDeferWindowPos(batch);if(!applied)for(auto p:placements)SetWindowPos(p.control,nullptr,p.x,p.y,p.w,p.h,SWP_NOACTIVATE|SWP_NOZORDER|SWP_NOCOPYBITS);placements.clear();ensure_navigation_visible();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);}

    HWND make(const wchar_t* cls,const wchar_t* label,DWORD style,int id,int tab=-1) {
        if(wcscmp(cls,WC_COMBOBOXW)==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
        HWND h=CreateWindowExW(0,cls,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!h)throw std::runtime_error(hr::error());
        hr::style_scrollbars(h,light_theme);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        if(wcscmp(cls,L"BUTTON")==0 && (style&BS_TYPEMASK)==BS_OWNERDRAW)SetWindowSubclass(h,owner_button_procedure,24,0);
        if(wcscmp(cls,L"BUTTON")==0 && (style&BS_TYPEMASK)==BS_AUTOCHECKBOX)SetWindowSubclass(h,checkbox_procedure,1,0);
        if(wcscmp(cls,WC_COMBOBOXW)==0){SetWindowSubclass(h,combo_procedure,1,0);SendMessageW(h,CB_SETITEMHEIGHT,static_cast<WPARAM>(-1),px(24));}
        if(tab>=0)controls[tab].push_back(h);return h;
    }
    HWND label(const wchar_t* s,int tab) {return make(L"STATIC",s,SS_LEFT,0,tab);}
    HWND button(const wchar_t* s,int id,int tab) {return make(L"BUTTON",s,WS_TABSTOP|BS_OWNERDRAW,id,tab);}
    HWND list(int tab,const std::vector<std::pair<const wchar_t*,int>>& columns,int identity=-1) {
        HWND h=make(WC_LISTVIEWW,L"",WS_TABSTOP|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS,identity<0?table_id+tab:identity,tab);
        ListView_SetExtendedListViewStyle(h,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP);
        SetWindowTheme(h,light_theme?L"Explorer":L"DarkMode_Explorer",nullptr);
        ListView_SetBkColor(h,bg);ListView_SetTextBkColor(h,bg);ListView_SetTextColor(h,ink);
        SetWindowSubclass(ListView_GetHeader(h),header_procedure,1,0);
        int i=0;
        for(const auto& column:columns){
            LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH|LVCF_SUBITEM;c.pszText=const_cast<wchar_t*>(column.first);c.cx=px(column.second);c.iSubItem=i;
            ListView_InsertColumn(h,i++,&c);
        }
        return h;
    }
    void add(HWND h,const std::vector<std::wstring>& cells) {
        int index=ListView_GetItemCount(h);
        for(size_t i=0;i<cells.size();++i){
            if(!i){LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=index;item.pszText=const_cast<wchar_t*>(cells[i].c_str());ListView_InsertItem(h,&item);}
            else ListView_SetItemText(h,index,static_cast<int>(i),const_cast<wchar_t*>(cells[i].c_str()));
        }
    }
    int selection(HWND list)const{return ListView_GetNextItem(list,-1,LVNI_SELECTED);}
    std::string selected_game()const{
        int i=selection(lists[1]);return i>=0 && static_cast<size_t>(i)<games.size()?games[i][2]:"";
    }
    void say(const std::wstring& message){SetWindowTextW(status,message.c_str());}
    bool confirm(const std::wstring& message){return hr::app_dialog(window,L"Review change",message,true,light_theme);}
    void failure(const std::string& error){
        say(hr::wide(error));try{store.log("Operation failed",error);}catch(const std::exception&){}
        if(!smoke_report.empty())smoke_errors.push_back(error);
        else hr::app_dialog(window,L"NovaTune",hr::wide(error),false,light_theme);
    }
    template<class Work> void async(const std::string& kind,Work work) {
        auto box=mailbox;
        std::thread([box,kind,work=std::move(work)]()mutable{
            Result result;result.kind=kind;
            try{work(result);}catch(const std::exception& e){result.error=e.what();}
            catch(...){result.error="Unexpected background task failure";}
            if(box->alive){std::lock_guard<std::mutex> lock(box->mutex);box->results.push_back(std::move(result));}
        }).detach();
    }
    void apply_theme(){
        hr::scrollbar_theme(false);
        bg=light_theme?RGB(245,246,248):RGB(13,13,15);panel=light_theme?RGB(255,255,255):RGB(19,19,22);ink=light_theme?RGB(30,35,43):RGB(231,235,242);
        neon=light_theme?RGB(0,128,46):RGB(26,242,44);
        DeleteObject(background);DeleteObject(panel_brush);background=CreateSolidBrush(bg);panel_brush=CreateSolidBrush(panel);
        BOOL dark=light_theme?FALSE:TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);
        for(auto list:lists)if(list){SetWindowTheme(list,light_theme?L"Explorer":L"DarkMode_Explorer",nullptr);ListView_SetBkColor(list,bg);ListView_SetTextBkColor(list,bg);ListView_SetTextColor(list,ink);}
        for(auto list:{driver_offers,stress_list,mod_changes})if(list){ListView_SetBkColor(list,bg);ListView_SetTextBkColor(list,bg);ListView_SetTextColor(list,ink);}
        SetWindowTheme(nav,light_theme?L"Explorer":L"DarkMode_Explorer",nullptr);
        if(desktop)desktop->theme(light_theme);apply_glass();RedrawWindow(window,nullptr,nullptr,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    }
    void apply_glass(){
        bool enabled=!light_theme && store.get("glass","1")=="1";
        auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
        SetWindowLongPtrW(window,GWL_EXSTYLE,enabled?style|WS_EX_LAYERED:style&~WS_EX_LAYERED);
        if(enabled)SetLayeredWindowAttributes(window,0,250,LWA_ALPHA);
                                                                              
                                                                                
        int backdrop=1;DwmSetWindowAttribute(window,38,&backdrop,sizeof backdrop);
        int corners=2;DwmSetWindowAttribute(window,33,&corners,sizeof corners);
        COLORREF caption=bg,border=RGB(38,40,45),caption_text=ink;
        DwmSetWindowAttribute(window,35,&caption,sizeof caption);
        DwmSetWindowAttribute(window,36,&caption_text,sizeof caption_text);
        DwmSetWindowAttribute(window,34,&border,sizeof border);
        MARGINS margins{0,0,0,0};DwmExtendFrameIntoClientArea(window,&margins);
        struct Accent {int state,flags;DWORD color;int animation;};
        struct Composition {int attribute;void* data;SIZE_T size;};
        using SetComposition=BOOL(WINAPI*)(HWND,Composition*);
        auto user=GetModuleHandleW(L"user32.dll");
        auto set=reinterpret_cast<SetComposition>(GetProcAddress(user,"SetWindowCompositionAttribute"));
        if(set){Accent accent{0,0,0,0};Composition data{19,&accent,sizeof accent};set(window,&data);}
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    }
#include "advanced_ui.inc"
#include "advanced_timeline.inc"
#include "advanced_init.inc"
#include "advanced_actions.inc"
    void initialize(HWND h){
        window=h; BOOL dark=light_theme?FALSE:TRUE;DwmSetWindowAttribute(h,20,&dark,sizeof dark); dpi=GetDpiForWindow(h);if(!dpi)dpi=96;
        font=CreateFontW(-px(store.get("text.large")=="1"?16:14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        sidebar_font=CreateFontW(-px(store.get("text.large")=="1"?18:16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        heading_font=CreateFontW(-px(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        brand_font=CreateFontW(-px(28),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Bahnschrift");
        title=make(L"STATIC",L"NovaTune",SS_OWNERDRAW,1006);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(brand_font),TRUE);
        brand_note=label(L"PERFORMANCE SUITE",-1);
        subtitle=label(L"Dashboard",-1);SendMessageW(subtitle,WM_SETFONT,reinterpret_cast<WPARAM>(heading_font),TRUE);
        page_description=label(L"Your system, at a glance.",-1);
        sidebar_footer=label(L"",-1);
        mode_label=make(L"STATIC",L"Browsing mode",SS_OWNERDRAW,1005);
        nav=make(L"LISTBOX",L"",WS_VSCROLL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|LBS_OWNERDRAWVARIABLE|LBS_HASSTRINGS,nav_id);
        SetWindowSubclass(nav,sidebar_procedure,1,0);
        SetWindowTheme(nav,light_theme?L"Explorer":L"DarkMode_Explorer",nullptr);
        filter_navigation();
        SendMessageW(nav,WM_SETFONT,reinterpret_cast<WPARAM>(sidebar_font),TRUE);
        status=label(L"Ready",-1);
        metrics=label(L"CPU: sampling...     RAM: sampling...",15);
        gpu_label=label(L"GPU: sampling...",15);
        button(L"Close selected app",CloseApp,15);
        label(L"Largest apps by RAM. Close asks visible windows to exit; save work first.",15);
        lists[15]=list(15,{{L"Application",320},{L"PID",90},{L"RAM MB",130},{L"CPU %",110}});
        button(L"Optimize RAM",TrimRam,15);
        for(int i=0;i<5;++i)gauges[i]=make(L"STATIC",i==4?L"BOOST":L"",SS_OWNERDRAW|SS_NOTIFY,1000+i,0);
        button(L"Add game .exe",AddGame,1);button(L"Remove",RemoveGame,1);button(L"Launch selected game",LaunchGame,1);
        lists[1]=list(1,{{L"Game",240},{L"Executable",570}});
        SetWindowLongPtrW(lists[1],GWL_STYLE,(GetWindowLongPtrW(lists[1],GWL_STYLE)&~LVS_TYPEMASK)|LVS_ICON|LVS_AUTOARRANGE);
        ListView_SetExtendedListViewStyle(lists[1],LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_INFOTIP);
        game_icons=ImageList_Create(px(64),px(64),ILC_COLOR32|ILC_MASK,8,8);ListView_SetImageList(lists[1],game_icons,LVSIL_NORMAL);
        ListView_SetIconSpacing(lists[1],px(170),px(128));
        label(L"Capture seconds (10-300):",1);
        duration=make(L"EDIT",L"30",WS_BORDER|WS_TABSTOP|ES_NUMBER,0,1);
        button(L"Record with PresentMon",Capture,1);
        label(L"Same scene + same settings = useful comparisons. Overlay works in windowed / borderless games. Ctrl+Alt+O toggles.",1);
        button(L"Toggle game overlay",ToggleOverlay,1);
        dashboard_modes[0]=button(L"Gaming",Gaming,0);dashboard_modes[1]=button(L"Browsing",Browsing,0);dashboard_modes[2]=button(L"Creating",Creating,0);
        auto info=hr::pc_info();std::wstring pc;
        for(size_t i=0;i<info.size();++i){pc+=hr::wide(info[i][0]+": "+info[i][1]);if(i+1<info.size())pc+=i==1 || i==3?L"\n":L"   /   ";}
        pc_label=label((L"SYSTEM HARDWARE\n"+pc).c_str(),0);
        for(int i=0;i<3;++i)dashboard_history[i]=make(L"STATIC",L"",SS_OWNERDRAW,1300+i,0);
        suggestion_note=label(L"SUGGESTED FIXES  /  Scan for old temp files and enabled background game recording.",0);
        suggestion_scan=button(L"Scan PC",ScanSuggestions,0);suggestion_fix=button(L"Review fixes",FixSuggestions,0);EnableWindow(suggestion_fix,FALSE);
        label(L"Use existing Windows power plans. No change keeps the current plan.",2);
        power_plans=hr::plans();
        for(int i=0;i<3;++i){
            label(hr::wide(modes[i]).c_str(),2);
            plan_combo[i]=make(WC_COMBOBOXW,L"",WS_TABSTOP|CBS_DROPDOWNLIST|WS_VSCROLL,0,2);
            SendMessageW(plan_combo[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No change"));
            int selected=0,index=1;
            for(const auto& p:power_plans){SendMessageW(plan_combo[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(p.name.c_str()));if(hr::lower(store.get(std::string("plan.")+modes[i]))==hr::lower(p.guid))selected=index;++index;}
            SendMessageW(plan_combo[i],CB_SETCURSEL,selected,0);
            auto binding=store.get(std::string("key.")+modes[i],std::string("Ctrl+Alt+")+modes[i][0]);
            key_edit[i]=make(L"EDIT",hr::wide(binding).c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,0,2);
        }
        label(L"Global shortcuts: Ctrl, Alt, Shift + letter, digit or F1-F12.",2);
        button(L"Save plans and hotkeys",SaveModes,2);button(L"Undo last Windows change",Undo,2);
        label(L"Modes change only the selected power plan; they do not guarantee more FPS.",2);
        button(L"Refresh",Refresh,3);button(L"Disable selected",DisableStartup,3);button(L"Restore selected",RestoreStartup,3);
        lists[3]=list(3,{{L"Name",190},{L"Source",110},{L"State",90},{L"Risk",180},{L"Command",500}});
        label(L"Registry Run keys, Startup folders and tasks. OS/security entries stay protected. Review indicators are not community safety ratings.",3);
        button(L"Import PresentMon CSV",Import,4);button(L"Compare with previous",Compare,4);
        lists[4]=list(4,{{L"Date",150},{L"Game",170},{L"Mode",85},{L"Avg FPS",85},{L"1% low",85},{L"P95 ms",85},{L"Stutters",75},{L"Metric",180}});
        summary=label(L"Select a game in Games to filter an import. Compare the same game, metric and scene.",4);
        fps_controls=controls[4];
        benchmark_kind=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,409,4);
        SendMessageW(benchmark_kind,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"System stress test"));SendMessageW(benchmark_kind,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"FPS comparison"));SendMessageW(benchmark_kind,CB_SETCURSEL,0,0);
        size_t stress_begin=controls[4].size();
        label(L"Duration (5-300 seconds)",4);stress_seconds=make(L"EDIT",L"30",WS_BORDER|ES_NUMBER|WS_TABSTOP,410,4);
        stress_cpu=make(L"BUTTON",L"CPU",BS_AUTOCHECKBOX|WS_TABSTOP,0,4);stress_ram=make(L"BUTTON",L"RAM",BS_AUTOCHECKBOX|WS_TABSTOP,0,4);stress_gpu=make(L"BUTTON",L"GPU compute",BS_AUTOCHECKBOX|WS_TABSTOP,0,4);
        SendMessageW(stress_cpu,BM_SETCHECK,BST_CHECKED,0);SendMessageW(stress_ram,BM_SETCHECK,BST_CHECKED,0);
        button(L"Start stress test",StartStress,4);button(L"Stop",StopStress,4);
        stress_status=label(L"Ready. Load tests increase power and temperature. Save work; stop at any time.",4);
        stress_list=list(4,{{L"Date",160},{L"Seconds",80},{L"CPU rounds",130},{L"RAM verified GB",160},{L"GPU dispatches",140},{L"Errors",80}});SetWindowLongPtrW(stress_list,GWLP_ID,420);
        for(size_t i=stress_begin;i<controls[4].size();++i)stress_controls.push_back(controls[4][i]);
        button(L"Check drivers + updates",CheckDrivers,5);button(L"Windows optional updates",OptionalUpdates,5);button(L"Vendor downloads",Vendor,5);
        label(L"Installed display drivers. An empty Windows Update result does not prove the latest vendor version.",5);
        lists[5]=list(5,{{L"Device",280},{L"Manufacturer",190},{L"Version",180},{L"Date",130}});
        driver_offers=list(5,{{L"Windows Update driver offers (read-only)",800}});
        label(L"NovaTune never downloads or installs drivers. Review offers in Windows Settings.",5);
        button(L"Refresh",Refresh,6);button(L"Automatic",ServiceAuto,6);button(L"Manual",ServiceManual,6);button(L"Disable",ServiceDisabled,6);button(L"Undo",Undo,6);
        lists[6]=list(6,{{L"Display name",270},{L"Service",180},{L"State",100},{L"Startup",130},{L"Editable",90},{L"Executable",500}});
        label(L"Conservative third-party filter. Changes affect startup only; services keep running. Some require administrator rights.",6);
        label(L"PresentMon executable:",7);
        presentmon=make(L"EDIT",hr::wide(store.get("presentmon")).c_str(),WS_TABSTOP|WS_BORDER|ES_AUTOHSCROLL,465,7);
        button(L"Browse...",PickPresentMon,7);
        notifications=make(L"BUTTON",L"Show mode notifications",WS_TABSTOP|BS_AUTOCHECKBOX,0,7);
        hidden=make(L"BUTTON",L"Start hidden to tray",WS_TABSTOP|BS_AUTOCHECKBOX,0,7);
        SendMessageW(notifications,BM_SETCHECK,store.get("notifications","1")=="1"?BST_CHECKED:BST_UNCHECKED,0);
        SendMessageW(hidden,BM_SETCHECK,store.get("hidden","0")=="1"?BST_CHECKED:BST_UNCHECKED,0);
        button(L"Save settings",SaveSettings,7);button(L"Open data folder",OpenData,7);button(L"Quit NovaTune",Quit,7);
        label(L"Change history",7);
        lists[7]=list(7,{{L"Date",170},{L"Action",220},{L"Detail",470}});
        glass_toggle=make(L"BUTTON",L"Black glass transparency",WS_TABSTOP|BS_AUTOCHECKBOX,0,7);
        SendMessageW(glass_toggle,BM_SETCHECK,store.get("glass","1")=="1"?BST_CHECKED:BST_UNCHECKED,0);
        button(L"Scan user temp",ScanTemp,8);button(L"Clean checked files",CleanTemp,8);
        temp_summary=label(L"Preview files older than 24 hours in your local Temp folder. Checked files are permanently deleted. Locked files and links are skipped.",8);
        lists[8]=list(8,{{L"Temporary file",620},{L"Size MB",120}});
        ListView_SetExtendedListViewStyle(lists[8],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_CHECKBOXES);
        label(L"Build a repeatable gaming profile",9);
        label(L"Enable Windows Game Mode and turn off background Game DVR recording for your account.\nApply the power plan selected for Gaming in Modes. Each change has an Undo record.",9);
        button(L"Apply FPS profile",ApplyFPS,9);button(L"Undo last change",Undo,9);
        label(L"1   Choose an existing Gaming power plan in Modes.\n\n2   Close unnecessary apps from Dashboard. RAM trimming is temporary and can add paging.\n\n3   Apply this profile, launch your game, and capture the same scene with PresentMon.\n\n4   Compare average FPS, 1% low and P95 in Benchmarks. Gains depend on your bottleneck.",9);
        button(L"Scan optional apps",ScanApps,10);button(L"Remove selected app",RemoveApp,10);button(L"Reduce suggestions",ReduceSuggestions,10);
        lists[10]=list(10,{{L"Optional app (current user)",430},{L"Version",350}});
        label(L"Only listed consumer apps can be removed. Store, Edge, runtimes, security and Xbox gaming services are excluded.\nRemoval may require downloading the app again from Microsoft Store. Undo tries local restoration first.",10);
        button(L"Undo / reinstall last app",ReinstallApp,10);
        button(L"One-click debloat",OneClickDebloat,10);
        button(L"Windows disk cleanup",OpenDiskCleanup,8);
        mouse_readback=make(L"STATIC",L"Windows pointer settings",SS_LEFT,466,11);
        label(L"Pointer speed (1-20). A middle value of 10 is the conventional 6/11 Windows setting.",11);
        mouse_speed=make(TRACKBAR_CLASSW,L"",WS_TABSTOP|TBS_NOTICKS,513,11);
        SetWindowSubclass(mouse_speed,mouse_slider_procedure,1,reinterpret_cast<DWORD_PTR>(new MouseSlider));
        SendMessageW(mouse_speed,TBM_SETRANGE,TRUE,MAKELPARAM(1,20));
        auto current_mouse=hr::mouse_settings();SendMessageW(mouse_speed,TBM_SETPOS,TRUE,current_mouse.speed);
        mouse_accel=make(L"BUTTON",L"Enhance pointer precision / acceleration",WS_TABSTOP|BS_AUTOCHECKBOX,514,11);
        SendMessageW(mouse_accel,BM_SETCHECK,current_mouse.acceleration?BST_CHECKED:BST_UNCHECKED,0);
        button(L"Apply mouse settings",SaveMouse,11);button(L"Gaming preset",MousePreset,11);button(L"Undo last change",Undo,11);button(L"Windows mouse settings",OpenMouse,11);
        label(L"",11);
        mouse_absolute=make(L"BUTTON",L"Scale VM/tablet pointer while NovaTune runs",WS_TABSTOP|BS_AUTOCHECKBOX,516,11);SendMessageW(mouse_absolute,BM_SETCHECK,store.get("mouse.absolute.scale","1")=="1"?BST_CHECKED:BST_UNCHECKED,0);
        label(L"Source device / button                         Output device / button",12);
        remap_device=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,400,12);
        remap_button=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,401,12);
        target_device=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,402,12);
        target_button=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,403,12);
        for(auto name:{L"Keyboard",L"Mouse",L"Xbox",L"PlayStation"})SendMessageW(remap_device,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        for(auto name:{L"Keyboard",L"Mouse"})SendMessageW(target_device,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));
        SendMessageW(remap_device,CB_SETCURSEL,0,0);SendMessageW(target_device,CB_SETCURSEL,0,0);populate_buttons();
        button(L"Add / replace mapping",AddMapping,12);button(L"Remove selected",RemoveMapping,12);
        remap_enabled=make(L"BUTTON",L"Enable remapping",WS_TABSTOP|BS_AUTOCHECKBOX,468,12);
        remap_game_only=make(L"STATIC",L"System-wide / all Windows apps",SS_LEFT,469,12);
        stick_mouse=make(L"BUTTON",L"Controller right stick moves mouse",WS_TABSTOP|BS_AUTOCHECKBOX,512,12);
        if(store.get("remap.game_only")=="1"){store.set("remap.game_only","0");store.set("remap.enabled","0");store.save();}
        SendMessageW(remap_enabled,BM_SETCHECK,store.get("remap.enabled","0")=="1"?BST_CHECKED:BST_UNCHECKED,0);

        SendMessageW(stick_mouse,BM_SETCHECK,store.get("remap.stick","0")=="1"?BST_CHECKED:BST_UNCHECKED,0);
        button(L"Save profile",SaveMapping,12);
        lists[12]=list(12,{{L"Source device",160},{L"Source button",250},{L"Output",310}});
        input_status=make(L"STATIC",L"Controller discovery...",SS_LEFT,515,12);
        label(L"Xbox: XInput. PlayStation: Sony HID buttons / d-pad. Output is keyboard or mouse, not a virtual gamepad.\nMappings apply system-wide after Start. Press Pause or Stop to release mapped buttons immediately.",12);
        listen_button=button(L"Capture input",ListenInput,12);
        device_view=make(L"STATIC",L"",SS_OWNERDRAW|SS_NOTIFY,1200,12);button(L"Start remapping",StartRemapping,12);button(L"Stop remapping",StopRemapping,12);
        button(L"Refresh drives",RefreshDisks,13);button(L"Analyze drive",AnalyzeDisk,13);button(L"Optimize drive",OptimizeDisk,13);button(L"Windows cleanup",OpenDiskCleanup,13);button(L"Optimize Drives UI",OpenOptimizeDrives,13);
        label(L"Windows /O chooses the right operation: HDD defragmentation or SSD optimization / retrim.\nAnalyze / Optimize opens a Windows console and may request administrator approval. No scheduled operations are changed.",13);
        button(L"Screenshot PNG",Screenshot,14);button(L"Start MP4 recording",StartRecording,14);button(L"Stop and save",StopRecording,14);button(L"Open captures",OpenCaptures,14);
        capture_status=label(L"Ready. Capture the primary display. MP4: 1280 x 720, up to 30 FPS, no audio.",14);
        label(L"Ctrl+Alt+P: screenshot   |   Ctrl+Alt+R: start / stop recording\n\nFiles are saved to your capture folder. The cursor is included. Protected video and exclusive-fullscreen games may not capture.\n\nRecording uses CPU/GPU resources; benchmark with recording off when you want comparable performance results.",14);
        app_search=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,411,16);
        SendMessageW(app_search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search desktop apps or publishers"));
        button(L"Refresh apps",RefreshInstalled,16);button(L"Uninstall selected",UninstallSelected,16);button(L"Windows Apps settings",OpenWindowsApps,16);
        lists[16]=list(16,{{L"Application",340},{L"Publisher",250},{L"Version",160},{L"Scope",100}});
        label(L"Opens the publisher's registered uninstaller. Store apps are managed in Debloater.\nNative uninstallers and Windows UAC retain their own windows.",16);
        button(L"Sleep selected",SleepSelected,15);button(L"Resume selected",ResumeSelected,15);button(L"Resume all",ResumeAll,15);
        storage_mode=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,412,17);
        for(auto name:{L"Cache cleaner",L"Folder breakdown",L"Duplicate finder"})SendMessageW(storage_mode,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));SendMessageW(storage_mode,CB_SETCURSEL,0,0);
        storage_path=make(L"EDIT",hr::data_dir().parent_path().parent_path().parent_path().wstring().c_str(),WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,0,17);
        button(L"Choose folder",StorageChoose,17);button(L"Scan / analyze",StorageScan,17);button(L"Stop scan",StorageStop,17);
        button(L"Clean / recycle checked",StorageClean,17);button(L"Choose cache locations",StorageRoots,17);button(L"Reveal selected",RevealFile,17);
        storage_summary=label(L"Choose cache locations or a folder. Password files are never included. Duplicate deletion is opt-in and keeps one copy.",17);
        lists[17]=list(17,{{L"Location / file",580},{L"Size",120},{L"Details",230}});
        tools=hr::system_tools();
        tool_category=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,413,18);
        for(auto name:{L"All tools",L"Repair",L"AI",L"Gaming",L"GPU",L"Power",L"Network",L"Shell",L"Security"})SendMessageW(tool_category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name));SendMessageW(tool_category,CB_SETCURSEL,0,0);
        button(L"Refresh details",ToolInspect,18);button(L"Run selected",ToolRun,18);button(L"Revert last change",Undo,18);button(L"Logs / backups",OpenLogs,18);
        lists[18]=list(18,{{L"Tool",390},{L"Category",100},{L"Review",230}});
        tool_description=label(L"Choose a tool. Destructive, security and network operations have a separate review.",18);
        tool_output=make(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,417,18);
        package_query=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,414,19);
        SendMessageW(package_query,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search term, or exact package IDs separated by commas"));
        button(L"Search",WingetSearch,19);button(L"Available updates",WingetUpdates,19);
        button(L"Install IDs",WingetInstall,19);button(L"Upgrade IDs",WingetUpgrade,19);button(L"Uninstall IDs",WingetUninstall,19);
        button(L"Enable daily updater",WingetSchedule,19);button(L"Disable updater",WingetUnschedule,19);
        label(L"WinGet uses the Windows package manager. Installation/update actions accept the selected package/source agreements after your review. Scheduled updates cover only the listed IDs while logged on.",19);
        package_output=make(L"EDIT",L"Ready. Use Search to find exact package IDs.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,418,19);
        hub_search=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,430,20);SendMessageW(hub_search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search gamer tools"));
        hub_run=button(L"Show report",HubRun,20);hub_favorite=button(L"Favorite",HubFavorite,20);hub_cancel_button=button(L"Cancel",HubCancel,20);EnableWindow(hub_cancel_button,FALSE);
        lists[20]=list(20,{{L"Tool",225},{L"Category",95}});hub_description=label(L"Select a tool to see its scope and inputs.",20);
        hub_label1=label(L"Source / target",20);hub_first=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,431,20);hub_browse1=button(L"Browse",HubSource,20);
        hub_label2=label(L"Destination / value",20);hub_second=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,432,20);hub_browse2=button(L"Browse",HubTarget,20);
        hub_output=make(L"EDIT",L"Select a tool. Read-only reports require no approval; changes show their exact scope first.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_HSCROLL|ES_AUTOHSCROLL,433,20);
        button(L"Refresh / check conflicts",InspectRecovery,21);button(L"Restore selected change",RestoreSelected,21);
        lists[21]=list(21,{{L"Change type",150},{L"Target",400},{L"Original state",180},{L"Applied state",180}});recovery_note=make(L"EDIT",L"Select a change to see its original/applied states, purpose and restart guidance.",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,2104,21);button(L"Activity log",HistoryLog,21);
        gallery_search=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,440,22);SendMessageW(gallery_search,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Search capture names, tags or games"));
        button(L"Refresh gallery",GalleryRefresh,22);
        lists[22]=list(22,{});SetWindowLongPtrW(lists[22],GWL_STYLE,(GetWindowLongPtrW(lists[22],GWL_STYLE)&~LVS_TYPEMASK)|LVS_ICON|LVS_AUTOARRANGE);
        ListView_SetExtendedListViewStyle(lists[22],LVS_EX_DOUBLEBUFFER|LVS_EX_LABELTIP|LVS_EX_INFOTIP);gallery_icons=ImageList_Create(px(128),px(96),ILC_COLOR32|ILC_MASK,16,16);ListView_SetImageList(lists[22],gallery_icons,LVSIL_NORMAL);ListView_SetIconSpacing(lists[22],px(172),px(152));
        gallery_info=label(L"Choose a capture to view its details. Newest 200 PNG/MP4 files are shown. Files remain on this Windows account.",22);
        button(L"Open selected",GalleryOpen,22);button(L"Crop image",GalleryCrop,22);button(L"Trim video",GalleryTrim,22);
        label(L"Tags",22);gallery_tags=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,441,22);SendMessageW(gallery_tags,EM_SETLIMITTEXT,200,0);
        label(L"Associated game",22);gallery_game=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,442,22);
        button(L"Save tags / game",GallerySave,22);
        auto overlay_choice=[&](const wchar_t* title,int id,std::initializer_list<const wchar_t*> values){label(title,23);HWND combo=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,id,23);for(auto value:values)SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));return combo;};
        overlay_anchor=overlay_choice(L"Position",450,{L"Top left",L"Top right",L"Bottom left",L"Bottom right"});overlay_scale=overlay_choice(L"Size",451,{L"75%",L"100%",L"125%",L"150%",L"200%"});overlay_accent=overlay_choice(L"Accent color",452,{L"Neon green",L"Cyan",L"Purple",L"White"});overlay_layout=overlay_choice(L"Layout",453,{L"Stacked",L"Compact / two columns"});overlay_display=overlay_choice(L"Display",454,{L"Display containing NovaTune",L"Primary display"});overlay_opacity=overlay_choice(L"Opacity",455,{L"50%",L"75%",L"90%",L"100%"});
        label(L"Horizontal margin (pixels)",23);overlay_x=make(L"EDIT",L"24",WS_BORDER|WS_TABSTOP|ES_NUMBER,456,23);label(L"Vertical margin (pixels)",23);overlay_y=make(L"EDIT",L"42",WS_BORDER|WS_TABSTOP|ES_NUMBER,457,23);
        const wchar_t* metric_names[]={L"CPU usage",L"Memory usage",L"GPU usage",L"Video memory",L"Game FPS",L"Disk read / write"};for(int n=0;n<6;++n)overlay_metrics[n]=make(L"BUTTON",metric_names[n],WS_TABSTOP|BS_AUTOCHECKBOX,458+n,23);
        button(L"Save HUD layout",OverlaySave,23);button(L"Reset layout",OverlayDefaults,23);button(L"Show / hide HUD",ToggleOverlay,23);label(L"LIVE PREVIEW",23);overlay_preview=make(L"STATIC",L"",SS_OWNERDRAW,1400,23);label(L"Save applies your layout to the HUD. The HUD stays click-through. Position is clamped to the selected display. Live FPS requires a selected game and PresentMon; unsupported telemetry stays unavailable. The desktop HUD supports windowed / borderless games.",23);load_overlay_editor();
        initialize_experience();initialize_comparison();initialize_network();
        hub_category=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,434,20);
        initialize_advanced();desktop=std::make_unique<hr::DesktopStudio>(window,store);desktop_editor=desktop->create_editor();controls[31].push_back(desktop_editor);
        for(const auto& tool:tools)hub_tools.push_back({"system."+tool.id,tool.name,"Windows / "+tool.category,"Open the detailed Windows tool page. No operation runs until you use that page's action.\n"+tool.description,"","",false});
        SendMessageW(hub_category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"All categories"));SendMessageW(hub_category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Favorites"));std::set<std::string> categories;for(const auto& tool:hub_tools)categories.insert(hub_category_name(tool));for(const auto& category:categories)SendMessageW(hub_category,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(hr::wide(category).c_str()));SendMessageW(hub_category,CB_SETCURSEL,0,0);
        filter_hub();refresh_recovery();
        refresh_cache_roots();filter_tools();
        refresh_saved();refresh_startup();refresh_mappings();refresh_disks();refresh_stress_results();configure_input();
        register_keys(false);
        if(!RegisterHotKey(window,4,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'O'))say(L"Overlay shortcut Ctrl+Alt+O unavailable; use Games button.");
        taskbar_created=RegisterWindowMessageW(L"TaskbarCreated");
        add_tray();
        RegisterHotKey(window,5,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'P');RegisterHotKey(window,6,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,'R');if(!RegisterHotKey(window,7,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F9))say(L"Replay hotkey Ctrl+Alt+F9 is unavailable; use Instant replay.");
        SetTimer(window,1,150,nullptr);SetTimer(window,2,3000,nullptr);SetTimer(window,4,33,nullptr);
        apply_theme();monitor();show_page(store.get("onboarding.complete")=="1"?0:33);if(store.get("desktop.autostart")=="1")try{desktop->enable(desktop->configuration());}catch(const std::exception& e){say(hr::wide(e.what()));}
        if(!smoke_report.empty()){SetTimer(window,3,800,nullptr);say(L"Running VM smoke test...");}
    }
    #include "network_ui.inc"
#include "comparison_ui.inc"
#include "experience_ui.inc"
    void layout(){
        if(!nav||collecting_layout)return;
        struct Finish{App* app;~Finish(){app->finish_layout();}} finish{this};placements.clear();collecting_layout=true;
        RECT rect;GetClientRect(window,&rect);
        if(rect.right<=0 || rect.bottom<=0)return;
        int width=MulDiv(rect.right,96,dpi),height=MulDiv(rect.bottom,96,dpi);
        int left=244,content=std::max(600,width-left-24),bottom=height-60;
        position(title,24,22,206,42);position(brand_note,26,70,204,24);
        position(subtitle,left,20,content-250,42);position(page_description,left,72,content,26);position(mode_label,width-246,28,220,38);
        position(global_search,20,114,200,34);position(nav,20,160,200,height-196);position(sidebar_footer,28,height-34,200,24);position(status,left,height-35,content,26);
                                                                                   
        int boost_side=std::min(230,content/5+14),gauge_side=std::min(190,(content-boost_side-48)/4);
        int total=gauge_side*4+boost_side+48,start=left+(content-total)/2,center_y=130+boost_side/2;
        for(int column=0;column<5;++column){
            int index=column==2?4:column<2?column:column-1,side=index==4?boost_side:gauge_side;
            position(gauges[index],start,center_y-side/2,side,side);start+=side+12;
        }
        int mode_y=center_y+boost_side/2+28;
        for(int i=0;i<3;++i)position(dashboard_modes[i],left+(content-444)/2+i*152,mode_y,140,36);
        position(suggestion_note,left+12,mode_y+52,content-320,56);
        position(suggestion_scan,left+content-302,mode_y+54,138,36);position(suggestion_fix,left+content-152,mode_y+54,150,36);
        int chart_top=mode_y+118,chart_height=std::max(90,bottom-106-chart_top),chart_width=(content-32)/3;
        for(int i=0;i<3;++i)position(dashboard_history[i],left+i*(chart_width+16),chart_top,chart_width,chart_height);
        position(pc_label,left+12,bottom-94,content-24,90);
        for(auto gauge:gauges)InvalidateRect(gauge,nullptr,FALSE);
        position(metrics,left,114,content,30);position(gpu_label,left,154,content,44);
        const int process_actions[]={2,5,6,7,8};int process_width=(content-48)/5;
        for(int n=0;n<5;++n)position(controls[15][process_actions[n]],left+n*(process_width+12),214,process_width,34);
        position(controls[15][3],left,264,content,34);position(lists[15],left,310,content,std::max(120,bottom-310));
        position(controls[1][0],left,114,150,32);position(controls[1][1],left+160,114,95,32);position(controls[1][2],left+265,114,220,32);
        position(lists[1],left,164,content,bottom-280);
        position(controls[1][4],left,bottom-100,190,26);position(duration,left+200,bottom-104,70,30);position(controls[1][6],left+285,bottom-106,220,34);position(controls[1][7],left,bottom-48,content-240,46);position(controls[1][8],left+content-230,bottom-46,230,34);
        position(controls[2][0],left,126,content,44);
        for(int i=0;i<3;++i){position(controls[2][1+i*3],left,200+i*62,100,28);position(plan_combo[i],left+110,194+i*62,content-350,240);position(key_edit[i],left+content-226,194+i*62,220,30);}
        position(controls[2][10],left,394,content,30);position(controls[2][11],left,442,220,36);position(controls[2][12],left+236,442,240,36);position(controls[2][13],left,510,content,60);
        for(int i=0;i<3;++i)position(controls[3][i],left+i*180,114,168,34);
        position(lists[3],left,164,content,bottom-226);position(controls[3][4],left,bottom-45,content,44);
        position(benchmark_kind,left,114,300,260);
        position(controls[4][0],left,166,220,34);position(controls[4][1],left+234,166,230,34);
        position(lists[4],left,220,content,bottom-296);position(summary,left,bottom-62,content,60);
        position(stress_controls[0],left,180,220,28);position(stress_seconds,left+230,176,75,32);
        position(stress_cpu,left+340,176,90,32);position(stress_ram,left+440,176,90,32);position(stress_gpu,left+540,176,190,32);
        position(stress_controls[5],left,234,210,36);position(stress_controls[6],left+226,234,130,36);
        position(stress_status,left,296,content,90);position(stress_list,left,408,content,std::max(100,bottom-408));
        for(int i=0;i<3;++i)position(controls[5][i],left+i*240,114,228,34);
        position(controls[5][3],left,164,content,42);position(lists[5],left,214,content,140);position(driver_offers,left,370,content,std::max(70,bottom-430));position(controls[5][6],left,bottom-44,content,44);
        for(int i=0;i<5;++i)position(controls[6][i],left+i*140,114,130,34);
        position(lists[6],left,164,content,bottom-234);position(controls[6][6],left,bottom-52,content,50);
        position(controls[7][0],left,114,content,24);position(presentmon,left,148,content-120,32);position(controls[7][2],left+content-110,148,110,32);
        position(notifications,left,204,260,30);position(hidden,left+270,204,250,30);position(glass_toggle,left+530,204,content-530,30);
        for(int i=0;i<3;++i)position(controls[7][5+i],left+i*190,254,178,34);
        position(controls[8][0],left,114,190,34);position(controls[8][1],left+205,114,210,34);position(controls[8][4],left+430,114,230,34);
        position(temp_summary,left,166,content,60);position(lists[8],left,238,content,bottom-238);
        position(controls[9][0],left,120,content,36);position(controls[9][1],left,178,content,82);
        position(controls[9][2],left,280,220,38);position(controls[9][3],left+240,280,210,38);
        position(controls[9][4],left,350,content,260);
        for(int i=0;i<3;++i)position(controls[10][i],left+i*(content/4),114,content/4-10,36);
        position(controls[10][6],left+3*(content/4),114,content/4-10,36);
        position(lists[10],left,166,content,bottom-300);position(controls[10][4],left,bottom-115,content,68);
        position(controls[10][5],left,bottom-40,240,34);
        position(controls[11][0],left,120,content,32);position(controls[11][1],left,180,content,40);
        position(mouse_speed,left,240,content-80,50);position(mouse_accel,left,310,content,34);
        for(int i=0;i<4;++i)position(controls[11][4+i],left+(i%2)*270,380+(i/2)*54,250,38);
        position(controls[11][8],left,520,content,32);position(mouse_absolute,left,566,content,34);
        int editor=250,design=content-editor-24,design_h=std::min(320,std::max(220,bottom-420)),options=164+design_h+20,mapping_y=options+50;
        position(remap_device,left,114,200,260);position(listen_button,left+216,114,180,34);position(controls[12][0],left+414,118,content-414,36);
        position(device_view,left,164,design,design_h);
        position(remap_button,left+design+24,164,editor,280);position(target_device,left+design+24,210,editor,220);position(target_button,left+design+24,256,editor,280);
        position(controls[12][5],left+design+24,306,editor,36);position(controls[12][6],left+design+24,352,editor,36);
        position(remap_enabled,left,options,180,30);position(remap_game_only,left+190,options,300,30);position(stick_mouse,left+500,options,content-500,30);
        position(lists[12],left,mapping_y,design,std::max(80,bottom-mapping_y-100));position(controls[12][10],left+design+24,mapping_y,editor,36);position(controls[12][16],left+design+24,mapping_y+48,editor/2-6,36);position(controls[12][17],left+design+30+editor/2,mapping_y+48,editor/2-6,36);
        position(input_status,left,bottom-88,content,32);position(controls[12][13],left,bottom-50,content,50);
        for(int i=0;i<5;++i)position(controls[13][i],left+i*(content/5),114,content/5-10,34);
        int columns=std::max(1,std::min(4,content/210));
        for(size_t i=0;i<disk_cards.size();++i)position(disk_cards[i],left+static_cast<int>(i%columns)*(content/columns),184+static_cast<int>(i/columns)*220,190,210);
        position(controls[13][5],left,bottom-88,content,80);
        for(int i=0;i<4;++i)position(controls[14][i],left+i*(content/4),114,content/4-10,36);
        position(capture_status,left,184,content,66);position(controls[14][5],left,290,content,240);
        position(app_search,left,114,content-550,34);
        for(int i=1;i<=3;++i)position(controls[16][i],left+content-535+(i-1)*180,114,170,34);
        position(lists[16],left,176,content,bottom-270);position(controls[16][5],left,bottom-78,content,70);
        position(storage_mode,left,114,230,260);position(storage_path,left+244,114,content-430,34);position(controls[17][2],left+content-174,114,174,34);
        for(int i=3;i<=7;++i)position(controls[17][i],left+(i-3)*(content/5),170,content/5-10,36);
        position(storage_summary,left,224,content,66);position(lists[17],left,310,content,std::max(100,bottom-310));
        position(tool_category,left,114,220,300);
        for(int i=1;i<=4;++i)position(controls[18][i],left+236+(i-1)*((content-236)/4),114,(content-236)/4-10,36);
        int tools_space=bottom-174,tools_height=(tools_space-104)*2/3;
        position(lists[18],left,174,content,tools_height);position(tool_description,left,190+tools_height,content,72);position(tool_output,left,278+tools_height,content,std::max(80,bottom-278-tools_height));
        int hub_list=std::clamp(content*37/100,310,420),hub_left=left+hub_list+20,hub_width=content-hub_list-20;
        position(hub_search,left,114,hub_list,34);position(hub_category,left,160,hub_list,280);position(lists[20],left,206,hub_list,bottom-206);
        int category_width=100,title_width=std::max(180,hub_list-category_width-22);ListView_SetColumnWidth(lists[20],0,px(title_width));ListView_SetColumnWidth(lists[20],1,px(category_width));
        int action_width=hub_width/2;position(hub_run,hub_left,114,action_width-8,34);position(hub_favorite,hub_left+action_width,114,(hub_width-action_width)/2-8,34);position(hub_cancel_button,hub_left+action_width+(hub_width-action_width)/2,114,(hub_width-action_width)/2-8,34);
        position(hub_description,hub_left,164,hub_width,90);int field_y=264;
        auto field=[&](bool present,bool browse,HWND label,HWND edit,HWND button){
            ShowWindow(label,page==20&&present?SW_SHOW:SW_HIDE);ShowWindow(edit,page==20&&present?SW_SHOW:SW_HIDE);ShowWindow(button,page==20&&present&&browse?SW_SHOW:SW_HIDE);
            if(!present)return;position(label,hub_left,field_y,hub_width,26);position(edit,hub_left,field_y+28,hub_width-(browse?92:0),34);if(browse)position(button,hub_left+hub_width-84,field_y+28,84,34);field_y+=80;
        };
        field(hub_has_first,hub_path_first,hub_label1,hub_first,hub_browse1);field(hub_has_second,hub_path_second,hub_label2,hub_second,hub_browse2);
        position(hub_output,hub_left,field_y+10,hub_width,std::max(100,bottom-field_y-10));
        position(controls[21][0],left,114,220,34);position(controls[21][1],left+236,114,220,34);position(GetDlgItem(window,HistoryLog),left+472,114,180,34);position(lists[21],left,174,content,std::max(150,bottom-374));position(recovery_note,left,bottom-186,content,186);
        int editor_width=std::min(420,content/2-12),editor_step=editor_width/2;for(int n=0;n<8;++n){int row=n/2,column=n%2;position(controls[23][n*2],left+column*editor_step,114+row*86,editor_step-12,24);position(controls[23][n*2+1],left+column*editor_step,142+row*86,editor_step-12,n<6?260:34);}
        for(int n=0;n<6;++n)position(overlay_metrics[n],left+(n%2)*editor_step,466+(n/2)*32,editor_step-12,28);position(controls[23][22],left,574,editor_width/2-8,34);position(controls[23][23],left+editor_width/2,574,editor_width/2-8,34);
        int preview_left=left+editor_width+24,preview_width=content-editor_width-24;position(controls[23][24],preview_left,114,preview_width,34);position(controls[23][25],preview_left,176,preview_width,24);position(overlay_preview,preview_left,214,preview_width,260);position(controls[23][27],preview_left,496,preview_width,std::max(100,bottom-496));
        position(gallery_search,left,114,content-166,34);position(controls[22][1],left+content-154,114,154,34);
        int gallery_right=left+content-272;position(lists[22],left,164,content-292,bottom-164);position(gallery_info,gallery_right,164,272,128);
        position(controls[22][4],gallery_right,304,272,34);position(controls[22][5],gallery_right,348,130,34);position(controls[22][6],gallery_right+142,348,130,34);
        position(controls[22][7],gallery_right,404,272,24);position(gallery_tags,gallery_right,432,272,34);position(controls[22][9],gallery_right,486,272,24);position(gallery_game,gallery_right,514,272,260);position(controls[22][11],gallery_right,568,272,34);
        layout_advanced(left,content,bottom);if(desktop_editor)position(desktop_editor,left,114,content,bottom-114);
        layout_experience(left,content,bottom);layout_comparison(left,content,bottom);layout_network(left,content,bottom);
        position(package_query,left,114,content-340,34);position(controls[19][1],left+content-324,114,130,34);position(controls[19][2],left+content-180,114,180,34);
        for(int i=3;i<=7;++i)position(controls[19][i],left+(i-3)*(content/5),170,content/5-10,36);
        position(controls[19][8],left,228,content,65);position(package_output,left,316,content,std::max(120,bottom-316));
    }
    void show_page(int p){
        if(p<0||p>=tab_count)return;
        page=p;SetWindowTextW(subtitle,tabs[p]);
        const wchar_t* descriptions[]={
            L"Live resources. Focused controls. Room for your next game.",
            L"Your games, launch profiles and live performance overlay.",
            L"Power profiles and shortcuts, tailored to how you work.",
            L"Choose what starts with Windows, with backups for every change.",
            L"Stress CPU and memory, or compare measured game frame times.",
            L"Display hardware, installed drivers and available update offers.",
            L"Inspect services and manage eligible startup settings.",
            L"Make NovaTune your own. Review preferences and change history.",
            L"Review temporary files before reclaiming disk space.",
            L"A focused gaming profile with reversible Windows settings.",
            L"Less optional clutter. More control over what stays installed.",
            L"Tune pointer speed and acceleration for your preferred feel.",
            L"Remap keyboard, mouse and controller buttons across Windows.",
            L"Storage health starts with space and the right Windows tools.",
            L"Capture a moment or record your desktop to a local file.",
            L"See active apps, close what you no longer need and review working-set use.",
            L"Review installed software and launch its registered removal process.",
            L"Review deep caches, folder sizes, large files and verified personal-file duplicates.",
            L"Native Windows maintenance, privacy, presentation and recovery controls.",
            L"Search, install and update software through Windows Package Manager.",
            L"Gaming diagnostics, versioned backups, discovery and hardware evidence.",
            L"Review individual changes and restore original values with conflict checks.",
            L"Your captures, with thumbnails, tags, game associations and original-preserving edits.",
            L"Customize HUD placement, size, colors, opacity and visible metrics.",
            L"Explicit, bounded desktop replay recording with a save hotkey.",
            L"Approve temporary game settings, save backups and automatic recovery.",
            L"Record locally, inspect real microphone levels and hear the result.",
            L"Named display and audio profiles with timed rollback and recovery.",
            L"Approved idle maintenance that defers games and fullscreen applications.",
            L"Versioned mod sets with exact reviewed file changes and clean baselines.",
            L"Measured frame spikes alongside sampled resources and background activity.",
            L"Create your own desktop with scenes, dock, widgets, animated wallpaper and window layouts.",
            L"Measure download, upload and response time; diagnose and review connection repairs.",
            L"Measure your baseline, review changes, personalize your desktop and share the result.",
            L"Your game, approved profile, launch tools and recorded performance in one place."
        };
        SetWindowTextW(page_description,descriptions[p]);
        for(int t=0;t<tab_count;++t)for(auto h:controls[t])ShowWindow(h,t==p?SW_SHOW:SW_HIDE);
        SendMessageW(nav,LB_SETCURSEL,nav_for_page(p),0);InvalidateRect(nav,nullptr,FALSE);
        layout();ensure_navigation_visible();if(p==34)refresh_center();if(p==33)refresh_comparison();if(p==11)load_mouse_editor();if(p==4)show_benchmark();if(p==15)refresh_processes();if(p==21)refresh_recovery();if(p==22){if(gallery_files.empty())scan_gallery();else filter_gallery();}if(p==23)load_overlay_editor();
        if(p>=24)show_advanced(p);
        if(p==6 && service_rows.empty())scan_services();
        if(p==5 && driver_rows.installed.empty())scan_drivers(false);
        if(p==10 && optional.empty() && !app_busy)scan_apps();
        if(p==16 && installed.empty() && !installed_busy)scan_installed();if(p==19)refresh_package_availability();
    }
    static std::string gallery_key(const std::filesystem::path& path){return "capture.meta."+hr::hex(hr::lower(hr::utf8(path.filename().wstring())));}
    hr::Row gallery_metadata(const std::filesystem::path& path){
        auto data=store.get(gallery_key(path));if(!data.empty())try{auto rows=hr::decode(data);if(rows.size()==1&&rows[0].size()==2)return rows[0];}catch(const std::exception&){}return {"",""};
    }
    std::filesystem::path selected_capture(){int row=selection(lists[22]);if(row<0||static_cast<size_t>(row)>=gallery_visible.size()||gallery_visible[row]>=gallery_files.size())return {};return gallery_files.at(gallery_visible[row]).path;}
    void check_capture(const std::filesystem::path& path){
        auto attr=GetFileAttributesW(path.c_str());auto ext=hr::lower(hr::utf8(path.extension().wstring()));if(path.empty()||path.parent_path()!=hr::capture_folder()||attr==INVALID_FILE_ATTRIBUTES||(attr&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||(ext!=".png"&&ext!=".mp4"))throw std::runtime_error("Select an existing local PNG or MP4 capture");
        if(recorder.active()&&path==recorder.file())throw std::runtime_error("Stop recording before opening or editing this capture");
    }
    void scan_gallery(bool preserve_selection=true){
        if(gallery_busy)return;auto selected=preserve_selection?selected_capture():std::filesystem::path{};if(!selected.empty())gallery_keep=selected.wstring();gallery_busy=true;EnableWindow(controls[22][1],FALSE);SetWindowTextW(gallery_info,L"Loading local capture thumbnails...");async("gallery",[](Result& r){r.media=hr::media_entries();});
    }
    void filter_gallery(bool preserve_selection=true){
        auto selected=preserve_selection?selected_capture():std::filesystem::path{};if(!selected.empty())gallery_keep=selected.wstring();gallery_filling=true;SendMessageW(lists[22],WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(lists[22]);ImageList_RemoveAll(gallery_icons);gallery_visible.clear();auto query=hr::lower(hr::utf8(text(gallery_search)));
        for(size_t i=0;i<gallery_files.size();++i){const auto& file=gallery_files[i];if(recorder.active()&&file.path==recorder.file())continue;auto meta=gallery_metadata(file.path);std::string title=hr::utf8(file.path.filename().wstring()),game=meta[1];for(const auto& row:games)if(hr::lower(row[2])==hr::lower(game)){game=row[1];break;}if(!query.empty()&&hr::lower(title+" "+meta[0]+" "+game).find(query)==std::string::npos)continue;
            int image=-1;if(file.thumbnail){HBITMAP bitmap=static_cast<HBITMAP>(file.thumbnail.get());HDC dc=GetDC(window),source=CreateCompatibleDC(dc),target=CreateCompatibleDC(dc);HBITMAP scaled=CreateCompatibleBitmap(dc,px(128),px(96));auto old_source=SelectObject(source,bitmap),old_target=SelectObject(target,scaled);RECT area{0,0,px(128),px(96)};FillRect(target,&area,background);BITMAP size{};GetObjectW(bitmap,sizeof size,&size);if(size.bmWidth>0&&size.bmHeight>0){double scale=std::min(static_cast<double>(px(128))/size.bmWidth,static_cast<double>(px(96))/size.bmHeight);int w=static_cast<int>(size.bmWidth*scale),h=static_cast<int>(size.bmHeight*scale);SetStretchBltMode(target,HALFTONE);StretchBlt(target,(px(128)-w)/2,(px(96)-h)/2,w,h,source,0,0,size.bmWidth,size.bmHeight,SRCCOPY);}SelectObject(source,old_source);SelectObject(target,old_target);image=ImageList_Add(gallery_icons,scaled,nullptr);DeleteObject(scaled);DeleteDC(source);DeleteDC(target);ReleaseDC(window,dc);}
            if(image<0){auto icon=native_icon();image=ImageList_AddIcon(gallery_icons,icon);DestroyIcon(icon);}auto label=file.path.filename().wstring();LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(gallery_visible.size());item.pszText=label.data();item.iImage=image;ListView_InsertItem(lists[22],&item);gallery_visible.push_back(i);if(file.path.wstring()==gallery_keep)ListView_SetItemState(lists[22],item.iItem,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        }
        gallery_filling=false;SendMessageW(lists[22],WM_SETREDRAW,TRUE,0);InvalidateRect(lists[22],nullptr,TRUE);describe_capture();
    }
    void describe_capture(){
        if(gallery_filling)return;auto path=selected_capture();bool selected=!path.empty(),png=hr::lower(hr::utf8(path.extension().wstring()))==".png";EnableWindow(controls[22][4],selected);EnableWindow(controls[22][5],selected&&png&&!gallery_busy);EnableWindow(controls[22][6],selected&&!png);EnableWindow(controls[22][11],selected);EnableWindow(gallery_tags,selected);EnableWindow(gallery_game,selected);
        auto meta=selected?gallery_metadata(path):hr::Row{"",""};SetWindowTextW(gallery_tags,hr::wide(meta[0]).c_str());gallery_games={""};SendMessageW(gallery_game,CB_RESETCONTENT,0,0);SendMessageW(gallery_game,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"No game association"));int chosen=0;for(const auto& game:games){SendMessageW(gallery_game,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(hr::wide(game[1]).c_str()));gallery_games.push_back(game[2]);if(hr::lower(game[2])==hr::lower(meta[1]))chosen=static_cast<int>(gallery_games.size()-1);}if(!meta[1].empty()&&!chosen){gallery_games.push_back(meta[1]);auto missing=L"Unavailable: "+std::filesystem::path(hr::wide(meta[1])).filename().wstring();SendMessageW(gallery_game,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(missing.c_str()));chosen=static_cast<int>(gallery_games.size()-1);}SendMessageW(gallery_game,CB_SETCURSEL,chosen,0);
        std::wstring info=std::to_wstring(gallery_visible.size())+L" shown / "+std::to_wstring(gallery_files.size())+L" loaded (newest 200).\n";if(selected){int row=selection(lists[22]);info+=path.filename().wstring()+L"\n"+number(gallery_files[gallery_visible[row]].bytes/1048576.0,2)+L" MB / "+(png?L"PNG image":L"MP4 video")+L"\nEdits create a new file. Original preserved.";}else info+=gallery_busy?L"Loading thumbnails...":L"Select a capture. Search matches filenames, tags and associated games.";SetWindowTextW(gallery_info,info.c_str());
    }
    void refresh_saved(){
        auto game=selected_game();
        games=store.of("game");benchmarks=store.of("benchmark");disabled=store.of("disabled");
        std::reverse(benchmarks.begin(),benchmarks.end());
        ListView_DeleteAllItems(lists[1]);ImageList_RemoveAll(game_icons);
        for(size_t i=0;i<games.size();++i){
            HICON icon=nullptr;auto path=hr::wide(games[i][2]);SHDefExtractIconW(path.c_str(),0,0,&icon,nullptr,MAKELONG(px(64),px(32)));
            if(!icon)icon=native_icon();int image=ImageList_AddIcon(game_icons,icon);DestroyIcon(icon);
            auto title=hr::wide(games[i][1]);LVITEMW item{};item.mask=LVIF_TEXT|LVIF_IMAGE;item.iItem=static_cast<int>(i);item.pszText=title.data();item.iImage=image;ListView_InsertItem(lists[1],&item);
            if(games[i][2]==game)ListView_SetItemState(lists[1],static_cast<int>(i),LVIS_SELECTED,LVIS_SELECTED);
        }
        ListView_DeleteAllItems(lists[4]);
        for(const auto& r:benchmarks)add(lists[4],{hr::wide(r[1]),hr::wide(std::filesystem::path(hr::wide(r[2])).filename().wstring().empty()?r[2]:hr::utf8(std::filesystem::path(hr::wide(r[2])).filename().wstring())),hr::wide(r[3]),hr::wide(r[6]),hr::wide(r[7]),hr::wide(r[8]),hr::wide(r[9]),hr::wide(r[4])});
        ListView_DeleteAllItems(lists[7]);
        auto history=store.of("history");std::reverse(history.begin(),history.end());
        for(const auto& r:history)add(lists[7],{hr::wide(r[1]),hr::wide(r[2]),hr::wide(r[3])});
        SetWindowTextW(mode_label,hr::wide(store.get("mode","Browsing")+" mode").c_str());InvalidateRect(mode_label,nullptr,FALSE);
        update_tray();
        refresh_advanced_library();refresh_center_library();
    }
    void refresh_startup(){
        if(startup_busy)return;startup_busy=true;
        async("autoruns",[](Result& r){r.autoruns=hr::autoruns();});
    }
    void monitor(){
        if(monitoring)return;monitoring=true;
        async("sample",[](Result& result){result.sample=hr::sample();});
    }
    void scan_services(){
        if(scanning_services)return;scanning_services=true;say(L"Reading service inventory...");
        async("services",[](Result& r){r.services=hr::services();});
    }
    void scan_drivers(bool updates){
        if(scanning_drivers)return;scanning_drivers=true;
        say(updates?L"Checking driver inventory and Windows Update offers...":L"Reading driver inventory...");
        async("drivers",[updates](Result& r){r.drivers=hr::drivers(updates);r.text=updates?"updates":"inventory";});
    }
    void show_benchmark(){
        bool fps=SendMessageW(benchmark_kind,CB_GETCURSEL,0,0)==1;
        for(auto h:fps_controls)ShowWindow(h,page==4&&fps?SW_SHOW:SW_HIDE);
        for(auto h:stress_controls)ShowWindow(h,page==4&&!fps?SW_SHOW:SW_HIDE);
    }
    void refresh_stress_results(){
        if(!stress_list)return;ListView_DeleteAllItems(stress_list);
        auto results=store.of("stress");std::reverse(results.begin(),results.end());
        for(const auto& r:results)add(stress_list,{hr::wide(r[1]),hr::wide(r[2]),hr::wide(r[4]),number(std::stod(r[5])/1073741824.0,2),hr::wide(r[6]),hr::wide(r[7])});
    }
    void update_stress(){
        auto state=stress.snapshot();
        if(stress_status && (state.running || !stress_saved)){
            std::wstring status=(state.running?L"Running  ":L"Finished  ")+std::to_wstring(state.elapsed)+L" / "+std::to_wstring(state.duration)+L" s  |  "+std::to_wstring(state.workers)+L" CPU workers\n";
            status+=L"CPU rounds "+std::to_wstring(state.cpu_rounds)+L"   RAM verified "+number(state.ram_bytes/1073741824.0,2)+L" GB   GPU dispatches "+std::to_wstring(state.gpu_dispatches)+L"   Errors "+std::to_wstring(state.errors);
            if(!state.gpu_status.empty())status+=L"\n"+hr::wide(state.gpu_status);
            if(text(stress_status)!=status)SetWindowTextW(stress_status,status.c_str());
        }
        if(!state.running && !stress_saved){
            stress_saved=true;
            store.rows.push_back({"stress",hr::now(),std::to_string(state.elapsed),std::to_string(state.workers),std::to_string(state.cpu_rounds),std::to_string(state.ram_bytes),std::to_string(state.gpu_dispatches),std::to_string(state.errors),state.gpu_status,store.get("mode","Browsing")});
            store.log("Stress test",std::to_string(state.elapsed)+" seconds; "+std::to_string(state.errors)+" verification errors");
            refresh_stress_results();
        }
    }
    void select_source(const std::string& device,unsigned code){
        auto index=SendMessageW(remap_device,CB_FINDSTRINGEXACT,static_cast<WPARAM>(-1),reinterpret_cast<LPARAM>(hr::wide(device).c_str()));
        if(index!=CB_ERR)SendMessageW(remap_device,CB_SETCURSEL,index,0);
        populate_buttons(true,false);
        auto found=std::find_if(source_buttons.begin(),source_buttons.end(),[&](const auto& item){return item.second==code;});
        if(found==source_buttons.end()){
            source_buttons.push_back({"Key "+std::to_string(code),code});SendMessageW(remap_button,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(hr::wide(source_buttons.back().first).c_str()));found=source_buttons.end()-1;
        }
        SendMessageW(remap_button,CB_SETCURSEL,found-source_buttons.begin(),0);selected_source=code;source_flash=GetTickCount64()+500;
        InvalidateRect(device_view,nullptr,FALSE);say(hr::wide("Captured "+device+" / "+found->first+". Choose an output, then add the mapping."));
    }
    void select_design_button(){
        POINT point;GetCursorPos(&point);ScreenToClient(device_view,&point);
        for(const auto& region:device_regions)if(PtInRect(&region.rect,point)){select_source(hr::utf8(text(remap_device)),region.code);return;}
    }
    void draw_device(const DRAWITEMSTRUCT& item){
        auto dc=item.hDC;RECT bounds=item.rcItem;FillRect(dc,&bounds,background);device_regions.clear();
        auto device=hr::utf8(text(remap_device));auto down=input.visual_buttons(device);
        SetBkMode(dc,TRANSPARENT);SelectObject(dc,font);
        auto key=[&](int x,int y,int width,int height,unsigned code,const std::wstring& label,bool circle=false){
            RECT rect{x,y,x+width,y+height};device_regions.push_back({rect,code});
            bool pressed=down.count(code)>0 || (GetTickCount64()<source_flash && code==selected_source);
            bool selected=pressed;
            HBRUSH brush=CreateSolidBrush(pressed?neon:light_theme?RGB(229,233,238):RGB(24,27,32));
            HPEN pen=CreatePen(PS_SOLID,selected?2:1,selected?neon:light_theme?RGB(178,186,198):RGB(65,70,80));
            auto old=SelectObject(dc,brush);auto oldpen=SelectObject(dc,pen);
            if(circle)Ellipse(dc,rect.left,rect.top,rect.right,rect.bottom);else RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,10,10);
            SelectObject(dc,old);SelectObject(dc,oldpen);DeleteObject(brush);DeleteObject(pen);
            SetTextColor(dc,pressed?RGB(4,24,25):ink);DrawTextW(dc,label.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
        };
        int width=bounds.right,height=bounds.bottom;
        if(device=="Keyboard"){
            int gap=4,w=std::max(23,(width-18)/15-4),h=std::max(18,(height-26)/6-3),x=6;
            key(x,5,w,h,VK_ESCAPE,L"Esc");x+=w+gap;
            for(unsigned i=1;i<=12;++i){key(x,5,w,h,VK_F1+i-1,L"F"+std::to_wstring(i));x+=w+gap;}
            const std::string rows[]={"1234567890","QWERTYUIOP","ASDFGHJKL","ZXCVBNM"};
            for(int row=0;row<4;++row){
                int left=6+(row==2?w/2:row==3?w:0),y=5+(row+1)*(h+gap);
                for(char c:rows[row]){key(left,y,w,h,static_cast<unsigned>(c),std::wstring(1,static_cast<wchar_t>(c)));left+=w+gap;}
            }
            int extra=6+11*(w+gap),y=h+gap+5;
            key(extra,y,w*2,h,VK_BACK,L"Back");key(extra,y+h+gap,w*2,h,VK_RETURN,L"Enter");
            key(extra,y+2*(h+gap),w,h,VK_UP,L"\u2191");key(extra-w-gap,y+3*(h+gap),w,h,VK_LEFT,L"\u2190");
            key(extra,y+3*(h+gap),w,h,VK_DOWN,L"\u2193");key(extra+w+gap,y+3*(h+gap),w,h,VK_RIGHT,L"\u2192");
                                                                         
            int base=height-h-3;
            key(6,base,w,h,VK_CONTROL,L"Ctrl");key(w+10,base,w,h,VK_SHIFT,L"Shift");key(2*w+14,base,w,h,VK_MENU,L"Alt");
            key(3*w+18,base,w*5,h,VK_SPACE,L"Space");
        }else if(device=="Mouse"){
            int mw=150,mh=std::min(210,height-12),x=(width-mw)/2,y=4;
            HBRUSH brush=CreateSolidBrush(light_theme?RGB(225,230,236):RGB(18,21,26));HPEN pen=CreatePen(PS_SOLID,2,light_theme?RGB(170,182,192):RGB(62,70,80));
            auto b=SelectObject(dc,brush),p=SelectObject(dc,pen);RoundRect(dc,x,y,x+mw,y+mh,75,75);SelectObject(dc,b);SelectObject(dc,p);DeleteObject(brush);DeleteObject(pen);
            key(x+9,y+12,60,65,1,L"LMB");key(x+81,y+12,60,65,2,L"RMB");key(x+59,y+32,32,44,3,L"\u2022");
            key(x-74,y+83,64,34,4,L"X1");key(x-74,y+127,64,34,5,L"X2");key(x+mw+14,y+80,82,32,6,L"Wheel up");key(x+mw+14,y+116,82,32,7,L"Wheel down");key(x+mw+14,y+152,82,32,8,L"Wheel left");key(x+mw+14,y+188,82,32,9,L"Wheel right");
        }else{if(GetTickCount64()<source_flash)down.insert(selected_source);auto regions=hr::draw_controller_art(dc,bounds,device=="PlayStation",down,neon);for(auto& region:regions)device_regions.push_back({region.rect,region.code});
        }
    }
    void draw_disk(const DRAWITEMSTRUCT& item){
        size_t index=item.CtlID-1100;if(index>=disk_rows.size())return;
        auto dc=item.hDC;RECT bounds=item.rcItem;FillRect(dc,&bounds,background);
        const auto& drive=disk_rows[index];int cx=bounds.right/2,cy=86,r=70;
        auto circle=[&](COLORREF color,int thickness){HPEN pen=CreatePen(PS_SOLID,thickness,color);auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));Ellipse(dc,cx-r,cy-r,cx+r,cy+r);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);};
        circle(light_theme?RGB(217,224,231):RGB(39,43,50),8);
        double used=drive.total?1.0-static_cast<double>(drive.free)/drive.total:0;
        HPEN pen=CreatePen(PS_SOLID,8,index==static_cast<size_t>(selected_disk)?neon:RGB(116,143,154));auto old=SelectObject(dc,pen);MoveToEx(dc,cx,cy-r,nullptr);AngleArc(dc,cx,cy,r,90,static_cast<FLOAT>(-used*360));SelectObject(dc,old);DeleteObject(pen);
        SelectObject(dc,heading_font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);RECT label{0,cy-30,bounds.right,cy+4};
        auto name=drive.root.substr(0,2);DrawTextW(dc,name.c_str(),-1,&label,DT_CENTER|DT_SINGLELINE);
        SelectObject(dc,font);label.top=cy+13;label.bottom=cy+40;auto free=number(drive.free/1073741824.0)+L" GB free";DrawTextW(dc,free.c_str(),-1,&label,DT_CENTER|DT_SINGLELINE);
        label.top=168;label.bottom=196;auto total=number(drive.total/1073741824.0)+L" GB  /  "+drive.filesystem;DrawTextW(dc,total.c_str(),-1,&label,DT_CENTER|DT_SINGLELINE);
    }

    void populate_buttons(bool source=true,bool target=true){
        if(source)source_buttons=hr::input_buttons(hr::utf8(text(remap_device)));if(target)target_buttons=hr::input_buttons(hr::utf8(text(target_device)));
        for(auto pair:{std::make_pair(remap_button,&source_buttons),std::make_pair(target_button,&target_buttons)}){if((pair.first==remap_button&&!source)||(pair.first==target_button&&!target))continue;
            SendMessageW(pair.first,CB_RESETCONTENT,0,0);
            for(const auto& button:*pair.second)SendMessageW(pair.first,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(hr::wide(button.first).c_str()));
            SendMessageW(pair.first,CB_SETCURSEL,0,0);
        }
    }
    void refresh_mappings(){
        if(!lists[12])return;ListView_DeleteAllItems(lists[12]);
        for(const auto& row:store.of("mapping")){
            auto sources=hr::input_buttons(row[1]),targets=hr::input_buttons(row[3]);
            auto name=[](const auto& buttons,unsigned code){for(const auto& b:buttons)if(b.second==code)return b.first;return std::to_string(code);};
            add(lists[12],{hr::wide(row[1]),hr::wide(name(sources,static_cast<unsigned>(std::stoul(row[2])))),hr::wide(row[3]+": "+name(targets,static_cast<unsigned>(std::stoul(row[4]))))});
        }
    }
    void update_mouse_readback(){auto current=hr::mouse_settings();auto mode=input.pointer_mode();auto message=L"WINDOWS / speed "+std::to_wstring(current.speed)+L" of 20 / precision "+(current.acceleration?L"on":L"off")+L" / input: "+hr::wide(mode);if(text(mouse_readback)!=message)SetWindowTextW(mouse_readback,message.c_str());auto note=std::wstring();if(text(controls[11][8])!=note)SetWindowTextW(controls[11][8],note.c_str());}
    void load_mouse_editor(){auto current=hr::mouse_settings();input.set_pointer_scaling(store.get("mouse.absolute.scale","1")=="1",current.speed);SendMessageW(mouse_speed,TBM_SETPOS,TRUE,current.speed);SendMessageW(mouse_accel,BM_SETCHECK,current.acceleration?BST_CHECKED:BST_UNCHECKED,0);update_mouse_readback();}
    void configure_input(){
        std::vector<hr::Mapping> mappings;
        for(const auto& r:store.of("mapping"))mappings.push_back({r[1],static_cast<unsigned>(std::stoul(r[2])),r[3]=="Mouse",static_cast<unsigned>(std::stoul(r[4]))});
        input.configure(std::move(mappings),store.get("remap.enabled","0")=="1",false,"",store.get("remap.stick","0")=="1");
        input.set_deadzone(std::stod(store.get("deadzone.global","0.27")));input.set_pointer_scaling(store.get("mouse.absolute.scale","1")=="1",hr::mouse_settings().speed);
    }
    void refresh_disks(){
        for(auto card:disk_cards){controls[13].erase(std::remove(controls[13].begin(),controls[13].end(),card),controls[13].end());DestroyWindow(card);}
        disk_cards.clear();disk_rows=hr::drives();selected_disk=0;
        for(size_t i=0;i<disk_rows.size() && i<24;++i)disk_cards.push_back(make(L"STATIC",L"",SS_OWNERDRAW|SS_NOTIFY,1100+static_cast<int>(i),13));
        layout();for(auto card:disk_cards)ShowWindow(card,page==13?SW_SHOW:SW_HIDE);
    }
    unsigned suggestion_count()const{return (suggestion_files.empty()?0u:1u)+(suggestion_capture?1u:0u);}
    void update_suggestion_badge(){
        auto title=L"Review fixes ("+std::to_wstring(suggestion_count())+L")";SetWindowTextW(suggestion_fix,title.c_str());
        EnableWindow(suggestion_fix,suggestions_scanned&&!suggestions_busy);InvalidateRect(suggestion_fix,nullptr,FALSE);
    }
    void apply_suggestion(HWND review,const std::string& id){
        fix_review=review;
        if(suggestions_busy)throw std::runtime_error("Wait for the current fix to finish");
        if(id=="temp"){
            if(suggestion_files.empty())throw std::runtime_error("This cleanup is already complete; scan again for new findings");
            suggestions_busy=true;EnableWindow(suggestion_scan,FALSE);update_suggestion_badge();auto files=suggestion_files;
            async("suggestion-clean",[files](Result& r){r.cleanup=hr::clean_temp(files);});
        }else if(id=="capture"){
            if(!suggestion_capture)throw std::runtime_error("This recording setting is already fixed");
            hr::registry_dword(store,HKEY_CURRENT_USER,L"System\\GameConfigStore",L"GameDVR_Enabled",0,"Approved background capture fix");
            suggestion_capture=false;update_suggestion_badge();hr::update_fix_review(review,id,L"Fixed. Original setting saved in Undo.",true);refresh_saved();
        }
    }
    void draw_history(const DRAWITEMSTRUCT& item){
        auto dc=item.hDC;RECT r=item.rcItem;FillRect(dc,&r,background);
        int index=item.CtlID-1300,w=r.right,h=r.bottom;
        HBRUSH fill=CreateSolidBrush(panel);HPEN border=CreatePen(PS_SOLID,1,light_theme?RGB(208,208,215):RGB(48,48,54));
        auto brush=SelectObject(dc,fill),pen=SelectObject(dc,border);RoundRect(dc,0,0,w-1,h-1,16,16);SelectObject(dc,brush);SelectObject(dc,pen);DeleteObject(fill);DeleteObject(border);
        SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);
        const wchar_t* titles[]={L"CPU ACTIVITY",L"MEMORY PRESSURE",L"GPU ACTIVITY"};RECT label{16,12,w-12,36};DrawTextW(dc,titles[index],-1,&label,DT_SINGLELINE);
        const auto& values=index==0?cpu_trend:index==1?ram_trend:gpu_trend;
        double value=values.empty()?-1:values.back();std::wstring detail=value<0?L"Telemetry unavailable":number(value,0)+L"%   /   40 recent samples";
        label.top=38;label.bottom=60;SetTextColor(dc,light_theme?RGB(94,94,105):RGB(151,151,166));DrawTextW(dc,detail.c_str(),-1,&label,DT_SINGLELINE|DT_END_ELLIPSIS);
        int top=70,bottom=h-24;HPEN grid=CreatePen(PS_SOLID,1,light_theme?RGB(225,225,232):RGB(39,39,46));pen=SelectObject(dc,grid);
        for(int i=0;i<4;++i){int y=top+(bottom-top)*i/3;MoveToEx(dc,16,y,nullptr);LineTo(dc,w-16,y);}SelectObject(dc,pen);DeleteObject(grid);
        COLORREF color=index==1?RGB(180,95,255):index==0?neon:RGB(0,216,255);
        std::vector<POINT> points;auto flush=[&]{if(points.size()>1){HPEN line=CreatePen(PS_SOLID,2,color);auto old=SelectObject(dc,line);Polyline(dc,points.data(),static_cast<int>(points.size()));SelectObject(dc,old);DeleteObject(line);}points.clear();};
        for(size_t i=0;i<values.size();++i){if(values[i]<0){flush();continue;}points.push_back({16+static_cast<LONG>((w-32)*i/39),bottom-static_cast<LONG>((bottom-top)*std::clamp(values[i],0.0,100.0)/100)});}flush();
    }
    void draw_gauge(const DRAWITEMSTRUCT& item){
        int index=static_cast<int>(item.CtlID)-1000;auto dc=item.hDC;RECT rect=item.rcItem;
        FillRect(dc,&rect,background);int w=rect.right,h=rect.bottom,size=std::min(w,h)-16,cx=w/2,cy=h/2;
        auto ellipse=[&](int radius,COLORREF color,int thickness){
            HPEN pen=CreatePen(PS_SOLID,thickness,color);auto old=SelectObject(dc,pen);auto brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
            Ellipse(dc,cx-radius,cy-radius,cx+radius,cy+radius);SelectObject(dc,old);SelectObject(dc,brush);DeleteObject(pen);
        };
        const COLORREF colors[]={neon,neon,neon,RGB(0,216,255)};
        if(index==4){
            const COLORREF color=light_theme?RGB(133,55,210):RGB(180,95,255);
            ellipse(size/2+2,light_theme?RGB(220,204,236):RGB(48,27,65),10);ellipse(size/2,color,3);
                                                                             
                                                                         
            POINT orbit[32]{};int radius=size/2+3;
            double phase=animation*(GetTickCount64()<boost_flash?13.0:5.0);
            for(int i=0;i<32;++i){double angle=(phase+i*2.4)*3.14159265358979323846/180.0;orbit[i]={cx+static_cast<LONG>(std::lround(radius*std::cos(angle))),cy+static_cast<LONG>(std::lround(radius*std::sin(angle)))};}
            HPEN glow=CreatePen(PS_SOLID,8,light_theme?RGB(221,198,241):RGB(68,31,99));auto old=SelectObject(dc,glow);Polyline(dc,orbit,32);SelectObject(dc,old);DeleteObject(glow);
            HPEN edge=CreatePen(PS_SOLID,3,light_theme?RGB(164,80,230):RGB(211,151,255));old=SelectObject(dc,edge);Polyline(dc,orbit,32);SelectObject(dc,old);DeleteObject(edge);
            SetTextColor(dc,ink);SetBkMode(dc,TRANSPARENT);SelectObject(dc,heading_font);
            RECT textrect{0,cy-22,w,cy+16};DrawTextW(dc,boost_busy?boost_phase.c_str():GetTickCount64()<boost_flash?L"DONE":L"BOOST",-1,&textrect,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
            SelectObject(dc,font);SetTextColor(dc,color);textrect.top=cy+22;textrect.bottom=cy+48;auto mode=hr::wide(store.get("mode","Browsing")+" mode");DrawTextW(dc,mode.c_str(),-1,&textrect,DT_CENTER|DT_SINGLELINE);return;
        }
        double value=index==0?last.cpu:index==1?(last.total>0?last.used/last.total*100:0):index==2?last.gpu_util:last.vram_total>0?last.vram_used/last.vram_total*100:-1;
        ellipse(size/2,light_theme?RGB(218,224,230):RGB(35,37,42),7);
        if(value>=0){HPEN pen=CreatePen(PS_SOLID,7,colors[index]);auto old=SelectObject(dc,pen);MoveToEx(dc,cx,cy-size/2,nullptr);AngleArc(dc,cx,cy,size/2,90,static_cast<FLOAT>(-std::clamp(value,0.0,100.0)*3.6));SelectObject(dc,old);DeleteObject(pen);}
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,ink);SelectObject(dc,heading_font);
        auto display=value<0?L"N/A":number(value,0)+L"%";RECT textrect{0,cy-28,w,cy+10};DrawTextW(dc,display.c_str(),-1,&textrect,DT_CENTER|DT_SINGLELINE|DT_VCENTER);
        SelectObject(dc,font);SetTextColor(dc,colors[index]);textrect.top=cy+16;textrect.bottom=cy+43;const wchar_t* names[]={L"CPU",L"RAM",L"GPU",L"VRAM"};DrawTextW(dc,names[index],-1,&textrect,DT_CENTER|DT_SINGLELINE);

    }
    void reset_columns(HWND list,const std::vector<std::pair<const wchar_t*,int>>& columns){
        SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
        while(Header_GetItemCount(ListView_GetHeader(list))>0)ListView_DeleteColumn(list,0);
        int index=0;for(const auto& item:columns){LVCOLUMNW column{};column.mask=LVCF_TEXT|LVCF_WIDTH;column.pszText=const_cast<wchar_t*>(item.first);column.cx=px(item.second);ListView_InsertColumn(list,index++,&column);}
        SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
    }
    void refresh_cache_roots(){
        if(!lists[17])return;cache_roots=hr::cache_targets();cache_preview=false;
        reset_columns(lists[17],{{L"Cleanup location",310},{L"Path",390},{L"Notes",450}});
        ListView_SetExtendedListViewStyle(lists[17],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_CHECKBOXES);
        for(const auto& target:cache_roots){add(lists[17],{hr::wide(target.label),target.path.wstring(),hr::wide(target.warning)});ListView_SetCheckState(lists[17],ListView_GetItemCount(lists[17])-1,target.id=="user-temp");}
        SetWindowTextW(storage_summary,L"Select locations, then Scan. Browsers must be closed for browser data. Cookies sign you out; password files are never included. Prefetch/shaders rebuild and may slow the next launch.");
    }
    void show_storage_results(){
        int mode=static_cast<int>(SendMessageW(storage_mode,CB_GETCURSEL,0,0));
        duplicate_rows.clear();
        if(mode==0){
            reset_columns(lists[17],{{L"Scanned cache file",600},{L"Size MB",100},{L"Category",230}});
            ListView_SetExtendedListViewStyle(lists[17],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_CHECKBOXES);
            if(cache_files.size()>10000)cache_files.resize(10000);
            for(const auto& file:cache_files){add(lists[17],{file.path.wstring(),number(file.bytes/1048576.0,2),hr::wide(file.category)});ListView_SetCheckState(lists[17],ListView_GetItemCount(lists[17])-1,TRUE);}
            cache_preview=true;SetWindowTextW(storage_summary,(std::to_wstring(cache_files.size())+L" eligible files previewed. Review checked files before permanent cache cleanup. Busy browsers/update queues, links and changed files are skipped.").c_str());
        }else if(mode==1){
            reset_columns(lists[17],{{L"Folder / top-level item",540},{L"Size GB",110},{L"Share / files",300}});
            ListView_SetExtendedListViewStyle(lists[17],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
            for(const auto& folder:storage_result.folders){
                double percent=storage_result.bytes?folder.bytes*100.0/storage_result.bytes:0;
                add(lists[17],{folder.path.wstring(),number(folder.bytes/1073741824.0,2),number(percent,1)+L"%  /  "+std::to_wstring(folder.files)+L" files"});
            }
            SetWindowTextW(storage_summary,(L"Total "+number(storage_result.bytes/1073741824.0,2)+L" GB in "+std::to_wstring(storage_result.files)+L" files. Folder sizes are logical bytes; inaccessible files and links are skipped. Largest files appear below.").c_str());
            for(const auto& file:storage_result.largest)add(lists[17],{L"LARGE FILE  "+file.path.wstring(),number(file.bytes/1073741824.0,2),L"Reveal to inspect; not auto-deleted"});
        }else{
            reset_columns(lists[17],{{L"Duplicate personal file",600},{L"Size MB",100},{L"Group / keeper",200}});
            ListView_SetExtendedListViewStyle(lists[17],LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER|LVS_EX_CHECKBOXES);
            for(size_t group=0;group<storage_result.duplicates.size();++group){
                const auto& duplicate=storage_result.duplicates[group];
                for(size_t n=0;n<duplicate.files.size();++n){
                    duplicate_rows.push_back({group,n});add(lists[17],{duplicate.files[n].path.wstring(),number(duplicate.files[n].bytes/1048576.0,2),std::to_wstring(group+1)+(n?L"  duplicate":L"  KEEP")});ListView_SetCheckState(lists[17],ListView_GetItemCount(lists[17])-1,FALSE);
                }
            }
            SetWindowTextW(storage_summary,(std::to_wstring(storage_result.duplicates.size())+L" SHA256-matching personal-file groups. First copy is kept. Nothing is selected automatically. Selected duplicates are revalidated and sent to Recycle Bin. Program/system files are excluded.").c_str());
        }
        if(storage_result.capped)say(L"Analysis stopped at 500,000 files; results are partial.");
    }
    static std::string hub_category_name(const hr::GamerTool& tool){return tool.id.rfind("system.",0)==0?"Windows tools":tool.category;}
    void remember_hub(){if(hub_selected.empty())return;hub_inputs[hub_selected]={text(hub_first),text(hub_second)};if(!hub_busy||hub_selected!=hub_pending_id)hub_reports[hub_selected]=text(hub_output);}
    void update_hub_actions(){int row=selection(lists[20]);bool selected=row>=0&&static_cast<size_t>(row)<visible_hub.size();EnableWindow(hub_run,selected&&!hub_busy);EnableWindow(hub_favorite,selected);EnableWindow(hub_cancel_button,hub_busy||!hub_live.empty());}
    void open_hub_tool(const std::string& id){SendMessageW(hub_category,CB_SETCURSEL,0,0);show_page(20);SetWindowTextW(hub_search,hr::wide("id:"+id).c_str());}
    void filter_hub(){
        if(!hub_category||!lists[20])return;remember_hub();auto previous=hub_selected;visible_hub.clear();SendMessageW(lists[20],WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(lists[20]);auto query=hr::lower(hr::utf8(text(hub_search))),category=hr::utf8(text(hub_category));
        std::vector<hr::GamerTool> sorted=hub_tools;std::stable_sort(sorted.begin(),sorted.end(),[&](const auto& a,const auto& b){return store.get("favorite."+a.id)=="1"&&store.get("favorite."+b.id)!="1";});
        for(const auto& tool:sorted){bool favorite=store.get("favorite."+tool.id)=="1",matches=query.empty()||(query.rfind("id:",0)==0?tool.id==query.substr(3):hr::lower(tool.title+" "+tool.category+" "+tool.description).find(query)!=std::string::npos);if(matches&&(category=="All categories"||category.empty()||(category=="Favorites"?favorite:hub_category_name(tool)==category))){visible_hub.push_back(tool);add(lists[20],{hr::wide((favorite?"* ":"")+tool.title),hr::wide(hub_category_name(tool))});}}
        SendMessageW(lists[20],WM_SETREDRAW,TRUE,0);InvalidateRect(lists[20],nullptr,FALSE);
        if(!visible_hub.empty()){size_t selected=0;bool found=false;for(size_t n=0;n<visible_hub.size();++n)if(visible_hub[n].id==previous){selected=n;found=true;break;}if(!found&&query.empty())for(size_t n=0;n<visible_hub.size();++n)if(visible_hub[n].id=="readiness"){selected=n;break;}ListView_SetItemState(lists[20],static_cast<int>(selected),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);describe_hub();}
        else{hub_selected.clear();hub_live.clear();hub_has_first=hub_has_second=hub_path_first=hub_path_second=false;SetWindowTextW(hub_description,L"No matching tools. Clear the search or choose another category.");SetWindowTextW(hub_output,L"Select a category and tool to view its options and report.");SetWindowTextW(hub_first,L"");SetWindowTextW(hub_second,L"");SetWindowTextW(hub_run,L"Select a tool");layout();}update_hub_actions();
    }
    void describe_hub(){
        int i=selection(lists[20]);if(i<0||static_cast<size_t>(i)>=visible_hub.size()){update_hub_actions();return;}const auto& tool=visible_hub[i];SetWindowTextW(hub_description,hr::wide(tool.description).c_str());SetWindowTextW(hub_label1,hr::wide(tool.first).c_str());SetWindowTextW(hub_label2,hr::wide(tool.second).c_str());
        hub_has_first=!tool.first.empty();hub_has_second=!tool.second.empty();hub_path_first=tool.id=="trim-clip"||tool.id=="crop-image"||tool.id=="favorite-game"||tool.id=="game-profile"||tool.id=="profile-disable"||tool.id=="background-sleep"||tool.id=="backup"||tool.id=="restore"||tool.id=="discover"||tool.id=="diagnostic-export"||tool.id=="deadzone";hub_path_second=tool.id=="restore"||tool.id=="capture-privacy";SetWindowTextW(hub_browse2,tool.id=="capture-privacy"?L"Select region":L"Browse");
        if(hub_selected!=tool.id){remember_hub();hub_selected=tool.id;SetWindowTextW(hub_output,hub_reports.count(tool.id)?hub_reports[tool.id].c_str():L"Ready. Choose the action above to generate this tool's report.");hub_live.clear();if(!hub_busy)EnableWindow(hub_cancel_button,FALSE);SetWindowTextW(hub_first,L"");SetWindowTextW(hub_second,L"");
            if(tool.id=="capture-privacy"){SetWindowTextW(hub_first,store.get("capture.cursor","1")=="1"?L"on":L"off");SetWindowTextW(hub_second,hr::wide(store.get("capture.region","full")).c_str());}
            if(tool.id=="thermal"){SetWindowTextW(hub_first,store.get("alerts.thermal")=="1"?L"on":L"off");SetWindowTextW(hub_second,hr::wide(store.get("alerts.thermal.threshold","85")).c_str());}
            if(tool.id=="controller-alerts")SetWindowTextW(hub_first,store.get("alerts.controller")=="1"?L"on":L"off");auto draft=hub_inputs.find(tool.id);if(draft!=hub_inputs.end()){SetWindowTextW(hub_first,draft->second.first.c_str());SetWindowTextW(hub_second,draft->second.second.c_str());}}
        const std::map<std::string,std::wstring> labels={{"trim-clip",L"Export selected range"},{"capture-privacy",L"Save capture scope"},{"crop-image",L"Select crop"},{"capture-files",L"Show captures"},{"favorite-game",L"Save favorite"},{"benchmark-notes",L"Save scene notes"},{"performance-target",L"Save FPS goals"},{"stutters",L"Analyze frame spikes"},{"benchmark-report",L"Compare captures"},{"update-impact",L"Compare update context"},{"game-profile",L"Save game profile"},{"profile-disable",L"Disable profile"},{"background-sleep",L"Save sleep list"},{"crash-recovery",L"Review and recover"},{"network",L"Test connection"},{"sensitivity",L"Convert sensitivity"},{"backup",L"Create backup"},{"restore",L"Restore snapshot"},{"discover",L"Find installed games"},{"memory-launch",L"Open memory diagnostic"},{"diagnostic-export",L"Preview and export"},{"controller",L"View live inputs"},{"controller-history",L"Show history"},{"controller-alerts",L"Save alerts"},{"thermal",L"Save alerts"},{"deadzone",L"Save dead zone"},{"readiness",L"Scan readiness"}};
        auto label=labels.find(tool.id);SetWindowTextW(hub_run,tool.id.rfind("system.",0)==0?L"Open tool page":label==labels.end()?L"Show report":label->second.c_str());SetWindowTextW(hub_favorite,store.get("favorite."+tool.id)=="1"?L"Unfavorite":L"Favorite");update_hub_actions();layout();
    }
    void update_controller_log(){
        if(!controller_log_loaded){controller_log_loaded=true;auto path=hr::data_dir()/L"controller-history.dat";if(std::filesystem::exists(path)){try{std::ifstream file(path,std::ios::binary);auto rows=hr::decode(std::string(std::istreambuf_iterator<char>(file),{}));for(const auto& row:rows)if(row.size()==3)controller_archive.push_back(row);}catch(const std::exception&){say(L"Old controller history is unreadable; new events remain available this session.");}}}
        auto events=input.controller_history();if(events.empty())return;auto latest=hr::encode({events.back()});if(latest==controller_event)return;size_t begin=0;if(!controller_event.empty())for(size_t i=0;i<events.size();++i)if(hr::encode({events[i]})==controller_event)begin=i+1;
        controller_archive.insert(controller_archive.end(),events.begin()+begin,events.end());while(controller_archive.size()>1000)controller_archive.erase(controller_archive.begin());controller_event=latest;hr::atomic_write(hr::data_dir()/L"controller-history.dat",hr::encode(controller_archive));
    }
    std::wstring controller_report(bool history){
        std::wostringstream out;if(history){update_controller_log();out<<L"CONTROLLER CONNECTION HISTORY\nOnly connection events are saved; no button presses.\n\n";for(auto row=controller_archive.rbegin();row!=controller_archive.rend();++row)out<<hr::wide((*row)[0]+" / "+(*row)[1]+" / "+(*row)[2])<<L"\n";if(controller_archive.empty())out<<L"No exposed connection events yet.";return out.str();}
        out<<L"CONTROLLER CALIBRATION LAB\nLive driver input. Leave sticks untouched to inspect drift.\nNative gamepad movement requires the game's own dead-zone settings.\n\n";auto readings=input.controllers();
        if(readings.empty())out<<L"No supported controller input detected. Connect an Xbox controller or press a PlayStation controller button.\n";
        for(const auto& reading:readings){out<<hr::wide(reading.id)<<L" / "<<hr::wide(reading.device)<<L"\nBattery: "<<hr::wide(reading.battery)<<L"\n";for(const auto& axis:reading.axes)out<<L"  "<<hr::wide(axis.first)<<L": "<<number(axis.second,3)<<L"\n";out<<L"\n";}
        out<<L"Xbox sticks use -1 to +1, triggers 0 to 1. Sony generic HID usages show normalized raw values; unavailable battery data is not estimated.";return out.str();
    }
    void hardware_alerts(){
        if(GetTickCount64()<next_input_alert)return;next_input_alert=GetTickCount64()+5000;update_controller_log();std::set<std::string> present;
        for(const auto& reading:input.controllers()){present.insert(reading.id);auto previous=battery_states[reading.id];battery_states[reading.id]=reading.battery;if(store.get("alerts.controller")=="1"&&(reading.battery=="Low"||reading.battery=="Empty")&&previous!=reading.battery)hr::app_toast(window,hr::wide(reading.id+" battery "+reading.battery),light_theme);}
        for(auto it=battery_states.begin();it!=battery_states.end();)if(!present.count(it->first))it=battery_states.erase(it);else ++it;
        double threshold=std::stod(store.get("alerts.thermal.threshold","85"));if(store.get("alerts.thermal")=="1"&&last.gpu_temp>=threshold){if(!thermal_since)thermal_since=GetTickCount64();if(GetTickCount64()-thermal_since>=10000&&(!last_thermal_alert||GetTickCount64()-last_thermal_alert>=300000)){hr::app_toast(window,L"GPU temperature "+number(last.gpu_temp,0)+L" C. Check cooling and load.",light_theme);last_thermal_alert=GetTickCount64();}}else thermal_since=0;
    }
    hr::CaptureOptions capture_options()const{
        hr::CaptureOptions options;options.cursor=store.get("capture.cursor","1")=="1";auto region=store.get("capture.region","full");if(region!="full"){
            std::replace(region.begin(),region.end(),',',' ');std::istringstream input(region);long long x=0,y=0,w=0,h=0;std::string extra;if(!(input>>x>>y>>w>>h)||(input>>extra)||x<-32768||y<-32768||w<1||h<1||x+w>32767||y+h>32767)throw std::runtime_error("Invalid saved capture region; select it again");options.region={static_cast<LONG>(x),static_cast<LONG>(y),static_cast<LONG>(x+w),static_cast<LONG>(y+h)};
        }hr::validate_capture_options(options);return options;
    }
    std::wstring capture_scope()const{return store.get("capture.region","full")=="full"?L"primary display":L"selected region";}
    std::wstring stutter_report(){
        auto frames=live.frames();std::wostringstream out;out<<L"STUTTER DETECTIVE\nGame: "<<hr::wide(live.game())<<L"\nMetric: "<<hr::wide(live.metric())<<L"\nApproximate association by capture-receive time, with resource samples about every two seconds. Correlation is not proof of a cause. Invalid/dropped and over-one-second intervals are excluded.\n\n";
        if(frames.size()<60){out<<L"Need at least 60 valid frames. Select the game and start its PresentMon overlay capture first.\nValid retained frames: "<<frames.size();if(!live.error().empty())out<<L"\n"<<hr::wide(live.error());return out.str();}
        std::vector<double> values;for(const auto& frame:frames)values.push_back(frame.second);auto stats=hr::calculate(values);auto sorted=values;std::nth_element(sorted.begin(),sorted.begin()+sorted.size()/2,sorted.end());double median=sorted[sorted.size()/2];
        out<<L"Retained frames: "<<frames.size()<<L" / average "<<number(stats.average)<<L" FPS / 1% low "<<number(stats.low)<<L" / P95 "<<number(stats.p95,2)<<L" ms\nLast frame received "<<(GetTickCount64()-frames.back().first)/1000<<L" seconds ago.\n\n";
        unsigned count=0;for(auto frame=frames.rbegin();frame!=frames.rend()&&count<20;++frame){if(frame->second<50||frame->second<median*2)continue;++count;out<<L"Spike "<<number(frame->second,2)<<L" ms / "<<(frames.back().first-frame->first)/1000.0<<L" seconds before last received frame\n";
            const ResourcePoint* nearest=nullptr;ULONGLONG distance=~0ULL;for(const auto& point:resource_history){auto delta=point.tick>frame->first?point.tick-frame->first:frame->first-point.tick;if(delta<distance){nearest=&point;distance=delta;}}
            if(nearest&&distance<=2500){out<<L"  Nearby CPU "<<number(nearest->cpu)<<L"% / RAM "<<number(nearest->ram)<<L"% / GPU "<<(nearest->gpu<0?L"unavailable":number(nearest->gpu)+L"%")<<L"\n  Busiest sampled process: "<<nearest->process<<L" ("<<number(nearest->top_cpu)<<L"% CPU)\n";
                if(nearest->cpu>90)out<<L"  Investigate CPU saturation/background activity.\n";if(nearest->ram>90)out<<L"  Investigate memory pressure; this alone does not establish paging.\n";if(nearest->gpu>=95)out<<L"  GPU was heavily utilized nearby; test lower rendering settings in the same scene.\n";if(nearest->read>100||nearest->write>100)out<<L"  Disk throughput was elevated nearby; throughput alone does not establish a storage bottleneck.\n";
            }else out<<L"  No close resource sample retained.\n";
        }if(!count)out<<L"No intervals met the heuristic (at least 50 ms and twice the median). This does not guarantee stutter-free play.";return out.str();
    }
    std::wstring benchmark_report(bool updates){
        auto game=selected_game();auto records=store.of("benchmark");std::reverse(records.begin(),records.end());if(game.empty()&&!records.empty())game=records.front()[2];std::vector<hr::Row> matching;for(const auto& row:records)if(hr::lower(row[2])==hr::lower(game))matching.push_back(row);
        std::wostringstream out;out<<(updates?L"UPDATE IMPACT TRACKING":L"BEFORE / AFTER PERFORMANCE")<<L"\nGame: "<<hr::wide(game)<<L"\n\n";if(matching.empty()){out<<L"No recorded benchmarks for this game yet.";return out.str();}
        auto metadata=[&](const hr::Row& row){std::map<std::string,std::string> result;auto encoded=store.get("benchmark.meta."+hr::hex(hr::encode({row})));if(!encoded.empty())try{for(const auto& field:hr::decode(encoded))if(field.size()==2)result[field[0]]=field[1];}catch(const std::exception&){}return result;};
        const auto& after=matching.front();out<<L"Newest: "<<hr::wide(after[1])<<L" / "<<hr::wide(after[4])<<L"\nAverage "<<hr::wide(after[6])<<L" FPS / 1% low "<<hr::wide(after[7])<<L" / P95 "<<hr::wide(after[8])<<L" ms\n";
        auto goal=store.get("performance.target."+hr::hex(hr::lower(game)));if(!goal.empty())try{auto rows=hr::decode(goal);if(rows.size()==1&&rows[0].size()==3){double average=std::stod(rows[0][1]),low=std::stod(rows[0][2]);out<<L"Goal average "<<number(average)<<L": "<<(std::stod(after[6])>=average?L"met":L"below target");if(low>0)out<<L" / 1% low "<<number(low)<<L": "<<(std::stod(after[7])>=low?L"met":L"below target");out<<L"\n";}}catch(const std::exception&){}
        const hr::Row* before=nullptr;for(size_t i=1;i<matching.size();++i)if(matching[i][4]==after[4]){before=&matching[i];break;}if(!before){out<<L"\nNeed another older capture with this exact frame metric to compare.";return out.str();}
        auto old_meta=metadata(*before),new_meta=metadata(after);out<<L"\nCompared with "<<hr::wide((*before)[1])<<L"\nAverage delta "<<number(std::stod(after[6])-std::stod((*before)[6]))<<L" FPS\n1% low delta "<<number(std::stod(after[7])-std::stod((*before)[7]))<<L" FPS\nP95 delta "<<number(std::stod(after[8])-std::stod((*before)[8]),2)<<L" ms (lower is better)\n";
        double old_frames=std::stod((*before)[5]),new_frames=std::stod(after[5]);if(old_frames>0&&new_frames>0)out<<L"Stutters per 1,000 frames: "<<number(1000*std::stod((*before)[9])/old_frames,2)<<L" -> "<<number(1000*std::stod(after[9])/new_frames,2)<<L"\n";
        bool comparable=!new_meta["scene"].empty()&&new_meta["scene"]==old_meta["scene"];out<<L"\nScene/settings notes: "<<(comparable?L"match":L"not established as matching; do not attribute this difference to a tweak or update")<<L"\n";
        if(!new_meta["scene"].empty())try{auto notes=hr::decode(new_meta["scene"]);for(const auto& row:notes)for(const auto& value:row)out<<hr::wide(value)<<L" / ";out<<L"\n";}catch(const std::exception&){}
        if(updates){bool environments=!new_meta["environment"].empty()&&!old_meta["environment"].empty(),drivers=!new_meta["drivers"].empty()&&!old_meta["drivers"].empty();out<<L"Environment fingerprint: "<<(!environments?L"unavailable":new_meta["environment"]==old_meta["environment"]?L"unchanged":L"changed")<<L"\nDriver fingerprint: "<<(!drivers?L"unavailable":new_meta["drivers"]==old_meta["drivers"]?L"unchanged":L"changed")<<L"\nA changed fingerprint is context, not evidence that the update caused the difference.";}
        return out.str();
    }
    void run_hub(bool apply){
        if(hub_busy)throw std::runtime_error("Wait for the gamer tool or cancel it");int i=selection(lists[20]);if(i<0||static_cast<size_t>(i)>=visible_hub.size())throw std::runtime_error("Select a gamer tool");auto tool=visible_hub[i];hub_cancel=std::make_shared<std::atomic<bool>>(false);auto first=hr::utf8(text(hub_first)),second=hr::utf8(text(hub_second));
        if(tool.id=="overlay-editor"){show_page(23);return;}
        if(tool.id=="capture-files"){show_page(22);return;}
        if(tool.id.rfind("system.",0)==0){auto id=tool.id.substr(7);SendMessageW(tool_category,CB_SETCURSEL,0,0);filter_tools();show_page(18);for(size_t i=0;i<visible_tools.size();++i)if(visible_tools[i].id==id){ListView_SetItemState(lists[18],static_cast<int>(i),LVIS_SELECTED,LVIS_SELECTED);ListView_EnsureVisible(lists[18],static_cast<int>(i),FALSE);describe_tool();break;}return;}
        if(tool.id=="maintenance-history"){std::wostringstream out;out<<L"MAINTENANCE HISTORY / MOST RECENT 300 EVENTS\n\n";auto rows=store.of("history");for(auto row=rows.rbegin();row!=rows.rend();++row)out<<hr::wide((*row)[1]+" / "+(*row)[2]+"\n"+(*row)[3]+"\n\n");SetWindowTextW(hub_output,out.str().c_str());return;}
        if(tool.id=="stutters"){SetWindowTextW(hub_output,stutter_report().c_str());return;}
        if(tool.id=="benchmark-report"||tool.id=="update-impact"){SetWindowTextW(hub_output,benchmark_report(tool.id=="update-impact").c_str());return;}
        if(tool.id=="controller"||tool.id=="controller-history"){hub_live=tool.id;EnableWindow(hub_cancel_button,TRUE);SetWindowTextW(hub_output,controller_report(tool.id=="controller-history").c_str());return;}
        hub_live.clear();if(tool.id=="thermal"){auto current=last.gpu_temp<0?L"GPU temperature unavailable on this hardware/driver.":L"Current GPU temperature: "+number(last.gpu_temp,0)+L" C.";SetWindowTextW(hub_output,current.c_str());}
        if(apply&&tool.needs_review){
            std::string review=tool.title+"\n\n"+tool.description+"\n\nFirst input: "+first+"\nSecond input: "+second;
            if(tool.id=="diagnostic-export"){second=hr::gamer_report(tool.id,"","",*hub_cancel);review="Export this exact report to "+first+"?\n\n"+second;}
            if(!confirm(hr::wide(review)))return;
        }
        if(tool.id=="capture-privacy"){
            if(recorder.active()||replay->active())throw std::runtime_error("Stop recording/replay before changing capture scope");auto cursor=hr::lower(first);if(second.empty())throw std::runtime_error("Choose full explicitly or select a capture region");if(cursor!="on"&&cursor!="off")throw std::runtime_error("Enter cursor on or off");auto old_cursor=store.get("capture.cursor","1"),old_region=store.get("capture.region","full");store.set("capture.cursor",cursor=="on"?"1":"0");store.set("capture.region",hr::lower(second));
            try{capture_options();}catch(...){store.set("capture.cursor",old_cursor);store.set("capture.region",old_region);throw;}store.save();SetWindowTextW(hub_output,L"Capture scope saved for future screenshots/recordings. Only the Windows cursor is toggled; a game's own rendered reticle remains part of its image.");return;
        }
        if(tool.id=="trim-clip"){
            if(first.empty())throw std::runtime_error("Choose an MP4 file");auto comma=second.find(',');if(comma==std::string::npos||second.find(',',comma+1)!=std::string::npos)throw std::runtime_error("Enter start,end in seconds, for example 5,20");
            auto number=[](const std::string& text){size_t used=0;double value=std::stod(text,&used);if(used!=text.size()||!std::isfinite(value))throw std::runtime_error("Enter valid start,end seconds");return value;};double begin=number(second.substr(0,comma)),end=number(second.substr(comma+1));if(begin<0||end<=begin||end-begin>600)throw std::runtime_error("Choose an increasing range up to ten minutes");auto path=std::filesystem::path(hr::wide(first));auto cancel=hub_cancel;hub_busy=true;hub_pending_id=tool.id;EnableWindow(hub_cancel_button,TRUE);async("gamer",[path,begin,end,cancel](Result& r){r.text="Exported new clip:\n"+hr::utf8(hr::export_clip(path,begin,end,*cancel).wstring())+"\nOriginal file preserved.";r.mode="Clip export";});return;
        }
        if(tool.id=="crop-image"){
            auto path=first.empty()?hr::app_file(window,L"Choose screenshot",L".png",light_theme):hr::wide(first);if(path.empty())return;
            HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Close programs writing this image before cropping");auto source_lock=std::shared_ptr<void>(locked,[](void* h){CloseHandle(h);});auto crop=hr::app_crop_region(window,path,light_theme);if(!crop)return;hub_busy=true;hub_pending_id=tool.id;EnableWindow(hub_cancel_button,FALSE);async("gamer",[path,crop,source_lock](Result& r){r.text="New cropped image:\n"+hr::utf8(hr::crop_screenshot(path,*crop).wstring());r.mode="Crop screenshot";});return;
        }
        if(tool.id=="favorite-game"){
            auto game=first.empty()?selected_game():first;if(game.empty()||std::none_of(games.begin(),games.end(),[&](const hr::Row& row){return hr::lower(row[2])==hr::lower(game);}))throw std::runtime_error("Choose a game already in the Games library");auto value=hr::lower(second);if(value!="on"&&value!="off")throw std::runtime_error("Enter on or off");store.set("favorite.game."+hr::hex(hr::lower(game)),value=="on"?"1":"0");store.save();SetWindowTextW(hub_output,L"Tray game favorite saved.");return;
        }
        if(tool.id=="benchmark-notes"||tool.id=="performance-target"){
            auto game=selected_game();if(game.empty())throw std::runtime_error("Select the game in Games first");auto key=hr::hex(hr::lower(game));
            if(tool.id=="benchmark-notes"){if(first.empty())throw std::runtime_error("Enter a repeatable scene name");store.set("benchmark.notes."+key,hr::encode({{"Scene",first,second}}));SetWindowTextW(hub_output,L"Scene/settings notes saved for future native captures of this game. Imported older CSVs do not inherit a claimed past environment.");}
            else{auto number=[](const std::string& text){size_t used=0;double value=std::stod(text,&used);if(used!=text.size()||!std::isfinite(value)||value<1||value>2000)throw std::runtime_error("FPS goal must be from 1 to 2000");return value;};double average=number(first),low=second.empty()?0:number(second);if(low>average)throw std::runtime_error("1% low goal cannot exceed the average goal");store.set("performance.target."+key,hr::encode({{"target",first,second.empty()?"0":second}}));SetWindowTextW(hub_output,L"Performance goals saved. Before/after reports evaluate future or existing captured results without changing a game's frame cap.");}store.save();return;
        }
        if(tool.id=="game-profile"||tool.id=="profile-disable"||tool.id=="background-sleep"){
            auto game=first.empty()?selected_game():first;if(game.empty()||!std::filesystem::is_regular_file(hr::wide(game))||hr::lower(hr::utf8(std::filesystem::path(hr::wide(game)).extension().wstring()))!=".exe")throw std::runtime_error("Select an existing game executable first");game=hr::utf8(std::filesystem::weakly_canonical(hr::wide(game)).wstring());auto key=hr::hex(hr::lower(game));
            if(tool.id=="game-profile"){auto profile=hr::lower(second);const std::set<std::string> allowed={"competitive","streaming","laptop","quiet","everyday"};if(!allowed.count(profile))throw std::runtime_error("Choose competitive, streaming, laptop, quiet or everyday");auto full=hr::game_profile(store,game);full.preset=profile;full.enabled=true;hr::save_game_profile(store,full);SetWindowTextW(hub_output,L"Opt-in saved. This exact game executable will start a local monitored session when detected. The temporary priority guard restores its original state when the session ends.");}
            else if(tool.id=="profile-disable"){auto full=hr::game_profile(store,game);full.enabled=false;hr::save_game_profile(store,full);if(hr::lower(sessions.active_game())==hr::lower(game))sessions.stop("Profile disabled");SetWindowTextW(hub_output,L"Automatic session profile disabled; its prior history is kept.");}
            else{std::istringstream paths(second);std::string path;unsigned count=0;while(std::getline(paths,path,';'))if(!path.empty()){if(++count>12||!std::filesystem::is_regular_file(hr::wide(path))||hr::lower(hr::utf8(std::filesystem::path(hr::wide(path)).extension().wstring()))!=".exe")throw std::runtime_error("Use up to 12 existing app executable paths separated by semicolons");}store.set("session.sleep."+key,second);store.save();SetWindowTextW(hub_output,L"Explicit background-app sleep list saved for the next opted-in session. Unsupported/protected targets are refused at use time and recorded in the session.");}return;
        }
        if(tool.id=="controller-alerts"||tool.id=="thermal"){
            auto enabled=hr::lower(first);if(enabled!="on"&&enabled!="off")throw std::runtime_error("Enter on or off");
            if(tool.id=="thermal"){auto value=second.empty()?store.get("alerts.thermal.threshold","85"):second;size_t used=0;double threshold=std::stod(value,&used);if(used!=value.size()||!std::isfinite(threshold)||threshold<50||threshold>110)throw std::runtime_error("Thermal threshold must be 50 to 110 Celsius");store.set("alerts.thermal.threshold",value);}
            store.set(tool.id=="thermal"?"alerts.thermal":"alerts.controller",enabled=="on"?"1":"0");store.save();SetWindowTextW(hub_output,L"Alert preferences saved. Notifications use actual exposed telemetry only.");return;
        }
        if(tool.id=="deadzone"){
            size_t used=0;double value=std::stod(second,&used);if(used!=second.size()||!std::isfinite(value)||value<0||value>.8)throw std::runtime_error("Dead zone must be 0 to 0.8");store.set("deadzone.global",second);store.save();input.set_deadzone(value);SetWindowTextW(hub_output,L"System-wide NovaTune right-stick-to-mouse dead zone saved. Native gamepad axes are unchanged.");return;
        }
        hub_busy=true;hub_pending_id=tool.id;EnableWindow(hub_cancel_button,TRUE);auto cancel=hub_cancel;SetWindowTextW(hub_output,L"Working... Cancel stops between bounded operations.");
        async("gamer",[tool,first,second,apply,cancel](Result& r){r.text=apply?hr::gamer_action(tool.id,first,second,*cancel):hr::gamer_report(tool.id,first,second,*cancel);r.mode=tool.title;});
    }
    void refresh_recovery(){
        recovery_indices.clear();ListView_DeleteAllItems(lists[21]);for(size_t i=store.rows.size();i-->0;){const auto& row=store.rows[i];if(row[0]!="journal")continue;recovery_indices.push_back(i);add(lists[21],{hr::wide(row[1]),hr::wide(row[2]+(row[5].empty()?"":" / "+row[5])),hr::wide(row[3].substr(0,120)),hr::wide(row[4].substr(0,120))});}describe_recovery();
    }
    void filter_tools(){
        if(!lists[18])return;remember_system_report();std::string previous;int row=selection(lists[18]);if(row>=0&&static_cast<size_t>(row)<visible_tools.size())previous=visible_tools[row].id;visible_tools.clear();ListView_DeleteAllItems(lists[18]);auto category=hr::utf8(text(tool_category));
        for(const auto& tool:tools)if(category=="All tools"||category.empty()||category==tool.category){visible_tools.push_back(tool);add(lists[18],{hr::wide(tool.name),hr::wide(tool.category),tool.security?L"Security tradeoff":tool.changes?L"Review change":L"Read-only / open"});}
        if(!visible_tools.empty()){size_t selected=0;for(size_t n=0;n<visible_tools.size();++n)if(visible_tools[n].id==previous)selected=n;ListView_SetItemState(lists[18],static_cast<int>(selected),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}describe_tool();
    }
    void remember_system_report(){if(!system_selected.empty()&&(!tool_busy||system_selected!=system_pending))system_reports[system_selected]=text(tool_output);}
    void describe_tool(){
        int selected=selection(lists[18]);bool valid=selected>=0&&static_cast<size_t>(selected)<visible_tools.size();EnableWindow(controls[18][1],valid&&!tool_busy);if(!valid){remember_system_report();EnableWindow(controls[18][2],FALSE);system_selected.clear();SetWindowTextW(tool_description,L"Select a tool to view its scope and availability.");return;}
        auto& tool=visible_tools[selected];auto details=hr::inspect_tool(tool.id);bool available=details.rfind("Unavailable:",0)!=0;EnableWindow(controls[18][2],available&&!tool_busy);SetWindowTextW(tool_description,hr::wide(tool.description).c_str());SetWindowTextW(controls[18][2],tool.changes?L"Review / apply":L"Run selected");
        if(system_selected!=tool.id){remember_system_report();system_selected=tool.id;auto previous=system_reports.find(tool.id);SetWindowTextW(tool_output,previous==system_reports.end()?hr::wide(details).c_str():previous->second.c_str());}
    }
    void start_tool(bool inspect){
        if(tool_busy)throw std::runtime_error("Wait for the current operation");
        int selected=selection(lists[18]);if(selected<0||static_cast<size_t>(selected)>=visible_tools.size())throw std::runtime_error("Select a system tool");
        auto tool=visible_tools[selected];
        if(inspect){auto details=hr::wide(hr::inspect_tool(tool.id));system_reports[tool.id]=details;SetWindowTextW(tool_output,details.c_str());return;}
        if(tool.changes&&!confirm(hr::wide(tool.name+"\n\n"+tool.description+(tool.restart?"\n\nA restart/sign-out may be needed. NovaTune will not restart automatically.":""))))return;
        active_log=hr::data_dir()/L"logs"/(hr::wide(tool.id)+L"-"+std::to_wstring(GetTickCount64())+L".log");
        const std::set<std::string> background={"component-analyze","component-clean","repair","sfc","recall-info","recall-remove","dns","net-info","tcp-reset","winsock","update-repair"};
        if(background.count(tool.id)){
            tool_busy=true;system_pending=tool.id;active_command_output=tool_output;SetWindowTextW(tool_output,L"Operation running. Native repair commands are not force-terminated. Output appears here as it is written.");
            auto snapshot=store;auto log=active_log;
            async("tool",[snapshot=std::move(snapshot),tool,log](Result& r)mutable{r.command=hr::run_system_tool(snapshot,tool.id,log);r.text=tool.name;});
        }else{
            auto result=hr::run_system_tool(store,tool.id,active_log);system_reports[tool.id]=hr::wide(result.output);SetWindowTextW(tool_output,system_reports[tool.id].c_str());refresh_saved();
        }
    }
    void refresh_package_availability(){if(!package_output)return;bool available=true;std::string reason;try{hr::winget_path();}catch(const std::exception& e){available=false;reason=e.what();}for(int i=1;i<=6;++i)EnableWindow(controls[19][i],available&&!tool_busy);EnableWindow(controls[19][7],!tool_busy);if(!available&&!tool_busy)SetWindowTextW(package_output,hr::wide("WinGet unavailable. Install Microsoft App Installer, then reopen Packages.\n\n"+reason).c_str());}
    void start_package(const std::string& action){
        if(tool_busy)throw std::runtime_error("Wait for the current operation");
        auto query=hr::utf8(text(package_query));
        if(action!="search"&&action!="updates"&&!confirm(hr::wide("WinGet "+action+" for these exact IDs:\n"+query+"\n\nThis can download/change software and accepts the selected package/source agreements. Applications may need to close.")))return;
        tool_busy=true;active_command_output=package_output;active_log=hr::data_dir()/L"logs"/(L"winget-"+std::to_wstring(GetTickCount64())+L".log");auto log=active_log;
        SetWindowTextW(package_output,L"Windows Package Manager is running...");
        async("package",[action,query,log](Result& r){r.command=hr::winget(action,query,log);r.text="WinGet "+action;});refresh_package_availability();describe_tool();
    }

    void filter_installed(){
        if(!lists[16])return;
        auto search=hr::lower(hr::utf8(text(app_search)));visible_installed.clear();
        SendMessageW(lists[16],WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(lists[16]);
        for(const auto& app:installed)if(search.empty() || hr::lower(app.name+" "+app.publisher).find(search)!=std::string::npos){
            visible_installed.push_back(app);add(lists[16],{hr::wide(app.name),hr::wide(app.publisher),hr::wide(app.version),app.machine?L"Machine":L"User"});
        }
        SendMessageW(lists[16],WM_SETREDRAW,TRUE,0);InvalidateRect(lists[16],nullptr,FALSE);
    }
    void scan_installed(){
        if(installed_busy)return;installed_busy=true;say(L"Reading registered desktop applications...");
        async("installed",[](Result& r){r.installed=hr::installed_apps();});
    }
    void scan_apps(){
        if(app_busy)return;app_busy=true;say(L"Reading optional apps for the current user...");
        async("apps",[](Result& r){r.apps=hr::optional_apps();});
    }
    void load_overlay_editor(){
        const auto& s=overlay_settings;SendMessageW(overlay_anchor,CB_SETCURSEL,s.anchor,0);const int scales[]={75,100,125,150,200},opacities[]={128,191,230,255};int scale=1,opacity=2;for(int n=0;n<5;++n)if(scales[n]==s.scale)scale=n;for(int n=0;n<4;++n)if(opacities[n]==s.opacity)opacity=n;SendMessageW(overlay_scale,CB_SETCURSEL,scale,0);SendMessageW(overlay_accent,CB_SETCURSEL,s.accent,0);SendMessageW(overlay_layout,CB_SETCURSEL,s.layout,0);SendMessageW(overlay_display,CB_SETCURSEL,s.display,0);SendMessageW(overlay_opacity,CB_SETCURSEL,opacity,0);SetWindowTextW(overlay_x,std::to_wstring(s.margin_x).c_str());SetWindowTextW(overlay_y,std::to_wstring(s.margin_y).c_str());for(int n=0;n<6;++n)SendMessageW(overlay_metrics[n],BM_SETCHECK,s.metrics&(1u<<n)?BST_CHECKED:BST_UNCHECKED,0);InvalidateRect(overlay_preview,nullptr,FALSE);
    }
    hr::OverlaySettings edited_overlay(){
        hr::OverlaySettings s;auto choice=[](HWND control,int maximum){auto value=SendMessageW(control,CB_GETCURSEL,0,0);if(value<0||value>maximum)throw std::runtime_error("Choose an overlay option");return static_cast<int>(value);};const int scales[]={75,100,125,150,200},opacities[]={128,191,230,255};s.anchor=choice(overlay_anchor,3);s.scale=scales[choice(overlay_scale,4)];s.accent=choice(overlay_accent,3);s.layout=choice(overlay_layout,1);s.display=choice(overlay_display,1);s.opacity=opacities[choice(overlay_opacity,3)];auto margin=[](HWND field){auto value=text(field);size_t used=0;int number=std::stoi(value,&used);if(used!=value.size())throw std::runtime_error("Enter a whole-pixel margin");return number;};s.margin_x=margin(overlay_x);s.margin_y=margin(overlay_y);s.metrics=0;for(int n=0;n<6;++n)if(SendMessageW(overlay_metrics[n],BM_GETCHECK,0,0)==BST_CHECKED)s.metrics|=1u<<n;hr::validate_overlay(s);return s;
    }
    void place_overlay(){
        if(!overlay)return;POINT primary{0,0};HMONITOR monitor=overlay_settings.display?MonitorFromPoint(primary,MONITOR_DEFAULTTOPRIMARY):MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST);MONITORINFO info{};info.cbSize=sizeof info;if(!GetMonitorInfoW(monitor,&info))throw std::runtime_error(hr::error());auto bounds=hr::overlay_bounds(overlay_settings,info.rcWork,dpi);SetWindowPos(overlay,HWND_TOPMOST,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOACTIVATE);SetLayeredWindowAttributes(overlay,0,static_cast<BYTE>(overlay_settings.opacity),LWA_ALPHA);InvalidateRect(overlay,nullptr,FALSE);
    }
    void draw_overlay(HDC dc,RECT rect,bool preview=false){
        auto settings=overlay_settings;if(preview)try{settings=edited_overlay();}catch(const std::exception&){}FillRect(dc,&rect,background);auto desired=hr::overlay_size(settings,96);double factor=std::min(static_cast<double>(rect.right-rect.left)/desired.cx,static_cast<double>(rect.bottom-rect.top)/desired.cy)*settings.scale/100.0;auto unit=[&](int value){return std::max(1,static_cast<int>(std::lround(value*factor)));};int width=unit(settings.layout?640:470);const COLORREF accents[]={RGB(26,242,44),RGB(0,216,255),RGB(180,95,255),RGB(245,245,247)};auto color=accents[settings.accent];HFONT hud_font=CreateFontW(-unit(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");auto old_font=SelectObject(dc,hud_font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);RECT line{unit(16),unit(12),width-unit(12),unit(38)};DrawTextW(dc,L"NOVATUNE  /  LIVE",-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS);
        std::vector<std::wstring> values={L"CPU "+number(last.cpu)+L"%",L"RAM "+number(last.used)+L" / "+number(last.total)+L" GB",last.gpu_util>=0?L"GPU "+number(last.gpu_util)+L"%":L"GPU unavailable",last.vram_total>0?L"VRAM "+number(last.vram_used/1073741824.0)+L" / "+number(last.vram_total/1073741824.0)+L" GB":L"VRAM unavailable",live.fps()>0?L"FPS "+number(live.fps())+L" (PresentMon)":L"FPS waiting / unavailable",last.disk_read>=0&&last.disk_write>=0?L"Disk R "+number(last.disk_read)+L" / W "+number(last.disk_write)+L" MB/s":L"Disk I/O unavailable"};int visible=0;for(int n=0;n<6;++n)if(settings.metrics&(1u<<n)){int column=settings.layout?visible%2:0,row=settings.layout?visible/2:visible,cell=settings.layout?width/2:width;line={column*cell+unit(16),unit(40+row*28),(column+1)*cell-unit(12),unit(66+row*28)};SetTextColor(dc,n==4?color:ink);DrawTextW(dc,values[n].c_str(),-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS);++visible;}
        int rows=settings.layout?(visible+1)/2:visible;line={unit(16),unit(46+rows*28),width-unit(12),unit(78+rows*28)};SetTextColor(dc,color);auto note=live.error().empty()?L"Ctrl+Alt+O to hide | Windowed / borderless":hr::wide(live.error());DrawTextW(dc,note.c_str(),-1,&line,DT_SINGLELINE|DT_END_ELLIPSIS);SelectObject(dc,old_font);DeleteObject(hud_font);
    }
    void toggle_overlay(){
        if(overlay){if(!timeline_requested)live.stop();DestroyWindow(overlay);overlay=nullptr;say(L"Game overlay hidden.");return;}
        WNDCLASSW cls{};cls.lpfnWndProc=overlay_procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"HeadroomOverlay";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
        overlay=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,L"HeadroomOverlay",L"NovaTune Overlay",WS_POPUP,24,42,470,224,nullptr,nullptr,GetModuleHandleW(nullptr),this);
        if(!overlay)throw std::runtime_error(hr::error());
        place_overlay();ShowWindow(overlay,SW_SHOWNOACTIVATE);
        overlay_game=selected_game();
        if(!overlay_game.empty() && !store.get("presentmon").empty()){
            try{live.start(store.get("presentmon"),overlay_game);}catch(const std::exception& e){say(hr::wide(e.what()));}
        }
        say(L"Overlay active. Live FPS needs a selected game and PresentMon; Ctrl+Alt+O toggles.");
    }
    void save_benchmark(const hr::Benchmark& b,const std::string& game,const std::string& mode,const std::string& csv,const hr::Row& metadata={}){
        hr::Row row{"benchmark",hr::now(),game,mode,b.metric,std::to_string(b.frames),hr::utf8(number(b.average)),hr::utf8(number(b.low)),hr::utf8(number(b.p95,2)),std::to_string(b.stutters),csv};store.rows.push_back(row);
        if(metadata.size()==4)store.set("benchmark.meta."+hr::hex(hr::encode({row})),hr::encode({{"origin",metadata[0]},{"scene",metadata[1]},{"environment",metadata[2]},{"drivers",metadata[3]}}));
        store.log("Benchmark",game+": "+hr::utf8(number(b.average))+" FPS");refresh_saved();say(L"Benchmark saved.");
    }

    void refresh_processes(){
        auto list=lists[15];if(!list)return;auto cell=[&](int row,int column){wchar_t value[64]{};if(row>=0)ListView_GetItemText(list,row,column,value,64);return std::wstring(value);};
        std::optional<DWORD> selected;int selected_row=selection(list);if(selected_row>=0)selected=static_cast<DWORD>(wcstoul(cell(selected_row,1).c_str(),nullptr,10));
        int top=ListView_GetTopIndex(list),horizontal=GetScrollPos(list,SB_HORZ),wanted_top=std::clamp(top,0,std::max(0,static_cast<int>(last.processes.size())-1));
        SendMessageW(list,WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(list);
        for(size_t n=0;n<last.processes.size();++n){const auto& process=last.processes[n];auto pid=std::to_wstring(process.pid);add(list,{process.name,pid,number(process.ram),number(process.cpu)});if(selected&&*selected==process.pid)ListView_SetItemState(list,static_cast<int>(n),LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);}
        RECT row{};int height=ListView_GetItemRect(list,0,&row,LVIR_BOUNDS)?row.bottom-row.top:px(20);ListView_Scroll(list,horizontal-GetScrollPos(list,SB_HORZ),(wanted_top-ListView_GetTopIndex(list))*height);SendMessageW(list,WM_SETREDRAW,TRUE,0);InvalidateRect(list,nullptr,FALSE);
    }
    void drain(){
        if(page==32&&network_busy)InvalidateRect(network_cards,nullptr,FALSE);
        std::deque<Result> results;
        {std::lock_guard<std::mutex> lock(mailbox->mutex);results.swap(mailbox->results);}
        update_advanced();
        if(desktop){if(desktop->active()&&GetTickCount64()>desktop_game_check){desktop_game_check=GetTickCount64()+1000;desktop_gaming=sessions.active()||hr::gaming_now(store);}desktop->tick(last,games,desktop_gaming);}
        if(overlay)InvalidateRect(overlay,nullptr,FALSE);if(page==23)InvalidateRect(overlay_preview,nullptr,FALSE);
        if(page==11)update_mouse_readback();
        if(page==34)update_center_status();
        if(input.take_emergency_stop()){SendMessageW(remap_enabled,BM_SETCHECK,BST_UNCHECKED,0);store.set("remap.enabled","0");store.save();say(L"Remapping stopped with Pause. Click Start remapping to resume.");}
        EnableWindow(GetDlgItem(window,StartRemapping),!input.enabled());EnableWindow(GetDlgItem(window,StopRemapping),input.enabled()||input.listening());
        if(input_status){auto value=hr::wide(input.status());if(text(input_status)!=value)SetWindowTextW(input_status,value.c_str());}
        if(device_view && page==12)InvalidateRect(device_view,nullptr,FALSE);
        if(listen_button){auto value=input.listening()?L"Listening...":L"Capture input";if(text(listen_button)!=value)SetWindowTextW(listen_button,value);}
        if(auto captured=input.take_capture())select_source(captured->device,captured->code);
        update_stress();hardware_alerts();
        if(page==20&&!hub_live.empty()){auto output=controller_report(hub_live=="controller-history");if(text(hub_output)!=output)SetWindowTextW(hub_output,output.c_str());}
        if(tool_busy&&!active_log.empty()){
            auto output=hr::read_output(active_log);if(output.size()>64000)output=output.substr(output.size()-64000);
            HWND target=active_command_output;if(target&&(target!=tool_output||system_selected==system_pending)&&!output.empty()&&text(target)!=hr::wide(output))SetWindowTextW(target,hr::wide(output).c_str());
        }
        if(capture_status){
            auto message=recorder.active()?L"Recording "+capture_scope()+L": "+std::to_wstring(recorder.elapsed())+L" seconds. MP4, 720p, 30 FPS, no audio.":recorder.error().empty()?L"Ready. Recording "+capture_scope()+L": MP4, 720p, up to 30 FPS, no audio.":hr::wide(recorder.error());
            if(text(capture_status)!=message)SetWindowTextW(capture_status,message.c_str());
        }
        for(auto& r:results){
            if(r.kind=="internet-speed"||r.kind=="internet-inspect"){network_busy=false;InvalidateRect(network_go,nullptr,FALSE);InvalidateRect(network_cards,nullptr,FALSE);EnableWindow(GetDlgItem(window,InternetDns),TRUE);EnableWindow(GetDlgItem(window,InternetRepair),TRUE);SetWindowTextW(network_go,L"Start speed test");EnableWindow(network_mode,TRUE);EnableWindow(GetDlgItem(window,InternetInspect),TRUE);auto report=r.error.empty()?r.text:"Network operation failed: "+r.error;SetWindowTextW(network_report,hr::wide(report).c_str());store.set("network.last",report);store.save();say(L"Network operation finished. Review the measured results.");continue;}
            if(r.kind=="comparison-before"||r.kind=="comparison-after"){comparison_busy=false;if(r.error.empty()){store.set(r.kind=="comparison-before"?"compare.before":"compare.after",r.text);if(r.kind=="comparison-before")store.set("compare.after","");store.save();say(L"Measured snapshot saved. Compare under the same workload.");}else say(hr::wide(r.error));refresh_comparison();continue;}
            if(advanced_result(r))continue;
            if(r.kind=="suggestions"||r.kind=="suggestion-clean") {suggestions_busy=false;EnableWindow(suggestion_scan,TRUE);}
            if(r.kind=="gallery"||r.kind=="gallery-crop"){gallery_busy=false;EnableWindow(controls[22][1],TRUE);}
            if(r.kind=="gamer"){hub_busy=false;update_hub_actions();}
            if(r.kind=="autoruns")startup_busy=false;
            if(r.kind=="cache_scan"||r.kind=="storage"||r.kind=="cache_clean"||r.kind=="duplicate_clean")storage_busy=false;
            if(r.kind=="tool"||r.kind=="package"){tool_busy=false;describe_tool();refresh_package_availability();}
            if(r.kind=="installed")installed_busy=false;
            if(r.kind=="sample")monitoring=false;if(r.kind=="boost-scan"||r.kind=="boost-done")boost_busy=false;
            if(r.kind=="services")scanning_services=false;
            if(r.kind=="drivers")scanning_drivers=false;
            if(r.kind=="capture")recording=false;
            if(r.kind=="temp" || r.kind=="clean")temp_busy=false;
            if(r.kind=="apps" || r.kind=="app_remove" || r.kind=="app_restore" || r.kind=="debloat")app_busy=false;
            if(!r.error.empty()){if(r.kind=="tool"){auto message=hr::wide("Operation failed: "+r.error);system_reports[system_pending]=message;if(system_selected==system_pending)SetWindowTextW(tool_output,message.c_str());system_pending.clear();}if(r.kind=="package")SetWindowTextW(package_output,hr::wide("WinGet failed: "+r.error).c_str());if(r.kind=="boost-scan"||r.kind=="boost-done")boost_phase=L"FAILED";
                if(r.kind=="suggestion-clean"){update_suggestion_badge();hr::update_fix_review(fix_review,"temp",hr::wide(r.error),false);say(L"Cleanup failed. Review fixes to retry.");}
                else if(r.kind=="sample")say(hr::wide(r.error));else failure(r.error);if(r.kind=="gallery"||r.kind=="gallery-crop")describe_capture();continue;
            }
            if(r.kind=="boost-scan"){if(r.boost){boost_phase=L"APPLYING";boost_busy=true;std::string details;try{details=hr::apply_boost_settings(store,*r.boost);}catch(...){boost_busy=false;boost_phase=L"FAILED";throw;}auto files=r.boost->temp;auto mode=r.boost->mode;refresh_saved();async("boost-done",[files,details,mode](Result& r){r.cleanup=hr::clean_temp(files);r.mode=mode;r.text=details+"\nOld temp files removed: "+std::to_string(r.cleanup.deleted)+" ("+std::to_string(r.cleanup.bytes/1048576)+" MB).\nChanged/locked files skipped: "+std::to_string(r.cleanup.skipped)+".\n\nNo FPS gain is claimed without measuring the same workload.";});}else boost_phase=L"BOOST";continue;}
            if(r.kind=="boost-done"){boost_flash=GetTickCount64()+5000;boost_phase=L"DONE";store.set("boost.last",r.text);store.log("Boost completed",r.text);auto notice=hr::wide(r.mode+" boost complete: "+std::to_string(r.cleanup.deleted)+" temp files removed ("+std::to_string(r.cleanup.bytes/1048576)+" MB). Power settings verified.");say(notice);hr::app_toast(window,notice,light_theme);continue;}
            if(r.kind=="gallery"){gallery_files=std::move(r.media);filter_gallery(false);}
            else if(r.kind=="gallery-crop"){if(!r.mode.empty())store.set(gallery_key(hr::wide(r.text)),r.mode);store.log("Screenshot cropped",r.text);store.save();gallery_keep=hr::wide(r.text);scan_gallery(false);say(L"Cropped image saved; original preserved.");}
            else if(r.kind=="gamer"){hub_reports[hub_pending_id]=hr::wide(r.text);if(hub_selected==hub_pending_id)SetWindowTextW(hub_output,hub_reports[hub_pending_id].c_str());else say(hr::wide(r.mode+" finished; select that tool to view its report."));store.log("Gamer tool: "+r.mode,"Finished; output shown locally");refresh_saved();}
            else if(r.kind=="suggestions"){
                suggestions_scanned=true;suggestion_files=std::move(r.temp);suggestion_capture=r.mode=="capture";unsigned long long bytes=0;for(const auto& file:suggestion_files)bytes+=file.size;
                auto message=L"SUGGESTED FIXES  /  "+std::to_wstring(suggestion_files.size())+L" old temp files ("+number(bytes/1048576.0,1)+L" MB). "+(suggestion_capture?L"Background recording enabled.":L"No explicit recording flag found.");
                SetWindowTextW(suggestion_note,message.c_str());update_suggestion_badge();say(L"Scan complete. Review fixes to approve changes. No FPS gain is guaranteed.");
            }else if(r.kind=="suggestion-clean"){
                suggestion_files.clear();update_suggestion_badge();
                hr::update_fix_review(fix_review,"temp",L"Finished: "+std::to_wstring(r.cleanup.deleted)+L" files removed; "+std::to_wstring(r.cleanup.skipped)+L" skipped. Rescan to check remaining files.",true);
                auto message=L"FIXES COMPLETE  /  "+std::to_wstring(r.cleanup.deleted)+L" old temp files removed; "+std::to_wstring(r.cleanup.skipped)+L" changed or locked files skipped. Scan again to refresh.";
                SetWindowTextW(suggestion_note,message.c_str());store.log("Approved dashboard cleanup",hr::utf8(message));refresh_saved();
            }else if(r.kind=="sample"){
                last=std::move(r.sample);
                try{sessions.tick(last,store,hr::lower(sessions.active_game())==hr::lower(overlay_game)?live.fps():0);if(!sessions.error().empty())say(hr::wide(sessions.error()));automatic_session_backup();}catch(const std::exception& e){say(hr::wide(e.what()));}
                const hr::Process* busiest=nullptr;for(const auto& process:last.processes)if(!busiest||process.cpu>busiest->cpu)busiest=&process;
                resource_history.push_back({GetTickCount64(),last.cpu,last.total?100*last.used/last.total:0,last.gpu_util,last.disk_read,last.disk_write,busiest?busiest->cpu:0,busiest?busiest->name:L"unavailable"});while(resource_history.size()>120)resource_history.pop_front();
                {auto& point=resource_history.back();auto processes=last.processes;std::sort(processes.begin(),processes.end(),[](const hr::Process& a,const hr::Process& b){return a.cpu>b.cpu;});auto game_name=hr::lower(hr::utf8(std::filesystem::path(hr::wide(overlay_game)).filename().wstring()));DWORD own_session=0;ProcessIdToSessionId(GetCurrentProcessId(),&own_session);for(const auto& process:processes){DWORD session=0;ProcessIdToSessionId(process.pid,&session);if(process.pid==GetCurrentProcessId()||session!=own_session||hr::lower(hr::utf8(process.name))==game_name)continue;point.processes.push_back(process);if(point.processes.size()==5)break;}}
                cpu_trend.push_back(last.cpu);ram_trend.push_back(last.total?last.used/last.total*100:0);gpu_trend.push_back(last.gpu_util);
                for(auto trend:{&cpu_trend,&ram_trend,&gpu_trend})while(trend->size()>40)trend->pop_front();
                SetWindowTextW(metrics,(L"CPU: "+number(last.cpu)+L"%     RAM: "+number(last.used)+L" / "+number(last.total)+L" GB").c_str());
                SetWindowTextW(gpu_label,last.gpu.c_str());
                if(page==15)refresh_processes();
                for(int n=0;n<4;++n)InvalidateRect(gauges[n],nullptr,FALSE);
                for(auto chart:dashboard_history)InvalidateRect(chart,nullptr,FALSE);update_tray();
            }else if(r.kind=="services"){
                int selected=selection(lists[6]),top=ListView_GetTopIndex(lists[6]);std::string selected_name;
                if(selected>=0 && static_cast<size_t>(selected)<service_rows.size())selected_name=service_rows[selected].name;
                bool same=service_rows.size()==r.services.size();
                if(same)for(size_t i=0;i<service_rows.size();++i)if(service_rows[i].name!=r.services[i].name){same=false;break;}
                SendMessageW(lists[6],WM_SETREDRAW,FALSE,0);unsigned editable=0;
                if(!same)ListView_DeleteAllItems(lists[6]);
                for(size_t i=0;i<r.services.size();++i){
                    const auto& value=r.services[i];editable+=value.eligible;
                    std::vector<std::wstring> cells={hr::wide(value.display),hr::wide(value.name),value.state==SERVICE_RUNNING?L"Running":value.state==SERVICE_STOPPED?L"Stopped":L"Pending",hr::wide(hr::service_mode(value.start,value.delayed)),value.eligible?L"Yes":L"Protected",hr::wide(value.path)};
                    if(!same)add(lists[6],cells);
                    else for(size_t column=0;column<cells.size();++column){
                        wchar_t current[4096]{};ListView_GetItemText(lists[6],static_cast<int>(i),static_cast<int>(column),current,4096);
                        if(cells[column]!=current)ListView_SetItemText(lists[6],static_cast<int>(i),static_cast<int>(column),const_cast<wchar_t*>(cells[column].c_str()));
                    }
                    if(value.name==selected_name)ListView_SetItemState(lists[6],static_cast<int>(i),LVIS_SELECTED,LVIS_SELECTED);
                }
                service_rows=std::move(r.services);
                if(!same && top>0 && top<static_cast<int>(service_rows.size()))ListView_EnsureVisible(lists[6],top,FALSE);
                SendMessageW(lists[6],WM_SETREDRAW,TRUE,0);RedrawWindow(lists[6],nullptr,nullptr,RDW_INVALIDATE|RDW_UPDATENOW);
                say(std::to_wstring(service_rows.size())+L" services listed; "+std::to_wstring(editable)+L" eligible.");
            }else if(r.kind=="drivers"){
                driver_rows=std::move(r.drivers);ListView_DeleteAllItems(lists[5]);ListView_DeleteAllItems(driver_offers);
                for(const auto& row:driver_rows.installed)add(lists[5],{hr::wide(row[0]),hr::wide(row[1]),hr::wide(row[2]),hr::wide(row[3])});
                for(const auto& offer:driver_rows.offers)add(driver_offers,{hr::wide(offer)});
                if(!driver_rows.update_error.empty())say(hr::wide("Update check unavailable: "+driver_rows.update_error));
                else say(r.text=="updates"?std::to_wstring(driver_rows.offers.size())+L" Windows Update driver offers.":L"Driver inventory loaded. Use Check drivers + updates to search offers.");
            }else if(r.kind=="autoruns"){
                autorun_rows=std::move(r.autoruns);SendMessageW(lists[3],WM_SETREDRAW,FALSE,0);ListView_DeleteAllItems(lists[3]);
                for(const auto& entry:autorun_rows)add(lists[3],{hr::wide(entry.name),hr::wide(entry.kind+" / "+entry.scope),entry.enabled?L"Enabled":L"Disabled",hr::wide(entry.risk),hr::wide(entry.command)});
                for(const auto& row:store.of("disabled-autorun")){auto decoded=hr::decode(row[1]);auto entry=hr::autorun_from_record(decoded.at(0));add(lists[3],{hr::wide(entry.name),hr::wide(entry.kind),L"Backup / disabled",L"Restore available",hr::wide(entry.command)});}
                for(const auto& row:store.of("disabled"))add(lists[3],{hr::wide(row[1]),L"Run backup",L"Backup / disabled",L"Legacy backup",L"Restore available"});
                SendMessageW(lists[3],WM_SETREDRAW,TRUE,0);InvalidateRect(lists[3],nullptr,FALSE);say(L"Startup inventory includes Run keys, Startup folders and scheduled tasks.");
            }else if(r.kind=="cache_scan"){
                cache_files=std::move(r.cache_files);show_storage_results();
            }else if(r.kind=="storage"){
                storage_result=std::move(r.storage);show_storage_results();
            }else if(r.kind=="cache_clean"||r.kind=="duplicate_clean"){
                store.log("Storage cleanup",std::to_string(r.cleanup.deleted)+" files; "+std::to_string(r.cleanup.bytes)+" logical bytes; "+std::to_string(r.cleanup.skipped)+" skipped");refresh_saved();
                SetWindowTextW(storage_summary,(std::to_wstring(r.cleanup.deleted)+L" files processed / "+number(r.cleanup.bytes/1048576.0)+L" MB. "+std::to_wstring(r.cleanup.skipped)+L" changed, locked or protected items skipped. Rescan to refresh results.").c_str());
            }else if(r.kind=="tool"||r.kind=="package"){
                auto report=r.command.output+"\nExit code: "+std::to_string(r.command.code)+(r.command.code==3010?" (restart required)":"");
                bool showing=true;if(r.kind=="tool"){system_reports[system_pending]=hr::wide(report);showing=system_selected==system_pending;if(showing)SetWindowTextW(tool_output,system_reports[system_pending].c_str());system_pending.clear();}else SetWindowTextW(package_output,hr::wide(report).c_str());store.log(r.text,"Exit "+std::to_string(r.command.code)+"; "+hr::utf8(r.command.log.wstring()));refresh_saved();say(hr::wide(r.text+(showing?" finished. Review the output.":" finished. Select that tool to view its report.")));
            }else if(r.kind=="installed"){
                installed=std::move(r.installed);filter_installed();say(std::to_wstring(installed.size())+L" registered desktop applications.");
            }else if(r.kind=="temp"){
                temp_files=std::move(r.temp);ListView_DeleteAllItems(lists[8]);unsigned long long bytes=0;
                for(const auto& file:temp_files){add(lists[8],{file.path.wstring(),number(file.size/1048576.0,2)});ListView_SetCheckState(lists[8],ListView_GetItemCount(lists[8])-1,TRUE);bytes+=file.size;}
                SetWindowTextW(temp_summary,(std::to_wstring(temp_files.size())+L" files / "+number(bytes/1048576.0)+L" MB eligible. Older than 24 hours; checked files will be permanently deleted.").c_str());
                say(L"Temp scan complete. Review checked files before cleaning.");
            }else if(r.kind=="clean"){
                store.log("Temp cleanup",std::to_string(r.cleanup.deleted)+" files; "+std::to_string(r.cleanup.bytes)+" bytes; "+std::to_string(r.cleanup.skipped)+" skipped");
                say(std::to_wstring(r.cleanup.deleted)+L" temporary files deleted, "+number(r.cleanup.bytes/1048576.0)+L" MB; "+std::to_wstring(r.cleanup.skipped)+L" skipped.");
                temp_files.clear();ListView_DeleteAllItems(lists[8]);refresh_saved();
            }else if(r.kind=="apps"){
                optional=std::move(r.apps);apps_loaded=true;ListView_DeleteAllItems(lists[10]);
                for(const auto& app:optional)add(lists[10],{hr::wide(app_title(app[0])),hr::wide(app_version(app[1]))});
                say(std::to_wstring(optional.size())+L" optional consumer apps available for removal.");
            }else if(r.kind=="debloat"){
                for(const auto& app:r.apps)store.log("Optional app removed",app[0]);
                refresh_saved();scan_apps();
                auto report=L"Debloat complete: "+std::to_wstring(r.apps.size())+L" optional apps removed. Windows suggestions reduced.";
                if(!r.text.empty())report+=L" Some apps were skipped: "+hr::wide(r.text);
                say(report);
                if(!r.text.empty())hr::app_dialog(window,L"Debloat results",report,false,light_theme);
            }else if(r.kind=="app_remove"){
                store.log("Optional app removed",r.record[0]);refresh_saved();scan_apps();
            }else if(r.kind=="app_restore"){
                auto found=std::find(store.rows.begin(),store.rows.end(),r.record);
                if(found!=store.rows.end())store.rows.erase(found);
                store.log("Optional app restored",r.record[2]);refresh_saved();scan_apps();
            }else if(r.kind=="screenshot"){
                store.log("Screenshot saved",r.text);refresh_saved();say(hr::wide("Screenshot saved: "+r.text));
            }else if(r.kind=="capture")save_benchmark(r.benchmark,r.game,r.mode,r.text,r.record);
        }
    }
    void add_tray(){
        NOTIFYICONDATAW data{};data.cbSize=sizeof data;data.hWnd=window;data.uID=1;data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP;data.uCallbackMessage=tray_message;data.hIcon=reinterpret_cast<HICON>(GetClassLongPtrW(window,GCLP_HICONSM));
        wcscpy_s(data.szTip,L"NovaTune");
        tray=Shell_NotifyIconW(NIM_ADD,&data)!=FALSE;
        if(!tray)say(L"Tray unavailable; keep this window open.");else update_tray();
    }
    void update_tray(){
        if(!tray)return;
        NOTIFYICONDATAW data{};data.cbSize=sizeof data;data.hWnd=window;data.uID=1;data.uFlags=NIF_TIP;
        auto tip=L"NovaTune | "+hr::wide(store.get("mode","Browsing"))+L" | CPU "+number(last.cpu)+L"% | RAM "+number(last.used)+L" GB";
        wcsncpy_s(data.szTip,tip.c_str(),_TRUNCATE);Shell_NotifyIconW(NIM_MODIFY,&data);
    }
    void tray_menu(){
        HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,OpenWindow,L"Open NovaTune");
        AppendMenuW(menu,MF_STRING|MF_GRAYED,0,(L"CPU "+number(last.cpu)+L"% | RAM "+number(last.used)+L" GB").c_str());
        for(size_t i=0;i<std::min<size_t>(3,last.processes.size());++i)AppendMenuW(menu,MF_STRING|MF_GRAYED,0,(last.processes[i].name+L"  "+number(last.processes[i].ram,0)+L" MB").c_str());
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,Gaming,L"Gaming");AppendMenuW(menu,MF_STRING,Browsing,L"Browsing");AppendMenuW(menu,MF_STRING,Creating,L"Creating");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING,replay->active()?ReplayStop:ReplayStart,replay->active()?L"Stop replay buffering":L"Start replay buffering");if(replay->active())AppendMenuW(menu,MF_STRING|(replay_saving?MF_GRAYED:0),ReplaySave,L"Save replay (Ctrl+Alt+F9)");if(microphone.active())AppendMenuW(menu,MF_STRING,MicStop,L"Stop microphone test");
        AppendMenuW(menu,MF_STRING,ToggleOverlay,L"Toggle performance HUD");AppendMenuW(menu,MF_STRING,Screenshot,L"Capture screenshot");AppendMenuW(menu,MF_STRING,recorder.active()?StopRecording:StartRecording,recorder.active()?L"Stop recording":L"Start recording");
        HMENU launches=CreatePopupMenu();std::vector<size_t> order;for(size_t i=0;i<games.size();++i)order.push_back(i);std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){return store.get("favorite.game."+hr::hex(hr::lower(games[a][2])))=="1"&&store.get("favorite.game."+hr::hex(hr::lower(games[b][2])))!="1";});for(size_t n=0;n<std::min<size_t>(12,order.size());++n){auto i=order[n];AppendMenuW(launches,MF_STRING,4000+static_cast<UINT_PTR>(n),hr::wide(games[i][1]).c_str());}if(order.empty())AppendMenuW(launches,MF_STRING|MF_GRAYED,0,L"Add games in the library first");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(launches),L"Launch game");
        HMENU favorites=CreatePopupMenu();std::vector<size_t> favorite_indices;unsigned count=0;for(size_t i=0;i<hub_tools.size()&&count<12;++i)if(store.get("favorite."+hub_tools[i].id)=="1"){AppendMenuW(favorites,MF_STRING,5000+static_cast<UINT_PTR>(count),hr::wide(hub_tools[i].title).c_str());favorite_indices.push_back(i);++count;}if(!count)AppendMenuW(favorites,MF_STRING|MF_GRAYED,0,L"Favorite a tool in Gamer tools first");AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(favorites),L"Favorite tools");
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING,Quit,L"Quit");
        POINT p;GetCursorPos(&p);SetForegroundWindow(window);
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr);DestroyMenu(menu);
        if(command>=4000&&command<4012&&static_cast<size_t>(command-4000)<order.size()){auto game=games[order[command-4000]];advanced_launch(game[2]);}
        else if(command>=5000&&command<5012&&static_cast<size_t>(command-5000)<favorite_indices.size()){auto tool=hub_tools[favorite_indices[command-5000]];ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);open_hub_tool(tool.id);}
        else if(command)action(command);
        PostMessageW(window,WM_NULL,0,0);
    }
    void register_keys(bool edited){
        std::vector<hr::Shortcut> requested;std::set<std::pair<unsigned,unsigned>> unique{{0x4003,'O'},{0x4003,'P'},{0x4003,'R'}};
        for(int i=0;i<3;++i){auto s=hr::shortcut(edited?hr::utf8(text(key_edit[i])):store.get(std::string("key.")+modes[i],std::string("Ctrl+Alt+")+modes[i][0]));if(!unique.insert({s.modifiers,s.key}).second)throw std::runtime_error("Each mode needs a different shortcut");requested.push_back(s);}
        for(int i=0;i<3;++i)UnregisterHotKey(window,i+1);
        bool success=true;
        for(int i=0;i<3;++i)if(!RegisterHotKey(window,i+1,requested[i].modifiers,requested[i].key))success=false;
        if(!success){
            for(int i=0;i<3;++i)UnregisterHotKey(window,i+1);
            if(edited)for(int i=0;i<3;++i){auto old=hr::shortcut(store.get(std::string("key.")+modes[i],std::string("Ctrl+Alt+")+modes[i][0]));RegisterHotKey(window,i+1,old.modifiers,old.key);}
            if(edited)throw std::runtime_error("A shortcut is reserved by another application; previous shortcuts retained where available");
            say(L"Global shortcuts unavailable. Edit them in Modes.");
        }
    }
    void switch_mode(const char* mode){if(boost_busy){say(L"Wait for Boost to finish before changing modes.");return;}
        hr::switch_mode(store,mode);refresh_saved();say(hr::wide(std::string(mode)+" mode active"));
        if(store.get("notifications","1")=="1"){std::wstring detail=L"Your selected power profile is active.";try{auto active=hr::active_plan();for(const auto& plan:power_plans)if(hr::lower(plan.guid)==hr::lower(active)){detail=L"Power plan: "+plan.name;break;}}catch(const std::exception&){}auto shortcut=store.get(std::string("key.")+mode,std::string("Ctrl+Alt+")+mode[0]);hr::app_mode_toast(window,hr::wide(mode),detail,hr::wide(shortcut),light_theme);}
    }
    void action(int command){
        if(advanced_action(command))return;
        if(command>=1100 && command<1200){selected_disk=command-1100;for(auto card:disk_cards)InvalidateRect(card,nullptr,FALSE);return;}

        switch(command){
        case PortableFolder:{wchar_t file[32768]{};auto size=GetModuleFileNameW(nullptr,file,32768);if(!size||size>=32768)throw std::runtime_error("Application folder unavailable");hr::shell(std::filesystem::path(file).parent_path().wstring());break;}
        case CenterLibrary:show_page(1);break;
        case CenterRefresh:refresh_center();break;
        case CenterLaunch:{auto path=center_selected();if(!std::filesystem::is_regular_file(hr::wide(path)))throw std::runtime_error("Game executable no longer exists");advanced_launch(path);refresh_center();break;}
        case CenterProfile:route_center(25);break;
        case CenterHud:select_center_game();show_page(23);break;
        case CenterReplay:select_center_game();show_page(24);break;
        case CenterTimeline:route_center(30);break;
        case HistoryLog:show_page(7);break;
        case CompareBefore:capture_comparison(false);break;
        case CompareAfter:capture_comparison(true);break;
        case CompareCancel:*comparison_cancel=true;say(L"Stopping measurement; previous report is preserved.");break;
        case CompareExport:export_comparison();break;
        case CompareOpen:{auto path=std::filesystem::path(hr::wide(store.get("compare.export")));if(path.parent_path()!=hr::data_dir()/L"reports"||path.extension()!=L".html"||!std::filesystem::is_regular_file(path))throw std::runtime_error("Export a PC report first");hr::shell(path.wstring());break;}
        case SetupScan:show_page(0);PostMessageW(window,WM_COMMAND,ScanSuggestions,0);break;
        case SetupStartup:show_page(3);break;
        case SetupDesktop:show_page(31);break;
        case SetupFinish:store.set("onboarding.complete","1");store.save();refresh_comparison();say(L"Setup finished. No settings were changed by finishing setup.");break;
        case InternetGo:if(network_busy){*network_cancel=true;say(L"Stopping after the current bounded network request...");}else start_network(false);break;
        case InternetInspect:if(!network_busy)start_network(true);break;
        case InternetDns:if(!network_busy){open_hub_tool("system.dns");run_hub(false);}break;
        case InternetRepair:if(!network_busy){open_hub_tool("system.winsock");run_hub(false);}break;
        case InternetSettings:hr::shell(L"ms-settings:network-status");break;
        case HubRun:run_hub(true);update_hub_actions();break;
        case HubCancel:*hub_cancel=true;hub_live.clear();update_hub_actions();say(L"Cancellation requested; an in-progress native query finishes first.");break;
        case HubSource:case HubTarget:{
            std::wstring chosen;if(command==HubTarget&&hub_selected=="capture-privacy"){
                if(recorder.active()||replay->active())throw std::runtime_error("Stop recording/replay before choosing its region");auto region=hr::app_region(window);if(region){auto text=std::to_wstring(region->left)+L","+std::to_wstring(region->top)+L","+std::to_wstring(region->right-region->left)+L","+std::to_wstring(region->bottom-region->top);SetWindowTextW(hub_second,text.c_str());}break;
            }
            if(command==HubSource&&hub_selected=="trim-clip")chosen=hr::app_file(window,L"Choose MP4 video",L".mp4",light_theme);
            else if(command==HubSource&&hub_selected=="crop-image")chosen=hr::app_file(window,L"Choose screenshot",L".png",light_theme);
            else if(command==HubSource&&(hub_selected=="favorite-game"||hub_selected=="deadzone"||hub_selected=="game-profile"||hub_selected=="profile-disable"||hub_selected=="background-sleep"))chosen=hr::app_file(window,L"Choose game executable",L".exe",light_theme);else chosen=hr::app_folder(window,L"Choose a scoped folder",light_theme);
            if(!chosen.empty()){if(hub_selected=="diagnostic-export")chosen=(std::filesystem::path(chosen)/L"NovaTune-diagnostics.txt").wstring();SetWindowTextW(command==HubSource?hub_first:hub_second,chosen.c_str());}break;
        }
        case HubFavorite:{int i=selection(lists[20]);if(i<0)throw std::runtime_error("Select a gamer tool");auto id=visible_hub.at(i).id;store.set("favorite."+id,store.get("favorite."+id)=="1"?"0":"1");store.save();filter_hub();break;}
        case InspectRecovery:refresh_recovery();describe_recovery();break;
        case RestoreSelected:{int i=selection(lists[21]);if(i<0||static_cast<size_t>(i)>=recovery_indices.size())throw std::runtime_error("Select one change");auto index=recovery_indices[i];auto row=store.rows.at(index);if(confirm(hr::wide("Restore this individual change?\n\n"+row[1]+" / "+row[2]+"\nOriginal: "+row[3]+"\nApplied: "+row[4]+"\n\nExternal conflicts and newer changes to this setting will be refused."))){hr::undo_at(store,index);refresh_recovery();refresh_saved();say(L"Selected change restored.");}break;}
        case ScanSuggestions:{
            if(suggestions_busy)break;suggestions_busy=true;EnableWindow(suggestion_scan,FALSE);EnableWindow(suggestion_fix,FALSE);SetWindowTextW(suggestion_note,L"SCANNING  /  Checking old user-temp files and background capture settings...");
            async("suggestions",[](Result& r){r.temp=hr::scan_temp();DWORD value=0,bytes=sizeof value;
                if(RegGetValueW(HKEY_CURRENT_USER,L"System\\GameConfigStore",L"GameDVR_Enabled",RRF_RT_REG_DWORD,nullptr,&value,&bytes)==ERROR_SUCCESS && value==1)r.mode="capture";
            });break;
        }
        case FixSuggestions:{
            if(suggestions_busy)break;std::vector<hr::FixItem> findings;unsigned long long bytes=0;for(const auto& file:suggestion_files)bytes+=file.size;
            if(!suggestion_files.empty())findings.push_back({"temp",L"Clean old temporary files",std::to_wstring(suggestion_files.size())+L" files / "+number(bytes/1048576.0,1)+L" MB older than 24 hours. Evidence: current file age and identity. Purpose: recover storage, not a proven FPS gain. Cost: permanent deletion, no Undo; preview in Temp cleaner first. Changed/locked files are skipped."});
            if(suggestion_capture)findings.push_back({"capture",L"Disable background game recording",L"Evidence: GameDVR_Enabled is explicitly 1 in this account's GameConfigStore. Windows Game DVR recording may stop, including background clips. Original value is backed up for Undo. This is not proof of an FPS gain; games may need reopening."});
            hr::app_fix_review(window,findings,[this](HWND review,const std::string& id){apply_suggestion(review,id);},light_theme);fix_review=nullptr;break;
        }
        case SleepSelected:{
            int i=selection(lists[15]);if(i<0)throw std::runtime_error("Select a background app");auto process=last.processes.at(i);
            if(sleeping.count(process.pid))throw std::runtime_error("This app is already sleeping");
            std::string reason;if(!hr::sleep_eligible(process,reason))throw std::runtime_error(reason);
            if(confirm(L"Suspend "+process.name+L" temporarily? Only choose an idle app. Its work pauses; RAM allocations remain. NovaTune's watchdog resumes it if NovaTune exits or crashes.")){
                sleeping[process.pid]=std::make_unique<hr::SleepSession>(process);say(L"App sleeping. Resume selected/all is available.");
            }break;
        }
        case ResumeSelected:{int i=selection(lists[15]);if(i<0)throw std::runtime_error("Select an app");sleeping.erase(last.processes.at(i).pid);say(L"Selected app resumed.");break;}
        case ResumeAll:sleeping.clear();say(L"All NovaTune-suspended apps resumed.");break;
        case StorageRoots:if(storage_busy)throw std::runtime_error("Wait for the scan");refresh_cache_roots();break;
        case StorageChoose:{
            if(storage_busy)throw std::runtime_error("Wait for the scan");auto path=hr::app_folder(window,L"Choose analysis folder",light_theme);if(!path.empty())SetWindowTextW(storage_path,path.c_str());break;
        }
        case StorageStop:*storage_cancel=true;say(L"Stopping the read-only scan...");break;
        case StorageScan:{
            if(storage_busy)throw std::runtime_error("A storage task is already running");
            int mode=static_cast<int>(SendMessageW(storage_mode,CB_GETCURSEL,0,0));storage_cancel=std::make_shared<std::atomic<bool>>(false);auto cancel=storage_cancel;
            if(mode==0){
                if(cache_preview){refresh_cache_roots();say(L"Choose locations and scan again.");break;}
                std::vector<hr::CacheTarget> selected;for(size_t i=0;i<cache_roots.size();++i)if(ListView_GetCheckState(lists[17],static_cast<int>(i)))selected.push_back(cache_roots[i]);
                if(selected.empty())throw std::runtime_error("Check cleanup locations first");storage_busy=true;
                async("cache_scan",[selected,cancel](Result& r){r.cache_files=hr::scan_caches(selected,*cancel);});
            }else{
                auto root=std::filesystem::path(text(storage_path));storage_busy=true;
                async("storage",[root,mode,cancel](Result& r){r.storage=hr::analyze_storage(root,mode==2,*cancel);});
            }
            say(L"Storage scan running. Stop cancels read-only analysis.");break;
        }
        case StorageClean:{
            if(storage_busy)throw std::runtime_error("Wait for the scan");int mode=static_cast<int>(SendMessageW(storage_mode,CB_GETCURSEL,0,0));
            if(mode==0){
                if(!cache_preview)throw std::runtime_error("Scan checked cache locations first");std::vector<hr::StorageFile> selected;
                for(size_t i=0;i<cache_files.size();++i)if(ListView_GetCheckState(lists[17],static_cast<int>(i)))selected.push_back(cache_files[i]);
                if(selected.empty())throw std::runtime_error("Check scanned files first");
                if(confirm(L"Permanently remove "+std::to_wstring(selected.size())+L" checked cache files? Browser cookies sign you out. Passwords are not selected. Changed, locked and protected files are skipped.")){
                    storage_busy=true;async("cache_clean",[selected](Result& r){r.cleanup=hr::clean_caches(selected);});
                }
            }else if(mode==2){
                std::vector<std::pair<hr::StorageFile,hr::StorageFile>> selected;
                for(size_t i=0;i<duplicate_rows.size();++i)if(ListView_GetCheckState(lists[17],static_cast<int>(i))){
                    auto pair=duplicate_rows[i];if(!pair.second)throw std::runtime_error("The KEEP copy cannot be removed");auto& group=storage_result.duplicates.at(pair.first);
                    selected.push_back({group.files.at(pair.second),group.files.front()});
                }
                if(selected.empty())throw std::runtime_error("Check duplicate copies to recycle");
                if(confirm(L"Send "+std::to_wstring(selected.size())+L" verified duplicate copies to Recycle Bin? One original per group is kept. Revalidation can skip changed or busy files.")){
                    auto owner=window;storage_busy=true;async("duplicate_clean",[selected,owner](Result& r){r.cleanup=hr::recycle_duplicates(owner,selected);});
                }
            }else throw std::runtime_error("Folder breakdown is read-only. Use Reveal to inspect a file.");break;
        }
        case RevealFile:{
            int selected=selection(lists[17]);if(selected<0)throw std::runtime_error("Select a result");int mode=static_cast<int>(SendMessageW(storage_mode,CB_GETCURSEL,0,0));
            if(mode==0&&cache_preview)hr::reveal(cache_files.at(selected).path);
            else if(mode==2){auto pair=duplicate_rows.at(selected);hr::reveal(storage_result.duplicates.at(pair.first).files.at(pair.second).path);}
            else if(mode==1){size_t n=static_cast<size_t>(selected);hr::reveal(n<storage_result.folders.size()?storage_result.folders[n].path:storage_result.largest.at(n-storage_result.folders.size()).path);}
            break;
        }
        case ToolInspect:start_tool(true);describe_tool();break;case ToolRun:start_tool(false);describe_tool();break;
        case OpenLogs:hr::shell(hr::data_dir().wstring());break;
        case WingetSearch:start_package("search");break;case WingetUpdates:start_package("updates");break;
        case WingetInstall:start_package("install");break;case WingetUpgrade:start_package("upgrade");break;case WingetUninstall:start_package("uninstall");break;
        case WingetSchedule:if(confirm(L"Create a daily 12:00 task that silently upgrades only the exact IDs in the box, accepting their package/source agreements? It runs while this user is logged on.")){hr::schedule_winget(store,hr::utf8(text(package_query)),true);refresh_saved();say(L"Selected-package updater registered.");}break;
        case WingetUnschedule:hr::schedule_winget(store,"",false);refresh_saved();say(L"NovaTune package updater removed.");break;
        case RefreshInstalled:scan_installed();break;
        case OpenWindowsApps:hr::shell(L"ms-settings:appsfeatures");break;
        case UninstallSelected:{
            int i=selection(lists[16]);if(i<0 || static_cast<size_t>(i)>=visible_installed.size())throw std::runtime_error("Select an installed app");
            auto app=visible_installed[i];
            if(confirm(hr::wide("Open the uninstaller for "+app.name+"?\nPublisher: "+app.publisher+"\nVersion: "+app.version+"\n\nThe publisher handles removal. Reinstall the app to restore it."))){
                hr::uninstall_app(window,app);store.log("Uninstaller opened",app.name);refresh_saved();say(L"Uninstaller opened. Refresh this list after it finishes.");
            }break;
        }
        case OneClickDebloat:{
            if(app_busy)throw std::runtime_error("Wait for the current app scan or operation");
            if(!apps_loaded){scan_apps();say(L"Scanning apps first. Review the results, then use One-click debloat.");break;}
            if(optional.empty()){hr::reduce_suggestions(store);refresh_saved();say(L"No listed optional apps are installed. Windows suggestions reduced.");break;}
            std::wstring review=L"Remove these optional apps for this account and reduce Windows suggestions?\n\n";
            for(const auto& app:optional)review+=hr::wide(app_title(app[0]))+L"\n";
            review+=L"\nRestoration may need a Microsoft Store download. Windows components, security and gaming services are excluded.";
            if(confirm(review)){
                hr::reduce_suggestions(store);
                auto apps=optional;
                for(const auto& app:apps)store.rows.push_back({"journal","appx",app[0],app[3],app[2],app[1]});
                store.save();app_busy=true;say(L"Debloating selected optional consumer apps...");
                async("debloat",[apps](Result& r){
                    for(const auto& app:apps){
                        try{hr::remove_optional_package(app);r.apps.push_back(app);}
                        catch(const std::exception& e){r.text+=app[0]+": "+e.what()+"; ";}
                    }
                });
            }break;
        }
        case 1004:{if(boost_busy)break;boost_busy=true;boost_phase=L"SCANNING";auto mode=store.get("mode","Browsing");auto snapshot=store;async("boost-scan",[snapshot,mode](Result& r){r.boost=std::make_shared<hr::BoostPlan>(hr::review_boost(snapshot,mode));});say(L"Scanning a boost plan for the selected mode...");break;}
        case ListenInput:if(input.listening())input.cancel_capture();else input.begin_capture();break;
        case 1200:select_design_button();break;
        case StartStress:{
            auto value=hr::utf8(text(stress_seconds));size_t used=0;unsigned seconds=static_cast<unsigned>(std::stoul(value,&used));if(used!=value.size())throw std::runtime_error("Enter a duration in seconds");
            stress.start(seconds,SendMessageW(stress_cpu,BM_GETCHECK,0,0)==BST_CHECKED,SendMessageW(stress_ram,BM_GETCHECK,0,0)==BST_CHECKED,SendMessageW(stress_gpu,BM_GETCHECK,0,0)==BST_CHECKED);
            stress_saved=false;say(L"System stress test running. Stop is always available.");break;
        }
        case StopStress:stress.stop();update_stress();say(L"Stress test stopped.");break;
        case SaveMouse:{
            int speed=static_cast<int>(SendMessageW(mouse_speed,TBM_GETPOS,0,0));
            store.set("mouse.absolute.scale",SendMessageW(mouse_absolute,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0");hr::save_mouse_settings(store,speed,SendMessageW(mouse_accel,BM_GETCHECK,0,0)==BST_CHECKED);load_mouse_editor();refresh_saved();say(L"Mouse speed applied and verified. Press Pause to stop VM cursor scaling.");break;
        }
        case MousePreset:hr::save_mouse_settings(store,10,false);SendMessageW(mouse_speed,TBM_SETPOS,TRUE,10);SendMessageW(mouse_accel,BM_SETCHECK,BST_UNCHECKED,0);load_mouse_editor();refresh_saved();say(L"Windows confirmed speed 10 and acceleration off.");break;
        case OpenMouse:hr::shell(L"main.cpl");break;
        case AddMapping:{
            auto device=hr::utf8(text(remap_device)),target=hr::utf8(text(target_device));
            auto source=source_buttons.at(static_cast<size_t>(SendMessageW(remap_button,CB_GETCURSEL,0,0))).second;
            auto output=target_buttons.at(static_cast<size_t>(SendMessageW(target_button,CB_GETCURSEL,0,0))).second;
            if(device==target && source==output)throw std::runtime_error("Choose a different output button");
            auto id=std::to_string(source);
            store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const hr::Row& r){return r[0]=="mapping" && r[1]==device && r[2]==id;}),store.rows.end());
            store.rows.push_back({"mapping",device,id,target,std::to_string(output)});store.save();refresh_mappings();if(input.enabled())configure_input();say(input.enabled()?L"Mapping updated in the active profile.":L"Mapping added. Click Start remapping to activate it.");break;
        }
        case RemoveMapping:{
            int i=selection(lists[12]);auto rows=store.of("mapping");if(i<0)throw std::runtime_error("Select a mapping");
            auto row=rows.at(i);auto found=std::find(store.rows.begin(),store.rows.end(),row);if(found!=store.rows.end())store.rows.erase(found);
            store.save();refresh_mappings();configure_input();break;
        }
        case StartRemapping:{if(store.of("mapping").empty()&&SendMessageW(stick_mouse,BM_GETCHECK,0,0)!=BST_CHECKED)throw std::runtime_error("Add a mapping first");auto prior=SendMessageW(remap_enabled,BM_GETCHECK,0,0);SendMessageW(remap_enabled,BM_SETCHECK,BST_CHECKED,0);try{action(SaveMapping);}catch(...){SendMessageW(remap_enabled,BM_SETCHECK,prior,0);throw;}break;}
        case StopRemapping:input.cancel_capture();SendMessageW(remap_enabled,BM_SETCHECK,BST_UNCHECKED,0);store.set("remap.enabled","0");configure_input();store.log("Input remapping stopped","Outputs released");say(L"Remapping stopped. Original buttons pass through.");break;
        case SaveMapping:{
            bool enabled=SendMessageW(remap_enabled,BM_GETCHECK,0,0)==BST_CHECKED;
            store.set("remap.game","");store.set("remap.enabled",enabled?"1":"0");store.set("remap.game_only","0");store.set("remap.stick",SendMessageW(stick_mouse,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0");
            store.log("Input profile saved",enabled?"Enabled":"Disabled");configure_input();refresh_saved();say(enabled?L"System-wide remapping active. Stop or Pause restores original input.":L"Profile saved. Click Start remapping to apply it across Windows.");break;
        }
        case RefreshDisks:refresh_disks();break;
        case OpenDiskCleanup:hr::disk_cleanup();break;
        case OpenOptimizeDrives:{wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);hr::shell((std::filesystem::path(system)/L"dfrgui.exe").wstring());break;}
        case AnalyzeDisk:case OptimizeDisk:{
            int i=selected_disk;if(i<0 || static_cast<size_t>(i)>=disk_rows.size())throw std::runtime_error("Select a drive");
            if(command==AnalyzeDisk || confirm(L"Let Windows optimize this drive using its media-appropriate method? The Windows console may request administrator approval.")){
                hr::optimize_drive(window,disk_rows.at(i).root,command==AnalyzeDisk);say(L"Windows drive operation opened.");
            }break;
        }
        case Screenshot:{auto options=capture_options();async("screenshot",[options](Result& r){r.text=hr::utf8(hr::screenshot(options).wstring());});say(L"Capturing "+capture_scope()+L"...");break;}
        case StartRecording:if(replay->active())throw std::runtime_error("Stop replay buffering before starting normal recording");if(recorder.active())action(StopRecording);else{recorder.start(capture_options());say(L"Screen recording started: MP4, 720p30, no audio.");}break;
        case StopRecording:if(recorder.file().empty())throw std::runtime_error("No recording has been started");recorder.stop();if(!recorder.error().empty())throw std::runtime_error(recorder.error());store.log("Screen recording saved",hr::utf8(recorder.file().wstring()));refresh_saved();say(L"Recording saved: "+recorder.file().wstring());break;
        case OpenCaptures:hr::shell(hr::capture_folder().wstring());break;
        case OverlaySave:{auto settings=edited_overlay();hr::save_overlay(store,settings);overlay_settings=settings;place_overlay();InvalidateRect(overlay_preview,nullptr,FALSE);say(L"HUD layout saved for this Windows account.");break;}
        case OverlayDefaults:overlay_settings=hr::OverlaySettings{};hr::save_overlay(store,overlay_settings);load_overlay_editor();place_overlay();say(L"Default HUD layout restored.");break;
        case GalleryRefresh:scan_gallery();break;
        case GalleryOpen:{auto path=selected_capture();check_capture(path);hr::shell(path.wstring());break;}
        case GallerySave:{auto path=selected_capture();check_capture(path);auto tags=hr::utf8(text(gallery_tags));if(tags.size()>800)throw std::runtime_error("Tags are too long");auto index=SendMessageW(gallery_game,CB_GETCURSEL,0,0);if(index<0||static_cast<size_t>(index)>=gallery_games.size())throw std::runtime_error("Select a game association");if(tags.empty()&&gallery_games[index].empty()){auto key=gallery_key(path);store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const hr::Row& row){return row[0]=="setting"&&row[1]==key;}),store.rows.end());}else store.set(gallery_key(path),hr::encode({{tags,gallery_games[index]}}));store.save();filter_gallery();say(L"Capture tags and game association saved locally.");break;}
        case GalleryTrim:{auto path=selected_capture();check_capture(path);if(hr::lower(hr::utf8(path.extension().wstring()))!=".mp4")throw std::runtime_error("Select an MP4 video");open_hub_tool("trim-clip");SetWindowTextW(hub_first,path.c_str());SetFocus(hub_second);break;}
        case GalleryCrop:{if(gallery_busy)break;auto path=selected_capture();check_capture(path);if(hr::lower(hr::utf8(path.extension().wstring()))!=".png")throw std::runtime_error("Select a PNG image");HANDLE locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(locked==INVALID_HANDLE_VALUE)throw std::runtime_error("Close programs writing this image before cropping");auto source_lock=std::shared_ptr<void>(locked,[](void* h){CloseHandle(h);});auto crop=hr::app_crop_region(window,path,light_theme);if(!crop)break;auto meta=hr::encode({gallery_metadata(path)});gallery_busy=true;EnableWindow(controls[22][1],FALSE);EnableWindow(controls[22][5],FALSE);async("gallery-crop",[path,crop,meta,source_lock](Result& r){r.text=hr::utf8(hr::crop_screenshot(path,*crop).wstring());r.mode=meta;});break;}

        case TrimRam:{
            int i=selection(lists[15]);auto process=i>=0?last.processes.at(i):hr::own_process();
            if(confirm(L"Trim the physical working set of "+process.name+L"? Allocations remain and may page back in. This can add stutter; use Close for apps you no longer need.")){
                auto released=hr::trim_ram(process);store.log("RAM working set trimmed",hr::utf8(process.name)+": "+hr::utf8(number(released))+" MB moved out of working set");
                refresh_saved();say(number(released)+L" MB trimmed from the working set. This is temporary.");
            }break;
        }
        case ScanTemp:if(!temp_busy){temp_busy=true;say(L"Scanning user temp files older than 24 hours...");async("temp",[](Result& r){r.temp=hr::scan_temp();});}break;
        case CleanTemp:{
            if(temp_busy)throw std::runtime_error("Wait for the active cleanup task");
            std::vector<hr::TempFile> selected;
            for(size_t i=0;i<temp_files.size();++i)if(ListView_GetCheckState(lists[8],static_cast<int>(i)))selected.push_back(temp_files[i]);
            if(selected.empty())throw std::runtime_error("Scan and check temporary files first");
            if(confirm(L"Permanently delete "+std::to_wstring(selected.size())+L" checked temp files? Files changed since the scan, links and locked files are skipped.")){
                temp_busy=true;async("clean",[selected=std::move(selected)](Result& r){r.cleanup=hr::clean_temp(selected);});
            }break;
        }
        case ApplyFPS:if(confirm(L"Enable Game Mode and disable background Game DVR recording for your account, then apply the Gaming power profile? Undo restores one setting at a time.")){hr::apply_fps_profile(store);refresh_saved();say(L"FPS profile applied. Benchmark the same scene to measure the result.");}break;
        case ReduceSuggestions:if(confirm(L"Disable Windows content suggestions and advertising ID for this account? Each setting can be restored with Undo.")){hr::reduce_suggestions(store);refresh_saved();say(L"Windows suggestions reduced.");}break;
        case ScanApps:scan_apps();break;
        case RemoveApp:{
            if(app_busy)throw std::runtime_error("Wait for the active app operation");
            int i=selection(lists[10]);if(i<0)throw std::runtime_error("Select an optional app");
            auto app=optional.at(i);
            if(confirm(hr::wide("Remove "+app_title(app[0])+" for this account? Windows may delete its files; restoring it can require a Microsoft Store download."))){
                store.rows.push_back({"journal","appx",app[0],app[3],app[2],app[1]});store.save();
                app_busy=true;async("app_remove",[app](Result& r){hr::remove_optional_package(app);r.record=app;});
                say(L"Removing selected optional app...");
            }break;
        }
        case ReinstallApp:{
            if(app_busy)throw std::runtime_error("Wait for the active app operation");
            auto records=store.of("journal");
            auto found=std::find_if(records.rbegin(),records.rend(),[](const hr::Row& r){return r[1]=="appx";});
            if(found==records.rend())throw std::runtime_error("No removed app to restore");
            auto row=*found;app_busy=true;
            async("app_restore",[row](Result& r){hr::restore_optional_app(row);r.record=row;});break;
        }
        case ToggleOverlay:toggle_overlay();break;
        case OpenWindow:ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);break;
        case Quit:
            if(comparison_busy||network_busy||boost_busy||tool_busy||storage_busy||hub_busy||profile_busy||mod_busy||maintenance_busy||automatic_backup_busy||replay_saving||suggestions_busy){say(L"An operation is running. Let it finish or stop the read-only scan; closing the window keeps it in the tray.");break;}
            sleeping.clear();
            stress.stop();update_stress();
            if(recording || temp_busy || app_busy){say(L"Wait for the active capture, cleanup or package task to finish before quitting.");break;}
            if(recorder.active()){recorder.stop();if(!recorder.error().empty())throw std::runtime_error(recorder.error());}
            if(overlay){live.stop();DestroyWindow(overlay);overlay=nullptr;}
            input.configure({},false,true,"",false);
            DestroyWindow(window);break;
        case Gaming:switch_mode("Gaming");break;
        case Browsing:switch_mode("Browsing");break;
        case Creating:switch_mode("Creating");break;
        case Refresh:if(page==3)refresh_startup();else if(page==6)scan_services();else monitor();break;
        case CloseApp:{
            int i=selection(lists[15]);if(i<0)throw std::runtime_error("Select an app");
            auto p=last.processes.at(i);
            if(confirm(L"Ask "+p.name+L" to close? Save your work first.")){hr::close_app(p);say(L"Close request sent.");}
            break;
        }
        case AddGame:{
            auto path=hr::app_file(window,L"Add a game",L".exe",light_theme);if(path.empty())break;
            for(const auto& r:store.of("game"))if(hr::lower(r[2])==hr::lower(hr::utf8(path)))throw std::runtime_error("Game already added");
            store.rows.push_back({"game",hr::utf8(std::filesystem::path(path).stem().wstring()),hr::utf8(path)});
            store.log("Game added",hr::utf8(path));refresh_saved();break;
        }
        case RemoveGame:{
            auto path=selected_game();if(path.empty())throw std::runtime_error("Select a game");
            if(hr::lower(sessions.active_game())==hr::lower(path))sessions.stop("Game removed from library");auto key=hr::hex(hr::lower(path));store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const hr::Row& row){return row[0]=="setting"&&(row[1]=="game.profile."+key||row[1]=="session.profile."+key||row[1]=="session.sleep."+key);}),store.rows.end());
            store.rows.erase(std::remove_if(store.rows.begin(),store.rows.end(),[&](const hr::Row& r){return r[0]=="game" && r[2]==path;}),store.rows.end());
            store.log("Game removed",path);refresh_saved();break;
        }
        case LaunchGame:{
            auto path=selected_game();if(path.empty())throw std::runtime_error("Select a game");
            if(!std::filesystem::is_regular_file(std::filesystem::path(hr::wide(path))))throw std::runtime_error("Game executable no longer exists");
            advanced_launch(path);break;
        }
        case SaveModes:{
            register_keys(true);
            for(int i=0;i<3;++i){int n=static_cast<int>(SendMessageW(plan_combo[i],CB_GETCURSEL,0,0));store.set(std::string("plan.")+modes[i],n>0?power_plans.at(n-1).guid:"");store.set(std::string("key.")+modes[i],hr::utf8(text(key_edit[i])));}
            store.log("Profiles saved","Power plans and shortcuts");refresh_saved();say(L"Plans and shortcuts saved.");break;
        }
        case Undo:{
            if(app_busy)throw std::runtime_error("Wait for the active package task");
            if(page==11){size_t index=store.rows.size();for(size_t n=store.rows.size();n-->0;)if(store.rows[n][0]=="journal"&&store.rows[n][1]=="mouse"){index=n;break;}if(index==store.rows.size())throw std::runtime_error("No mouse setting remains to undo");hr::undo_at(store,index);load_mouse_editor();refresh_saved();say(L"Original Windows mouse settings restored and verified.");break;}
            auto records=store.of("journal");
            if(!records.empty() && records.back()[1]=="appx"){
                auto row=records.back();app_busy=true;async("app_restore",[row](Result& r){hr::restore_optional_app(row);r.record=row;});
            }else{hr::undo(store);refresh_saved();refresh_startup();if(page==6)scan_services();say(L"Last Windows change restored.");}
            break;
        }
        case DisableStartup:{
            int i=selection(lists[3]);if(i<0||static_cast<size_t>(i)>=autorun_rows.size())throw std::runtime_error("Select an enabled startup entry");
            auto entry=autorun_rows[i];if(!entry.editable)throw std::runtime_error("This startup entry is protected");
            if(confirm(hr::wide("Disable future startup/triggers for "+entry.name+"?\nRunning applications are not stopped. Risk: "+entry.risk))){
                hr::disable_autorun(store,entry);refresh_startup();refresh_saved();
            }break;
        }
        case RestoreStartup:{
            int i=selection(lists[3]);if(i<0||static_cast<size_t>(i)<autorun_rows.size())throw std::runtime_error("Select a disabled startup backup");
            auto rows=store.of("disabled-autorun");size_t n=static_cast<size_t>(i)-autorun_rows.size();
            if(n>=rows.size()){auto legacy=store.of("disabled");if(n-rows.size()>=legacy.size())throw std::runtime_error("Select a disabled backup");hr::restore_saved_startup(store,legacy[n-rows.size()]);}
            else hr::restore_autorun(store,rows[n]);refresh_startup();refresh_saved();break;
        }
        case ServiceAuto:case ServiceManual:case ServiceDisabled:{
            int i=selection(lists[6]);if(i<0)throw std::runtime_error("Select a service");auto s=service_rows.at(i);
            if(!s.eligible)throw std::runtime_error("This service is protected");
            DWORD target=command==ServiceAuto?SERVICE_AUTO_START:command==ServiceManual?SERVICE_DEMAND_START:SERVICE_DISABLED;
            if(confirm(hr::wide("Set "+s.display+" to "+hr::service_mode(target)+" startup? This does not stop the running service. Administrator rights may be required."))){
                hr::change_service(store,s,target);refresh_saved();scan_services();
            }break;
        }
        case CheckDrivers:scan_drivers(true);break;
        case OptionalUpdates:hr::shell(L"ms-settings:windowsupdate-optionalupdates");break;
        case Vendor:{
            int i=selection(lists[5]);if(i<0 || static_cast<size_t>(i)>=driver_rows.installed.size())throw std::runtime_error("Select a display driver");
            auto link=hr::vendor(driver_rows.installed[i][0]+" "+driver_rows.installed[i][1]);if(link.empty())throw std::runtime_error("No supported vendor link for this device");
            hr::shell(hr::wide(link));break;
        }
        case PickPresentMon:{
            auto path=hr::app_file(window,L"Choose PresentMon",L".exe",light_theme);if(!path.empty()){SetWindowTextW(presentmon,path.c_str());dirty=true;}break;
        }
        case SaveSettings:{
            auto path=text(presentmon);
            if(!path.empty() && (!std::filesystem::is_regular_file(std::filesystem::path(path)) || hr::lower(hr::utf8(std::filesystem::path(path).extension().wstring()))!=".exe"))throw std::runtime_error("Choose an existing PresentMon executable");
            store.set("presentmon",hr::utf8(path));store.set("notifications",SendMessageW(notifications,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0");store.set("hidden",SendMessageW(hidden,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0");
            store.set("glass",SendMessageW(glass_toggle,BM_GETCHECK,0,0)==BST_CHECKED?"1":"0");apply_glass();
            store.log("Settings saved","Preferences");dirty=false;refresh_saved();say(L"Settings saved.");break;
        }
        case OpenData:hr::shell(hr::data_dir().wstring());break;
        case Import:{
            auto path=hr::app_file(window,L"Import frame capture",L".csv",light_theme);if(path.empty())break;
            auto game=selected_game();std::ifstream stream{std::filesystem::path(path)};
            if(!stream)throw std::runtime_error("Unable to open CSV");
            auto b=hr::csv(stream,game.empty()?"":hr::utf8(std::filesystem::path(hr::wide(game)).filename().wstring()));
            save_benchmark(b,game.empty()?(b.process.empty()?"Imported":b.process):game,store.get("mode","Browsing"),hr::utf8(path));break;
        }
        case Compare:{
            int i=selection(lists[4]);if(i<0 || static_cast<size_t>(i+1)>=benchmarks.size())throw std::runtime_error("Select a result with an older result below it");
            const auto& after=benchmarks[i];const auto& before=benchmarks[i+1];
            if(after[2]!=before[2] || after[4]!=before[4])throw std::runtime_error("Compare the same game and frame metric");
            auto message=L"Change vs previous: average "+number(std::stod(after[6])-std::stod(before[6]))+L" FPS, 1% low "+number(std::stod(after[7])-std::stod(before[7]))+L" FPS, P95 "+number(std::stod(after[8])-std::stod(before[8]),2)+L" ms. Lower P95 is better.";
            SetWindowTextW(summary,message.c_str());break;
        }
        case Capture:{
            if(recording)throw std::runtime_error("A benchmark is already running");
            auto game=selected_game();if(game.empty())throw std::runtime_error("Select a game first");
            auto value=hr::utf8(text(duration));size_t used=0;unsigned seconds=static_cast<unsigned>(std::stoul(value,&used));
            if(used!=value.size() || seconds<10 || seconds>300)throw std::runtime_error("Duration must be 10-300 seconds");
            if(store.get("presentmon").empty())throw std::runtime_error("Choose PresentMon.exe in Settings and save first");
            hr::Store snapshot=store;
            hr::Records environment;for(const auto& row:hr::pc_info())if(row[0]!="Device")environment.push_back(row);hr::Row metadata{"captured",store.get("benchmark.notes."+hr::hex(hr::lower(game))),hr::encode(environment),driver_rows.installed.empty()?"":hr::encode(driver_rows.installed)};
            auto mode=store.get("mode","Browsing");recording=true;say(L"Recording... play the same scene. Results appear when capture finishes.");
            async("capture",[snapshot=std::move(snapshot),game,mode,seconds,metadata](Result& r)mutable{r.record=metadata;
                r.text=hr::capture(snapshot,game,seconds);r.game=game;r.mode=mode;
                std::ifstream in{std::filesystem::path(hr::wide(r.text))};if(!in)throw std::runtime_error("Capture CSV was not created");
                r.benchmark=hr::csv(in,hr::utf8(std::filesystem::path(hr::wide(game)).filename().wstring()));
            });break;
        }
        }
    }
    void smoke(){
        ++smoke_tick;
        if(smoke_tick<=tab_count)show_page(static_cast<int>(smoke_tick-1));
        if(smoke_tick==tab_count+1){
                                                                               
            if(store.get("plan.Gaming").empty())switch_mode("Gaming");
            if(store.get("plan.Browsing").empty())switch_mode("Browsing");
        }
        if(smoke_tick>=tab_count+4 && !monitoring && !scanning_services && !scanning_drivers && !app_busy){
            std::ostringstream report;
            report<<"NovaTune native Windows smoke test\n"
                  <<"Pages visited: "<<tab_count<<"\nCPU: "<<last.cpu<<"\nRAM GB: "<<last.used<<" / "<<last.total
                  <<"\nProcesses: "<<last.processes.size()<<"\nPower plans: "<<power_plans.size()
                  <<"\nStartup entries: "<<startups.size()<<"\nServices: "<<service_rows.size()
                  <<"\nDisplay drivers: "<<driver_rows.installed.size()<<"\nTray: "<<tray
                  <<"\nErrors: "<<smoke_errors.size()<<"\n";
            for(const auto& error:smoke_errors)report<<error<<"\n";
            hr::atomic_write(smoke_report,report.str());KillTimer(window,3);
            show_page(0);say(L"VM smoke test complete; report saved.");
        }
    }
};
LRESULT CALLBACK overlay_procedure(HWND h,UINT msg,WPARAM w,LPARAM l){
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(h,GWLP_USERDATA));
    if(msg==WM_NCCREATE){app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));}
    if(msg==WM_NCHITTEST)return HTTRANSPARENT;
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT && app){PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT rect;GetClientRect(h,&rect);app->draw_overlay(dc,rect);EndPaint(h,&paint);return 0;}
    return DefWindowProcW(h,msg,w,l);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l){
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    try{
        if(message==WM_NCCREATE){
            app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app));
        }
        if(!app)return DefWindowProcW(window,message,w,l);
        if(message==app->taskbar_created && app->taskbar_created){app->add_tray();if(app->desktop)app->desktop->explorer_changed();return 0;}
        if(message==WM_APP+70){app->show_page(31);ShowWindow(window,SW_RESTORE);SetForegroundWindow(window);return 0;}
        switch(message){
        case WM_CREATE:app->initialize(window);return 0;
        case WM_SIZE:if(w!=SIZE_MINIMIZED){app->layout();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);}return 0;
        case WM_EXITSIZEMOVE:app->layout();RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);return 0;
        case WM_DISPLAYCHANGE:app->place_overlay();if(app->desktop)app->desktop->explorer_changed();return 0;
        case WM_ACTIVATE:{
            COLORREF caption=bg,caption_text=ink;
            DwmSetWindowAttribute(window,35,&caption,sizeof caption);
            DwmSetWindowAttribute(window,36,&caption_text,sizeof caption_text);
            InvalidateRect(window,nullptr,FALSE);break;
        }
        case WM_GETMINMAXINFO:{auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={app->px(1060),app->px(760)};return 0;}
        case WM_CTLCOLOREDIT:case WM_CTLCOLORLISTBOX:{
            auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,ink);SetBkColor(dc,panel);return reinterpret_cast<LRESULT>(app->panel_brush);
        }
        case WM_CTLCOLORBTN:case WM_CTLCOLORSTATIC:{
            auto dc=reinterpret_cast<HDC>(w);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,reinterpret_cast<HWND>(l)==app->mode_label?neon:ink);return reinterpret_cast<LRESULT>(app->background);
        }
        case WM_MEASUREITEM:{
            auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);
            if(item->CtlType==ODT_COMBOBOX){item->itemHeight=app->px(24);return TRUE;}
            if(item->CtlType==ODT_LISTBOX && item->CtlID==nav_id){
                bool category=item->itemID<app->nav_visible.size() && navigation[app->nav_visible[item->itemID]].page<0;
                item->itemHeight=app->px(category?23:30);return TRUE;
            }
            break;
        }
        case WM_DRAWITEM:{
            auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);SavedDrawingState saved_drawing(item->hDC);
            if(app->draw_network(*item))return TRUE;
            if(app->draw_advanced_item(*item))return TRUE;
            if(item->CtlType==ODT_STATIC && item->CtlID==1006){
                FillRect(item->hDC,&item->rcItem,app->panel_brush);
                auto old=SelectObject(item->hDC,app->brand_font);SetBkMode(item->hDC,TRANSPARENT);
                SetTextCharacterExtra(item->hDC,app->px(1));
                SIZE first{},second{};GetTextExtentPoint32W(item->hDC,L"Nova",4,&first);GetTextExtentPoint32W(item->hDC,L"Tune",4,&second);
                int y=item->rcItem.top+(item->rcItem.bottom-item->rcItem.top-first.cy)/2;
                SetTextColor(item->hDC,ink);TextOutW(item->hDC,item->rcItem.left,y,L"Nova",4);
                SetTextColor(item->hDC,light_theme?RGB(0,116,40):RGB(26,242,44));TextOutW(item->hDC,item->rcItem.left+first.cx,y,L"Tune",4);
                SetTextCharacterExtra(item->hDC,0);SelectObject(item->hDC,old);return TRUE;
            }
            if(item->CtlType==ODT_STATIC && item->CtlID==1005){
                FillRect(item->hDC,&item->rcItem,app->background);HBRUSH brush=CreateSolidBrush(light_theme?RGB(219,239,235):RGB(28,28,31));HPEN pen=CreatePen(PS_SOLID,1,RGB(39,65,64));
                auto old=SelectObject(item->hDC,brush);auto oldpen=SelectObject(item->hDC,pen);
                RoundRect(item->hDC,0,0,item->rcItem.right,item->rcItem.bottom,16,16);
                SelectObject(item->hDC,old);SelectObject(item->hDC,oldpen);DeleteObject(brush);DeleteObject(pen);
                SetTextColor(item->hDC,neon);SetBkMode(item->hDC,TRANSPARENT);SelectObject(item->hDC,app->font);
                auto value=text(item->hwndItem);RECT rect=item->rcItem;DrawTextW(item->hDC,value.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;
            }
            if(item->CtlType==ODT_COMBOBOX){
                FillRect(item->hDC,&item->rcItem,app->panel_brush);
                SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,ink);SelectObject(item->hDC,app->font);
                std::wstring value;
                if(item->itemID!=static_cast<UINT>(-1)){
                    auto n=SendMessageW(item->hwndItem,CB_GETLBTEXTLEN,item->itemID,0);
                    if(n>=0){value.resize(static_cast<size_t>(n)+1);SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(&value[0]));value.resize(static_cast<size_t>(n));}
                }
                RECT rect=item->rcItem;rect.left+=8;DrawTextW(item->hDC,value.c_str(),-1,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE);return TRUE;
            }
            if(item->CtlType==ODT_STATIC && ((item->CtlID>=1000 && item->CtlID<=1004) || (item->CtlID>=1100 && item->CtlID<1200) || item->CtlID==1200 || (item->CtlID>=1300 && item->CtlID<=1302))){
                int width=item->rcItem.right-item->rcItem.left,height=item->rcItem.bottom-item->rcItem.top;
                if(width>0 && height>0){
                    HDC memory=CreateCompatibleDC(item->hDC);HBITMAP bitmap=CreateCompatibleBitmap(item->hDC,width,height);
                    auto old=SelectObject(memory,bitmap);auto copy=*item;copy.hDC=memory;copy.rcItem={0,0,width,height};if(item->CtlID>=1300)app->draw_history(copy);else if(item->CtlID==1200)app->draw_device(copy);else if(item->CtlID>=1100)app->draw_disk(copy);else app->draw_gauge(copy);
                    BitBlt(item->hDC,item->rcItem.left,item->rcItem.top,width,height,memory,0,0,SRCCOPY);
                    SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);
                }return TRUE;
            }
            if(item->CtlType==ODT_LISTBOX){
                if(item->itemID==static_cast<UINT>(-1) || item->itemID>=app->nav_visible.size())return TRUE;
                const auto source=app->nav_visible[item->itemID];const auto& entry=navigation[source];bool selected=(item->itemState&ODS_SELECTED) && entry.page>=0;
                FillRect(item->hDC,&item->rcItem,app->panel_brush);
                if(selected){
                    HBRUSH brush=CreateSolidBrush(light_theme?RGB(0,101,94):RGB(36,36,40));auto oldbrush=SelectObject(item->hDC,brush);auto oldpen=SelectObject(item->hDC,GetStockObject(NULL_PEN));
                    RoundRect(item->hDC,item->rcItem.left+4,item->rcItem.top+2,item->rcItem.right-4,item->rcItem.bottom-2,10,10);
                    SelectObject(item->hDC,oldbrush);SelectObject(item->hDC,oldpen);DeleteObject(brush);
                }
                SetBkMode(item->hDC,TRANSPARENT);SelectObject(item->hDC,entry.page<0?app->font:app->sidebar_font);
                SetTextColor(item->hDC,entry.page<0?RGB(119,128,139):selected?RGB(250,250,252):ink);
                RECT rect=item->rcItem;
                if(entry.page<0){size_t end=source+1;while(end<std::size(navigation)&&navigation[end].page>=0)++end;bool active=false;for(size_t n=source+1;n<end;++n)if(navigation[n].page==app->page)active=true;auto shade=active?neon:RGB(119,128,139);SetTextColor(item->hDC,shade);int x=app->px(16),y=(rect.top+rect.bottom)/2;auto xx=[&](int n){return x+app->px(n);};auto yy=[&](int n){return y+app->px(n);};auto pen=CreatePen(PS_SOLID,app->px(1),shade);auto previous=SelectObject(item->hDC,pen),brush=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));std::wstring group=entry.label;
                    if(group==L"HOME"){POINT roof[]={{xx(-7),yy(0)},{xx(0),yy(-6)},{xx(7),yy(0)}};Polyline(item->hDC,roof,3);Rectangle(item->hDC,xx(-5),yy(0),xx(5),yy(6));}
                    else if(group==L"GAMING"){RoundRect(item->hDC,xx(-7),yy(-4),xx(8),yy(6),app->px(5),app->px(5));MoveToEx(item->hDC,xx(-4),yy(0),nullptr);LineTo(item->hDC,xx(0),yy(0));MoveToEx(item->hDC,xx(-2),yy(-2),nullptr);LineTo(item->hDC,xx(-2),yy(3));Ellipse(item->hDC,xx(3),yy(-1),xx(6),yy(2));}
                    else if(group==L"INPUT & DEVICES"){RoundRect(item->hDC,xx(-4),yy(-7),xx(5),yy(8),app->px(6),app->px(6));MoveToEx(item->hDC,xx(0),yy(-5),nullptr);LineTo(item->hDC,xx(0),yy(-1));}
                    else if(group==L"SYSTEM"){Rectangle(item->hDC,xx(-4),yy(-4),xx(5),yy(5));for(int n=-3;n<=3;n+=3){MoveToEx(item->hDC,xx(-7),yy(n),nullptr);LineTo(item->hDC,xx(-4),yy(n));MoveToEx(item->hDC,xx(5),yy(n),nullptr);LineTo(item->hDC,xx(8),yy(n));}}
                    else if(group==L"STORAGE & APPS"){RoundRect(item->hDC,xx(-7),yy(-6),xx(8),yy(0),app->px(3),app->px(3));RoundRect(item->hDC,xx(-7),yy(1),xx(8),yy(7),app->px(3),app->px(3));}
                    else{Rectangle(item->hDC,xx(-7),yy(-6),xx(8),yy(4));MoveToEx(item->hDC,xx(-3),yy(7),nullptr);LineTo(item->hDC,xx(4),yy(7));POINT play[]={{xx(-2),yy(-3)},{xx(3),yy(-1)},{xx(-2),yy(2)},{xx(-2),yy(-3)}};Polyline(item->hDC,play,4);}
                    SelectObject(item->hDC,brush);SelectObject(item->hDC,previous);DeleteObject(pen);
                }
                if(entry.page>=0){
                    int x=16,y=(rect.top+rect.bottom)/2;COLORREF color=selected?neon:light_theme?RGB(37,115,58):RGB(74,134,83);
                    auto icon=[&](int thickness,COLORREF shade){
                        HPEN pen=CreatePen(PS_SOLID,thickness,shade);auto old=SelectObject(item->hDC,pen);auto brush=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));
                        if(entry.page==0 || entry.page==4 || entry.page==9){POINT path[]={{x-5,y+4},{x-1,y+4},{x+1,y-5},{x+4,y+3},{x+7,y-2}};Polyline(item->hDC,path,5);}
                        else if(entry.page==1){RoundRect(item->hDC,x-7,y-5,x+8,y+6,5,5);MoveToEx(item->hDC,x-4,y,nullptr);LineTo(item->hDC,x+1,y);MoveToEx(item->hDC,x-2,y-2,nullptr);LineTo(item->hDC,x-2,y+3);Ellipse(item->hDC,x+3,y-2,x+5,y);}
                        else if(entry.page==11){RoundRect(item->hDC,x-5,y-8,x+6,y+8,6,6);MoveToEx(item->hDC,x,y-7,nullptr);LineTo(item->hDC,x,y);}
                        else if(entry.page==13){Ellipse(item->hDC,x-7,y-7,x+8,y+8);Ellipse(item->hDC,x-2,y-2,x+3,y+3);}
                        else if(entry.page==14){RoundRect(item->hDC,x-7,y-5,x+8,y+6,3,3);Ellipse(item->hDC,x-3,y-3,x+4,y+4);}
                        else {RoundRect(item->hDC,x-7,y-6,x+8,y+7,3,3);MoveToEx(item->hDC,x-3,y-2,nullptr);LineTo(item->hDC,x+4,y-2);MoveToEx(item->hDC,x-3,y+2,nullptr);LineTo(item->hDC,x+4,y+2);}
                        SelectObject(item->hDC,old);SelectObject(item->hDC,brush);DeleteObject(pen);
                    };
                    if(selected)icon(5,light_theme?RGB(0,80,35):RGB(21,67,29));icon(1,color);
                }
                rect.left+=app->px(36);
                DrawTextW(item->hDC,entry.label,-1,&rect,DT_SINGLELINE|DT_VCENTER|DT_NOPREFIX);return TRUE;
            }
            if(item->CtlID==1400){app->draw_overlay(item->hDC,item->rcItem,true);return TRUE;}
            if(item->CtlType==ODT_BUTTON){
                ButtonPaintBuffer paint(*item);item=&paint.local;
                bool disabled=(item->itemState&ODS_DISABLED)!=0;auto color=disabled?RGB(34,36,41):(item->itemState&ODS_SELECTED)?RGB(36,38,43):RGB(28,28,31);
                FillRect(item->hDC,&item->rcItem,app->background);
                                                                                    
                auto bounds=item->rcItem;InflateRect(&bounds,-2,-2);
                HPEN halo=CreatePen(PS_SOLID,4,disabled?(light_theme?RGB(222,226,232):bg):light_theme?RGB(195,224,203):RGB(15,47,23));
                auto halo_pen=SelectObject(item->hDC,halo);auto hollow=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));
                RoundRect(item->hDC,bounds.left,bounds.top,bounds.right,bounds.bottom,12,12);
                SelectObject(item->hDC,halo_pen);SelectObject(item->hDC,hollow);DeleteObject(halo);
                HBRUSH brush=CreateSolidBrush(color);HPEN pen=CreatePen(PS_SOLID,1,disabled?RGB(68,73,82):(item->itemState&(ODS_FOCUS|ODS_SELECTED))?neon:RGB(40,102,51));
                auto oldpen=SelectObject(item->hDC,pen);auto oldbrush=SelectObject(item->hDC,brush);
                RoundRect(item->hDC,bounds.left,bounds.top,bounds.right,bounds.bottom,12,12);
                SelectObject(item->hDC,oldpen);SelectObject(item->hDC,oldbrush);DeleteObject(pen);DeleteObject(brush);
                SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,disabled?RGB(148,156,168):RGB(245,245,247));SelectObject(item->hDC,app->font);
                auto label=text(item->hwndItem);RECT rect=item->rcItem;
                if(item->CtlID==FixSuggestions && app->suggestions_scanned){
                    label=L"Review fixes";rect.right-=app->px(31);int diameter=app->px(23),x=item->rcItem.right-diameter-app->px(9),y=(item->rcItem.bottom-diameter)/2;
                    SetTextColor(item->hDC,RGB(26,242,44));
                    RECT count_rect{x,y,x+diameter,y+diameter};auto count=std::to_wstring(app->suggestion_count());DrawTextW(item->hDC,count.c_str(),-1,&count_rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);SetTextColor(item->hDC,RGB(245,245,247));
                }
                DrawTextW(item->hDC,label.c_str(),-1,&rect,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);return TRUE;
            }
            break;
        }
        case WM_ERASEBKGND:{RECT r;GetClientRect(window,&r);FillRect(reinterpret_cast<HDC>(w),&r,app->background);return 1;}
        case WM_NOTIFY:{
            auto info=reinterpret_cast<NMHDR*>(l);
            if(info->hwndFrom==app->lists[21]&&info->code==LVN_ITEMCHANGED){app->describe_recovery();return 0;}
            if(info->hwndFrom==app->lists[27]&&info->code==LVN_ITEMCHANGED){app->select_device_profile();return 0;}
            if(info->hwndFrom==app->lists[29]&&info->code==LVN_ITEMCHANGED){app->clear_mod_review();return 0;}
            if(info->hwndFrom==app->lists[22]&&info->code==LVN_ITEMCHANGED){app->describe_capture();return 0;}
            if(info->hwndFrom==app->lists[22]&&info->code==NM_DBLCLK){app->action(GalleryOpen);return 0;}
            if(info->hwndFrom==app->lists[20]&&info->code==LVN_ITEMCHANGED){app->describe_hub();return 0;}
            if(info->hwndFrom==app->lists[1] && info->code==LVN_GETINFOTIPW){auto tip=reinterpret_cast<NMLVGETINFOTIPW*>(l);if(tip->iItem>=0&&static_cast<size_t>(tip->iItem)<app->games.size())lstrcpynW(tip->pszText,hr::wide(app->games[tip->iItem][2]).c_str(),tip->cchTextMax);return 0;}
            if(info->hwndFrom==app->lists[18]&&info->code==LVN_ITEMCHANGED){app->describe_tool();return 0;}
            if(info->hwndFrom==app->lists[17]&&info->code==LVN_ITEMCHANGING&&SendMessageW(app->storage_mode,CB_GETCURSEL,0,0)==2){
                auto item=reinterpret_cast<NMLISTVIEW*>(l);if(item->iItem>=0&&static_cast<size_t>(item->iItem)<app->duplicate_rows.size()&&app->duplicate_rows[item->iItem].second==0&&((item->uNewState&LVIS_STATEIMAGEMASK)==INDEXTOSTATEIMAGEMASK(2)))return TRUE;
            }
            if(info->hwndFrom==app->mouse_speed && info->code==NM_CUSTOMDRAW){
                auto draw=reinterpret_cast<NMCUSTOMDRAW*>(l);
                if(draw->dwDrawStage==CDDS_PREPAINT)return CDRF_NOTIFYITEMDRAW;
                if(draw->dwDrawStage==CDDS_ITEMPREPAINT && draw->dwItemSpec==TBCD_CHANNEL){
                    RECT rect=draw->rc;rect.top=(rect.top+rect.bottom)/2-2;rect.bottom=rect.top+4;
                    FillRect(draw->hdc,&rect,app->panel_brush);int speed=static_cast<int>(SendMessageW(app->mouse_speed,TBM_GETPOS,0,0));
                    rect.right=rect.left+(rect.right-rect.left)*(speed-1)/19;HBRUSH brush=CreateSolidBrush(neon);FillRect(draw->hdc,&rect,brush);DeleteObject(brush);return CDRF_SKIPDEFAULT;
                }
            }
            break;
        }
        case WM_COMMAND:
            if(LOWORD(w)==2000&&HIWORD(w)==EN_CHANGE){if(app->global_search&&!app->navigation_filtering)app->filter_navigation();return 0;}
            if(LOWORD(w)==2001&&HIWORD(w)==CBN_SELCHANGE){app->refresh_center();return 0;}
            if((LOWORD(w)==2003&&HIWORD(w)==CBN_SELCHANGE)||(LOWORD(w)==2004&&HIWORD(w)==BN_CLICKED)){app->save_accessibility();return 0;}
            if(LOWORD(w)==470&&HIWORD(w)==CBN_SELCHANGE&&!app->replay->active()){const unsigned lengths[]={30,60,120};auto chosen=SendMessageW(app->replay_seconds,CB_GETCURSEL,0,0);if(chosen>=0&&chosen<3){app->store.set("replay.seconds",std::to_string(lengths[chosen]));app->store.save();}return 0;}
            if(LOWORD(w)==480&&HIWORD(w)==CBN_SELCHANGE){app->load_game_profile();return 0;}
            if(LOWORD(w)==474&&HIWORD(w)==CBN_SELCHANGE){app->fill_display_modes();return 0;}
            if((LOWORD(w)==1502||LOWORD(w)==1503)&&HIWORD(w)==STN_CLICKED){app->timeline_click(reinterpret_cast<HWND>(l));return 0;}
            if(LOWORD(w)==440&&HIWORD(w)==EN_CHANGE){app->filter_gallery();return 0;}
            if(LOWORD(w)==430&&HIWORD(w)==EN_CHANGE){app->filter_hub();return 0;}
            if(LOWORD(w)==nav_id && HIWORD(w)==LBN_SELCHANGE){
                int selected=static_cast<int>(SendMessageW(app->nav,LB_GETCURSEL,0,0));
                if(!app->navigation_filtering && selected>=0 && static_cast<size_t>(selected)<app->nav_visible.size()){
                    while(static_cast<size_t>(selected)<app->nav_visible.size() && navigation[app->nav_visible[selected]].page<0)++selected;
                    if(static_cast<size_t>(selected)<app->nav_visible.size())app->show_page(navigation[app->nav_visible[selected]].page);
                }
            }
            else if(LOWORD(w)==412&&HIWORD(w)==CBN_SELCHANGE){if(app->storage_busy){SendMessageW(app->storage_mode,CB_SETCURSEL,0,0);break;}if(SendMessageW(app->storage_mode,CB_GETCURSEL,0,0)==0)app->refresh_cache_roots();else app->show_storage_results();}
            else if(LOWORD(w)==413&&HIWORD(w)==CBN_SELCHANGE)app->filter_tools();
            else if(LOWORD(w)==434&&HIWORD(w)==CBN_SELCHANGE)app->filter_hub();
            else if(LOWORD(w)==411 && HIWORD(w)==EN_CHANGE)app->filter_installed();
            else if(LOWORD(w)==409 && HIWORD(w)==CBN_SELCHANGE)app->show_benchmark();
            else if((LOWORD(w)==400 || LOWORD(w)==402) && HIWORD(w)==CBN_SELCHANGE){bool source=LOWORD(w)==400;if(source){app->selected_source=0;app->source_flash=0;}app->populate_buttons(source,!source);InvalidateRect(app->device_view,nullptr,FALSE);}
            else if(HIWORD(w)==BN_CLICKED)app->action(LOWORD(w));
            return 0;
        case WM_HOTKEY:if(w==7){app->action(ReplaySave);return 0;}if(w==5){app->action(Screenshot);return 0;}if(w==6){app->action(StartRecording);return 0;}if(w==4){app->toggle_overlay();return 0;}if(w>=1 && w<=3)app->switch_mode(modes[w-1]);return 0;
        case WM_TIMER:if(w==1)app->drain();else if(w==2)app->monitor();else if(w==3)app->smoke();else if(w==4){if(app->store.get("motion.reduced")!="1")++app->animation;if(app->page==32&&app->network_busy)InvalidateRect(app->network_go,nullptr,FALSE);if(app->page==0)InvalidateRect(app->gauges[4],nullptr,FALSE);}return 0;
        case tray_message:if(l==WM_LBUTTONDBLCLK)app->action(OpenWindow);else if(l==WM_RBUTTONUP || l==WM_CONTEXTMENU)app->tray_menu();return 0;
        case WM_CLOSE:if(app->tray){if(app->microphone.active())app->microphone.stop();ShowWindow(window,SW_HIDE);return 0;}app->action(Quit);return 0;
        case WM_DESTROY:{
            app->desktop.reset();
            *app->replay_cancel=true;*app->mod_cancel=true;*app->maintenance_cancel=true;app->replay->stop();app->microphone.stop();
            app->mailbox->alive=false;
            for(int i=1;i<=7;++i)UnregisterHotKey(window,i);
            for(int i=1;i<=4;++i)KillTimer(window,i);
            NOTIFYICONDATAW data{};data.cbSize=sizeof data;data.hWnd=window;data.uID=1;Shell_NotifyIconW(NIM_DELETE,&data);
            PostQuitMessage(0);return 0;
        }
        }
    }catch(const std::exception& e){
        if(message==WM_CREATE){hr::app_dialog(window,L"NovaTune startup",hr::wide(e.what()),false,light_theme);return -1;}
        if(app && app->status)app->failure(e.what());
        else {hr::app_dialog(window,L"NovaTune",hr::wide(e.what()),false,light_theme);return message==WM_CREATE?-1:0;}
    }
    return DefWindowProcW(window,message,w,l);
}
int self_test(const std::filesystem::path& report){
    std::ostringstream out;unsigned failures=0;
    auto test=[&](const char* name,const std::function<void()>& fn){try{fn();out<<"PASS "<<name<<"\n";}catch(const std::exception& e){++failures;out<<"FAIL "<<name<<": "<<e.what()<<"\n";}};
    test("monitoring",[]{auto s=hr::sample();if(s.total<=0 || s.processes.empty())throw std::runtime_error("No memory/process data");});
    test("power plans",[]{if(hr::plans().empty() || hr::active_plan().empty())throw std::runtime_error("No active power plan");});
    test("startup inventory",[]{hr::startup();});
    test("service inventory",[]{if(hr::services().empty())throw std::runtime_error("No services");});
    test("driver inventory",[]{hr::drivers(false);});
    test("settings roundtrip",[&]{
        auto path=report;path+=L".settings-test";
        hr::Store store(path);store.set("test","UTF-8 roundtrip");store.save();
        hr::Store loaded(path);if(loaded.get("test")!="UTF-8 roundtrip")throw std::runtime_error("Settings differ");
        std::filesystem::remove(path);
    });
    out<<"Failures: "<<failures<<"\n";hr::atomic_write(report,out.str());return failures?1:0;
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int show){
    try{
        int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
        std::wstring option=argc>1?argv[1]:L"",argument=argc>2?argv[2]:L"";
        if(option==L"--desktop-shell-guard"){int result=hr::desktop_shell_guard(argc,argv);LocalFree(argv);return result;}
        if(option==L"--settings-worker"){int result=hr::settings_worker(argc,argv);LocalFree(argv);return result;}
        if(option==L"--maintenance-run"){LocalFree(argv);hr::Store scheduled;if(!hr::maintenance_plan(scheduled).background)return 0;std::atomic<bool> cancel{false};hr::run_maintenance(scheduled,cancel);return 0;}
        if(option==L"--priority-worker"){int result=hr::priority_worker(argc,argv);LocalFree(argv);return result;}
        if(option==L"--memory-service"){LocalFree(argv);return hr::run_memory_service();}
        if(option==L"--sleep-worker"){int result=hr::sleep_worker(argc,argv);LocalFree(argv);return result;}
        if(option==L"--winget-scheduled"){LocalFree(argv);auto log=hr::data_dir()/L"logs"/(L"scheduled-"+std::to_wstring(GetTickCount64())+L".log");return static_cast<int>(hr::winget("upgrade",hr::utf8(argument),log).code);}
        LocalFree(argv);
        if(option==L"--self-test"){
            auto report=argument.empty()?hr::data_dir()/L"self-test.txt":std::filesystem::path(argument);
            return self_test(report);
        }
        HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\Headroom.Native.v2");
        if(!mutex)throw std::runtime_error(hr::error());
        bool exists=GetLastError()==ERROR_ALREADY_EXISTS;
        if(exists){HWND previous=FindWindowW(class_name,nullptr);if(previous){ShowWindow(previous,SW_RESTORE);SetForegroundWindow(previous);}CloseHandle(mutex);return 0;}
        INITCOMMONCONTROLSEX controls{sizeof controls,ICC_LISTVIEW_CLASSES|ICC_STANDARD_CLASSES|ICC_BAR_CLASSES};InitCommonControlsEx(&controls);
        App app(option==L"--smoke-test"?std::filesystem::path(argument):std::filesystem::path());
        WNDCLASSEXW cls{};cls.cbSize=sizeof cls;cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=native_icon();cls.hIconSm=cls.hIcon;cls.lpszClassName=class_name;
        if(!RegisterClassExW(&cls))throw std::runtime_error(hr::error());
        HWND window=CreateWindowExW(WS_EX_CONTROLPARENT,class_name,L"NovaTune",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,1200,830,nullptr,nullptr,instance,&app);
        if(!window)throw std::runtime_error("Unable to create NovaTune window");
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
        int width=std::min<int>(app.px(1200),work.right-work.left),height=std::min<int>(app.px(830),work.bottom-work.top);
        SetWindowPos(window,nullptr,work.left+(work.right-work.left-width)/2,work.top+(work.bottom-work.top-height)/2,width,height,SWP_NOZORDER);
        ShowWindow(window,app.store.get("hidden","0")=="1" && app.tray && option!=L"--smoke-test"?SW_HIDE:show);UpdateWindow(window);
        MSG message;
        while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        CloseHandle(mutex);DestroyIcon(cls.hIcon);return static_cast<int>(message.wParam);
    }catch(const std::exception& e){hr::app_dialog(nullptr,L"NovaTune startup",hr::wide(e.what()),false,light_theme);return 1;}
}
