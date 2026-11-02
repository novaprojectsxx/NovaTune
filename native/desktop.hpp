#pragma once
#include "windows.hpp"
#include <memory>
#include <array>
#include <functional>
namespace hr {
struct DesktopPin {std::string title,path;};
struct SnapZone {std::string name;int x=0,y=0,w=500,h=1000;};
struct DesktopScene {
    std::string name="Default",display,video,notes="Your desktop. Your rules.";
    int accent=0,opacity=242,icon_size=48,dock_edge=0,wall_mode=1,fps=24,edge_mode=0,edge_width=4,columns=3;
    int dock_style=0,hover_animation=0;unsigned custom_accent=RGB(26,242,44),custom_background=RGB(25,25,31),custom_text=RGB(236,240,247);
    bool custom_colors=false,replace_taskbar=true,replace_switcher=true,auto_hide=false;
    int music_x=500,music_y=780,music_width=320,music_height=80,skin=0;
    int launcher_style=0,switcher_style=0,launcher_opacity=242,switcher_opacity=242,corner_size=16,corner_dwell=650;
    bool music=false,corners=false;std::array<int,4> corner_actions{},corner_triggers{};std::vector<std::string> hidden_apps;
    int widget_x=750,widget_y=30;unsigned widgets=31;
    bool dock=true,board=true,wallpaper=false,launcher=true,switcher=true,edges=false,snap=false,interactive=true,start_menu=true;
    double latitude=0,longitude=0;std::string location;
    std::vector<DesktopPin> pins;
    std::vector<SnapZone> zones={{"Left",0,0,500,1000},{"Right",500,0,500,1000},{"Top left",0,0,500,500},{"Top right",500,0,500,500}};
};
std::string desktop_path_key(const std::string& path);
int desktop_shell_guard(int argc,wchar_t** argv);
void validate_desktop_scene(const DesktopScene& scene);
std::string encode_desktop_scene(const DesktopScene& scene);
DesktopScene decode_desktop_scene(const std::string& record);
DesktopScene desktop_preset(int preset);
RECT desktop_zone(const SnapZone& zone,const RECT& work);
std::vector<std::pair<std::string,RECT>> desktop_displays();
struct DesktopWindow {HWND handle=nullptr;DWORD pid=0;unsigned long long created=0;std::string title,path;bool minimized=false;};
std::vector<DesktopWindow> desktop_windows(HWND main_window=nullptr);
void desktop_launch(const std::string& path);
class DesktopStudio {
    struct Impl;std::unique_ptr<Impl> impl;
public:
    DesktopStudio(HWND owner,Store& store);
    ~DesktopStudio();
    HWND create_editor();
    void theme(bool light);
    void tick(const Sample& sample,const Records& games,bool gaming);
    void explorer_changed();
    void enable(const DesktopScene& scene);
    void stop();
    bool active() const;
    void preview(const DesktopScene& scene);
    void launcher();
    void switcher();
    void snap(HWND window,size_t zone);
    void undo_snap();
    std::string status() const;
    DesktopScene configuration() const;
};
}
