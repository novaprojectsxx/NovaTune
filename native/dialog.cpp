#include "dialog.hpp"
#include "scrollbars.hpp"
#include "windows.hpp"
#include <commctrl.h>
#include <dwmapi.h>
#include <shlobj.h>
#include <algorithm>
#include <filesystem>
#include <vector>
namespace hr {
namespace {
struct Dialog {
    HWND owner=nullptr,window=nullptr,body=nullptr,path=nullptr,list=nullptr,drive=nullptr;
    HFONT font=nullptr,heading=nullptr;
    HBRUSH background=nullptr,panel=nullptr;
    COLORREF bg,fg,muted,field;
    bool confirm=false,file=false,folder_mode=false,light=false,done=false,accepted=false;
    std::wstring title,message,extension,result;
    std::filesystem::path folder;
    std::vector<std::filesystem::directory_entry> entries;
    unsigned remaining=0;
    int width=740,height=440;
    Dialog(bool theme):light(theme){
        bg=light?RGB(246,247,249):RGB(13,13,15);fg=light?RGB(28,32,40):RGB(241,242,245);
        muted=light?RGB(90,98,110):RGB(157,165,177);field=light?RGB(255,255,255):RGB(25,25,28);
        background=CreateSolidBrush(bg);panel=CreateSolidBrush(field);
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        heading=CreateFontW(-24,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
    }
    ~Dialog(){DeleteObject(background);DeleteObject(panel);DeleteObject(font);DeleteObject(heading);}
    HWND make(const wchar_t* cls,const wchar_t* text,DWORD style,int id,int x,int y,int w,int h){
        if(wcscmp(cls,WC_COMBOBOXW)==0)style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
        auto control=CreateWindowExW(0,cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        style_scrollbars(control,light);
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return control;
    }
    std::wstring read(HWND control){
        int n=GetWindowTextLengthW(control);std::wstring value(n+1,L'\0');GetWindowTextW(control,&value[0],n+1);value.resize(n);return value;
    }
    void fill(){
        entries.clear();ListView_DeleteAllItems(list);
        std::error_code error;
        std::vector<std::filesystem::directory_entry> directories,files;
        for(std::filesystem::directory_iterator it(folder,std::filesystem::directory_options::skip_permission_denied,error),end;!error && it!=end;it.increment(error)){
            std::error_code ec;
            if(it->is_directory(ec))directories.push_back(*it);
            else if(!folder_mode && it->is_regular_file(ec) && lower(utf8(it->path().extension().wstring()))==lower(utf8(extension)))files.push_back(*it);
        }
        auto sort=[](const auto& a,const auto& b){return lower(utf8(a.path().filename().wstring()))<lower(utf8(b.path().filename().wstring()));};
        std::sort(directories.begin(),directories.end(),sort);std::sort(files.begin(),files.end(),sort);
        entries=std::move(directories);entries.insert(entries.end(),files.begin(),files.end());
        for(size_t i=0;i<entries.size();++i){
            auto name=entries[i].path().filename().wstring();LVITEMW item{};item.mask=LVIF_TEXT;item.iItem=static_cast<int>(i);item.pszText=&name[0];ListView_InsertItem(list,&item);
            std::error_code ec;auto type=entries[i].is_directory(ec)?L"Folder":L"File";
            ListView_SetItemText(list,static_cast<int>(i),1,const_cast<wchar_t*>(type));
        }
        SetWindowTextW(path,folder.c_str());
    }
    void open(bool navigate=false){
        auto value=read(path);std::filesystem::path candidate(value);
        if(!candidate.is_absolute())candidate=folder/candidate;
        std::error_code ec;
        if(std::filesystem::is_directory(candidate,ec)){
            if(folder_mode&&!navigate){result=candidate.wstring();accepted=true;DestroyWindow(window);return;}
            folder=candidate;fill();return;
        }
        int selected=ListView_GetNextItem(list,-1,LVNI_SELECTED);
        if(!std::filesystem::is_regular_file(candidate,ec) && selected>=0 && static_cast<size_t>(selected)<entries.size())candidate=entries[selected].path();
        if(std::filesystem::is_directory(candidate,ec)){folder=candidate;fill();return;}
        if(std::filesystem::is_regular_file(candidate,ec) && lower(utf8(candidate.extension().wstring()))==lower(utf8(extension))){
            result=candidate.wstring();accepted=true;DestroyWindow(window);
        }else{
            message=L"Choose an existing "+extension+L" file, or paste its full path.";InvalidateRect(window,nullptr,FALSE);
        }
    }
    void create(){
        BOOL dark=light?FALSE:TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof dark);
        int corner=2;DwmSetWindowAttribute(window,33,&corner,sizeof corner);
        COLORREF no_border=0xFFFFFFFE;DwmSetWindowAttribute(window,34,&no_border,sizeof no_border);
        if(file){
            drive=make(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,20,24,72,92,220);
            wchar_t roots[512]{};GetLogicalDriveStringsW(512,roots);
            for(auto root=roots;*root;root+=wcslen(root)+1)SendMessageW(drive,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(root));
            if(SendMessageW(drive,CB_GETCOUNT,0,0)>0)SendMessageW(drive,CB_SETCURSEL,0,0);
            make(L"BUTTON",L"Up",BS_OWNERDRAW|WS_TABSTOP,21,126,72,64,32);
            path=make(L"EDIT",L"",WS_BORDER|WS_TABSTOP|ES_AUTOHSCROLL,22,202,72,width-330,32);
            make(L"BUTTON",L"Go",BS_OWNERDRAW|WS_TABSTOP,23,width-116,72,90,32);
            list=make(WC_LISTVIEWW,L"",LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|WS_TABSTOP,24,24,126,width-48,height-232);
            ListView_SetExtendedListViewStyle(list,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);ListView_SetBkColor(list,field);ListView_SetTextBkColor(list,field);ListView_SetTextColor(list,fg);
            SetWindowSubclass(ListView_GetHeader(list),header,1,reinterpret_cast<DWORD_PTR>(this));
            LVCOLUMNW c{};c.mask=LVCF_TEXT|LVCF_WIDTH;c.pszText=const_cast<wchar_t*>(L"Name");c.cx=width-200;ListView_InsertColumn(list,0,&c);c.pszText=const_cast<wchar_t*>(L"Type");c.cx=100;ListView_InsertColumn(list,1,&c);
            PWSTR value=nullptr;if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&value))){folder=value;CoTaskMemFree(value);}else folder=L"C:\\";
            fill();
        }else{
            body=make(L"EDIT",message.c_str(),ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL,25,28,84,width-56,height-170);
        }
        make(L"BUTTON",file?(folder_mode?L"Choose folder":L"Open"):confirm?L"Continue":L"OK",BS_OWNERDRAW|WS_TABSTOP,IDOK,width-156,height-62,128,36);
        if(file||confirm)make(L"BUTTON",L"Cancel",BS_OWNERDRAW|WS_TABSTOP,IDCANCEL,width-296,height-62,128,36);
        if(remaining){SetWindowTextW(GetDlgItem(window,IDOK),L"Keep settings");SetWindowTextW(GetDlgItem(window,IDCANCEL),L"Revert");SetTimer(window,31,1000,nullptr);}
        SetFocus(file?path:GetDlgItem(window,confirm?IDCANCEL:IDOK));
    }
    static LRESULT CALLBACK header(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
        auto self=reinterpret_cast<Dialog*>(data);
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT){
            PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT rect;GetClientRect(h,&rect);
            auto brush=CreateSolidBrush(self->light?RGB(231,235,240):RGB(29,29,32));FillRect(dc,&rect,brush);DeleteObject(brush);
            SetBkMode(dc,TRANSPARENT);SetTextColor(dc,self->fg);SelectObject(dc,self->font);
            for(int i=0;i<Header_GetItemCount(h);++i){RECT item;Header_GetItemRect(h,i,&item);item.left+=8;wchar_t label[128]{};HDITEMW info{};info.mask=HDI_TEXT;info.pszText=label;info.cchTextMax=128;Header_GetItem(h,i,&info);DrawTextW(dc,label,-1,&item,DT_SINGLELINE|DT_VCENTER);}
            EndPaint(h,&paint);return 0;
        }
        return DefSubclassProc(h,msg,w,l);
    }
    static LRESULT CALLBACK procedure(HWND window,UINT msg,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Dialog*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=window;SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(window,msg,w,l);
        try{
            switch(msg){
            case WM_CREATE:self->create();return 0;
            case WM_CLOSE:DestroyWindow(window);return 0;
            case WM_DESTROY:self->done=true;return 0;
            case WM_ERASEBKGND:return 1;
            case WM_PAINT:{
                PAINTSTRUCT paint;auto dc=BeginPaint(window,&paint);RECT rect;GetClientRect(window,&rect);FillRect(dc,&rect,self->background);
                auto old=SelectObject(dc,self->heading);SetTextColor(dc,self->fg);SetBkMode(dc,TRANSPARENT);RECT title{28,24,self->width-28,58};DrawTextW(dc,self->title.c_str(),-1,&title,DT_SINGLELINE|DT_END_ELLIPSIS);
                SelectObject(dc,self->font);
                if(self->file){SetTextColor(dc,self->muted);RECT footer{28,self->height-99,self->width-28,self->height-65};DrawTextW(dc,self->message.c_str(),-1,&footer,DT_WORDBREAK);}
                SelectObject(dc,old);EndPaint(window,&paint);return 0;
            }
            case WM_CTLCOLOREDIT:case WM_CTLCOLORSTATIC:case WM_CTLCOLORLISTBOX:{auto dc=reinterpret_cast<HDC>(w);SetTextColor(dc,self->fg);SetBkColor(dc,self->field);return reinterpret_cast<LRESULT>(self->panel);}
            case WM_MEASUREITEM:{auto item=reinterpret_cast<MEASUREITEMSTRUCT*>(l);if(item->CtlType==ODT_COMBOBOX){item->itemHeight=24;return TRUE;}break;}
            case WM_DRAWITEM:{
                auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
                if(item->CtlType==ODT_COMBOBOX){
                    FillRect(item->hDC,&item->rcItem,self->panel);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,self->fg);SelectObject(item->hDC,self->font);
                    wchar_t value[128]{};if(item->itemID!=static_cast<UINT>(-1))SendMessageW(item->hwndItem,CB_GETLBTEXT,item->itemID,reinterpret_cast<LPARAM>(value));
                    RECT rect=item->rcItem;rect.left+=7;DrawTextW(item->hDC,value,-1,&rect,DT_SINGLELINE|DT_VCENTER);return TRUE;
                }
                if(item->CtlType!=ODT_BUTTON)break;
                FillRect(item->hDC,&item->rcItem,self->background);
                HBRUSH brush=CreateSolidBrush((item->itemState&ODS_SELECTED)?RGB(48,52,58):RGB(28,28,31));HPEN pen=CreatePen(PS_SOLID,1,RGB(35,76,43));
                auto b=SelectObject(item->hDC,brush),p=SelectObject(item->hDC,pen);RoundRect(item->hDC,0,0,item->rcItem.right,item->rcItem.bottom,12,12);SelectObject(item->hDC,b);SelectObject(item->hDC,p);DeleteObject(brush);DeleteObject(pen);
                SetTextColor(item->hDC,RGB(248,248,250));SetBkMode(item->hDC,TRANSPARENT);SelectObject(item->hDC,self->font);
                auto label=self->read(item->hwndItem);RECT rect=item->rcItem;DrawTextW(item->hDC,label.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;
            }
            case WM_COMMAND:{
                int id=LOWORD(w);
                if(id==IDCANCEL){DestroyWindow(window);return 0;}
                if(id==IDOK){if(self->file)self->open();else{self->accepted=true;DestroyWindow(window);}return 0;}
                if(id==21){auto parent=self->folder.parent_path();if(!parent.empty()){self->folder=parent;self->fill();}return 0;}
                if(id==23){self->open(true);return 0;}
                if(id==20 && HIWORD(w)==CBN_SELCHANGE){self->folder=self->read(self->drive);self->fill();return 0;}
                break;
            }
            case WM_NOTIFY:{
                auto info=reinterpret_cast<NMHDR*>(l);
                if(self->file && info->hwndFrom==self->list){
                    int selected=ListView_GetNextItem(self->list,-1,LVNI_SELECTED);
                    if(selected>=0 && static_cast<size_t>(selected)<self->entries.size()){
                        if(info->code==NM_DBLCLK){SetWindowTextW(self->path,self->entries[selected].path().c_str());self->open(true);}
                        else if(info->code==LVN_ITEMCHANGED){std::error_code ec;if(self->entries[selected].is_regular_file(ec))SetWindowTextW(self->path,self->entries[selected].path().c_str());}
                    }
                }break;
            }
            case WM_TIMER:if(w==31&&self->remaining){--self->remaining;if(!self->remaining){DestroyWindow(window);return 0;}auto caption=L"Revert ("+std::to_wstring(self->remaining)+L")";SetWindowTextW(GetDlgItem(window,IDCANCEL),caption.c_str());return 0;}break;
            case WM_KEYDOWN:if(w==VK_ESCAPE){DestroyWindow(window);return 0;}break;
            }
        }catch(const std::exception& e){self->message=wide(e.what());if(self->body)SetWindowTextW(self->body,self->message.c_str());InvalidateRect(window,nullptr,FALSE);}
        return DefWindowProcW(window,msg,w,l);
    }
    void run(){
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"HeadroomAppDialog";cls.style=CS_DROPSHADOW;RegisterClassW(&cls);
        RECT bounds{};if(owner)GetWindowRect(owner,&bounds);else SystemParametersInfoW(SPI_GETWORKAREA,0,&bounds,0);
        int x=bounds.left+(bounds.right-bounds.left-width)/2,y=bounds.top+(bounds.bottom-bounds.top-height)/2;
        window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),WS_POPUP|WS_CLIPCHILDREN,x,y,width,height,owner,nullptr,cls.hInstance,this);
        if(!window){done=true;return;}
        if(owner)EnableWindow(owner,FALSE);
        ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
        SetFocus(file?path:GetDlgItem(window,confirm?IDCANCEL:IDOK));
        MSG msg;
        while(!done){
            int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}
            if(msg.message==WM_KEYDOWN && msg.wParam==VK_ESCAPE){DestroyWindow(window);continue;}
            if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}
        }
        if(owner){EnableWindow(owner,TRUE);SetForegroundWindow(owner);}
    }
};
}
namespace {
struct Toast {
    std::wstring title,message,footer;bool light=false,mode=false;int icon=0,dpi=96,shape_width=0,shape_height=0;HFONT font=nullptr,heading=nullptr,small=nullptr;ULONGLONG start=0;unsigned lifetime=3600;
    Toast(std::wstring caption,std::wstring value,std::wstring hint,bool theme,int symbol,int scale,bool profile):title(std::move(caption)),message(std::move(value)),footer(std::move(hint)),light(theme),mode(profile),icon(symbol),dpi(scale){auto make=[&](int size,int weight){return CreateFontW(-px(size),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");};font=make(13,FW_NORMAL);heading=make(19,FW_SEMIBOLD);small=make(10,FW_MEDIUM);}
    ~Toast(){DeleteObject(font);DeleteObject(heading);DeleteObject(small);}
    int px(int n)const{return MulDiv(n,dpi,96);}
    void shape(HWND h){RECT r{};GetClientRect(h,&r);if(r.right==shape_width&&r.bottom==shape_height)return;shape_width=r.right;shape_height=r.bottom;auto region=CreateRoundRectRgn(0,0,r.right+1,r.bottom+1,px(24),px(24));if(region&&!SetWindowRgn(h,region,TRUE))DeleteObject(region);}
    void paint(HDC dc,RECT r){
        auto bg=light?RGB(248,250,252):RGB(22,24,29),fg=light?RGB(25,32,41):RGB(242,245,250),muted=light?RGB(83,96,111):RGB(155,167,183),accent=RGB(26,242,44),glyph=light?RGB(0,120,49):accent;
        auto solid=[&](RECT area,COLORREF color){auto b=CreateSolidBrush(color);FillRect(dc,&area,b);DeleteObject(b);};
        auto outline=[&](RECT area,COLORREF fill,COLORREF edge,int radius,int thickness=1){auto b=CreateSolidBrush(fill);auto p=CreatePen(PS_SOLID,px(thickness),edge);auto old_b=SelectObject(dc,b),old_p=SelectObject(dc,p);RoundRect(dc,area.left,area.top,area.right,area.bottom,px(radius),px(radius));SelectObject(dc,old_b);SelectObject(dc,old_p);DeleteObject(b);DeleteObject(p);};
        auto line=[&](int x1,int y1,int x2,int y2,COLORREF c,int thickness=1){auto p=CreatePen(PS_SOLID,px(thickness),c);auto old=SelectObject(dc,p);MoveToEx(dc,px(x1),px(y1),nullptr);LineTo(dc,px(x2),px(y2));SelectObject(dc,old);DeleteObject(p);};
        solid(r,bg);outline({0,0,r.right-1,r.bottom-1},bg,accent,24);
        outline({px(18),px(34),px(64),px(80)},light?RGB(226,243,232):RGB(25,43,33),light?RGB(155,207,172):RGB(42,83,54),16);
        auto p=CreatePen(PS_SOLID,px(2),glyph);auto old=SelectObject(dc,p),brush=SelectObject(dc,GetStockObject(NULL_BRUSH));
        if(icon==1){RoundRect(dc,px(26),px(46),px(57),px(67),px(9),px(9));MoveToEx(dc,px(30),px(56),nullptr);LineTo(dc,px(39),px(56));MoveToEx(dc,px(34),px(51),nullptr);LineTo(dc,px(34),px(61));Ellipse(dc,px(46),px(51),px(49),px(54));Ellipse(dc,px(51),px(57),px(54),px(60));}
        else if(icon==2){Ellipse(dc,px(27),px(42),px(56),px(71));Ellipse(dc,px(35),px(42),px(48),px(71));MoveToEx(dc,px(28),px(56),nullptr);LineTo(dc,px(55),px(56));}
        else if(icon==3){POINT nib[]={{px(29),px(66)},{px(34),px(50)},{px(49),px(43)},{px(55),px(49)},{px(47),px(64)},{px(29),px(66)}};Polyline(dc,nib,6);MoveToEx(dc,px(31),px(64),nullptr);LineTo(dc,px(44),px(54));}
        else{Ellipse(dc,px(28),px(43),px(55),px(70));MoveToEx(dc,px(41),px(53),nullptr);LineTo(dc,px(41),px(64));Ellipse(dc,px(40),px(48),px(43),px(51));}
        SelectObject(dc,brush);SelectObject(dc,old);DeleteObject(p);
        auto text=[&](const std::wstring& value,RECT area,HFONT face,COLORREF color,UINT flags){SelectObject(dc,face);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,color);DrawTextW(dc,value.c_str(),-1,&area,flags|DT_NOPREFIX);};
        text(mode?L"MODE SWITCHED":L"NOVATUNE NOTIFICATION",{px(80),px(13),r.right-px(42),px(31)},small,muted,DT_SINGLELINE|DT_VCENTER);
        text(title,{px(79),px(32),r.right-px(38),px(60)},heading,fg,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        text(message,{px(80),px(67),r.right-px(22),px(115)},font,muted,DT_WORDBREAK|DT_END_ELLIPSIS);
        solid({px(20),px(122),r.right-px(20),px(123)},light?RGB(221,229,235):RGB(43,49,58));
        text(footer,{px(20),px(128),r.right-px(90),px(146)},small,muted,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        text(mode?L"● ACTIVE":L"● INFO",{r.right-px(84),px(128),r.right-px(20),px(146)},small,glyph,DT_SINGLELINE|DT_VCENTER|DT_RIGHT);
        int close_x=MulDiv(r.right,96,dpi)-28;line(close_x,18,close_x+7,25,muted);line(close_x+7,18,close_x,25,muted);
        double remaining=1-std::clamp((GetTickCount64()-start)/static_cast<double>(lifetime),0.0,1.0);RECT track{px(20),r.bottom-px(7),r.right-px(20),r.bottom-px(5)};solid(track,light?RGB(219,229,223):RGB(43,53,47));track.right=track.left+static_cast<LONG>((track.right-track.left)*remaining);solid(track,accent);
    }
    static LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Toast*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<Toast*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,msg,w,l);
        if(msg==WM_CREATE){self->start=GetTickCount64();SetTimer(h,1,40,nullptr);self->shape(h);return 0;}
        if(msg==WM_SIZE){self->shape(h);InvalidateRect(h,nullptr,FALSE);return 0;}
        if(msg==WM_TIMER){if(GetTickCount64()-self->start>=self->lifetime)DestroyWindow(h);else InvalidateRect(h,nullptr,FALSE);return 0;}
        if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(msg==WM_LBUTTONUP){DestroyWindow(h);return 0;}
        if(msg==WM_DESTROY){KillTimer(h,1);return 0;}
        if(msg==WM_NCDESTROY){SetWindowLongPtrW(h,GWLP_USERDATA,0);delete self;return 0;}
        if(msg==WM_ERASEBKGND)return 1;
        if(msg==WM_PAINT){PAINTSTRUCT paint;auto target=BeginPaint(h,&paint);RECT r{};GetClientRect(h,&r);auto dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));if(dc&&bitmap){auto previous=SelectObject(dc,bitmap);self->paint(dc,r);BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,previous);}else self->paint(target,r);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);EndPaint(h,&paint);return 0;}
        return DefWindowProcW(h,msg,w,l);
    }
};
void show_toast(HWND owner,const std::wstring& title,const std::wstring& message,const std::wstring& footer,bool light,int icon,bool mode){
    WNDCLASSW cls{};cls.lpfnWndProc=Toast::procedure;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"HeadroomToast";cls.style=CS_DROPSHADOW;RegisterClassW(&cls);
    EnumThreadWindows(GetCurrentThreadId(),[](HWND h,LPARAM l)->BOOL{wchar_t name[64]{};GetClassNameW(h,name,64);if(std::wstring(name)==L"HeadroomToast"&&GetWindow(h,GW_OWNER)==reinterpret_cast<HWND>(l))DestroyWindow(h);return TRUE;},reinterpret_cast<LPARAM>(owner));
    MONITORINFO monitor{};monitor.cbSize=sizeof monitor;if(!GetMonitorInfoW(MonitorFromWindow(owner,MONITOR_DEFAULTTOPRIMARY),&monitor))SystemParametersInfoW(SPI_GETWORKAREA,0,&monitor.rcWork,0);int dpi=owner?GetDpiForWindow(owner):96;if(!dpi)dpi=96;auto scale=[&](int value){return MulDiv(value,dpi,96);};auto area=monitor.rcWork;int width=std::min(scale(400),static_cast<int>(area.right-area.left)-scale(32)),height=scale(160);auto data=new Toast(title,message,footer,light,icon,dpi,mode);
    auto accessible_name=title+L": "+message;HWND toast=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED,cls.lpszClassName,accessible_name.c_str(),WS_POPUP,area.right-width-scale(20),area.bottom-height-scale(20),width,height,owner,nullptr,cls.hInstance,data);
    if(!toast){delete data;return;}SetLayeredWindowAttributes(toast,0,255,LWA_ALPHA);int backdrop=1;DwmSetWindowAttribute(toast,38,&backdrop,sizeof backdrop);int corner=2;DwmSetWindowAttribute(toast,33,&corner,sizeof corner);COLORREF no_border=0xFFFFFFFE;DwmSetWindowAttribute(toast,34,&no_border,sizeof no_border);ShowWindow(toast,SW_SHOWNOACTIVATE);NotifyWinEvent(EVENT_SYSTEM_ALERT,toast,OBJID_WINDOW,CHILDID_SELF);
}
}
void app_toast(HWND owner,const std::wstring& message,bool light){show_toast(owner,L"NovaTune",message,L"Click to dismiss",light,0,false);}
void app_mode_toast(HWND owner,const std::wstring& mode,const std::wstring& detail,const std::wstring& shortcut,bool light){int icon=mode==L"Gaming"?1:mode==L"Browsing"?2:3;show_toast(owner,mode+L" mode",detail,L"Shortcut: "+shortcut,light,icon,true);}
bool app_timed_confirm(HWND owner,const std::wstring& title,const std::wstring& message,unsigned seconds,bool light){Dialog dialog(light);dialog.owner=owner;dialog.title=title;dialog.message=message;dialog.confirm=true;dialog.remaining=std::clamp(seconds,1u,60u);dialog.height=340;dialog.run();return dialog.accepted;}
bool app_dialog(HWND owner,const std::wstring& title,const std::wstring& message,bool confirm,bool light){
    Dialog dialog(light);dialog.owner=owner;dialog.title=title;dialog.message=message;dialog.confirm=confirm;
    size_t lines=1+std::count(message.begin(),message.end(),L'\n')+message.size()/82;
    dialog.height=std::clamp<int>(170+static_cast<int>(lines)*23,240,650);dialog.run();return dialog.accepted;
}
std::wstring app_folder(HWND owner,const std::wstring& title,bool light){
    Dialog dialog(light);dialog.owner=owner;dialog.title=title;dialog.file=true;dialog.folder_mode=true;dialog.width=820;dialog.height=590;
    dialog.message=L"Browse or paste a folder path. Choose folder confirms the current location.";dialog.run();return dialog.result;
}
std::wstring app_file(HWND owner,const std::wstring& title,const std::wstring& extension,bool light){
    Dialog dialog(light);dialog.owner=owner;dialog.title=title;dialog.extension=extension;dialog.file=true;dialog.width=820;dialog.height=590;
    dialog.message=L"Browse folders or paste a full path. Showing "+extension+L" files.";dialog.run();return dialog.result;
}
}
