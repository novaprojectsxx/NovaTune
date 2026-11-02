#include "scrollbars.hpp"
#include <commctrl.h>
#include <algorithm>
#include <string>
namespace hr {
namespace {
bool light_theme=false;
struct Scroll {int drag=-1;int offset=0;bool redraw=true;};
struct Bar {RECT rect{},thumb{};SCROLLINFO info{};bool vertical=false;};
bool geometry(HWND h,int bar,Bar& b){
    SCROLLBARINFO native{};native.cbSize=sizeof native;
    if(!GetScrollBarInfo(h,bar==SB_VERT?OBJID_VSCROLL:OBJID_HSCROLL,&native)||(native.rgstate[0]&(STATE_SYSTEM_INVISIBLE|STATE_SYSTEM_OFFSCREEN)))return false;
    b.info.cbSize=sizeof b.info;b.info.fMask=SIF_RANGE|SIF_PAGE|SIF_POS;if(!GetScrollInfo(h,bar,&b.info))return false;
    b.vertical=bar==SB_VERT;b.rect=native.rcScrollBar;
    int start=b.vertical?b.rect.top:b.rect.left,length=b.vertical?b.rect.bottom-b.rect.top:b.rect.right-b.rect.left;
    auto range=static_cast<long long>(b.info.nMax)-b.info.nMin+1;int extent=std::max(1,length-4);
    int thumb=range>0?std::clamp(static_cast<int>(extent*static_cast<double>(b.info.nPage)/range),std::min(28,extent),extent):extent;
    long long maximum=std::max<long long>(b.info.nMin,static_cast<long long>(b.info.nMax)-std::max(1u,b.info.nPage)+1);
    int position=maximum>b.info.nMin?static_cast<int>((extent-thumb)*static_cast<double>(b.info.nPos-b.info.nMin)/(maximum-b.info.nMin)):0;
    b.thumb=b.rect;if(b.vertical){b.thumb.top=start+2+position;b.thumb.bottom=b.thumb.top+thumb;}else{b.thumb.left=start+2+position;b.thumb.right=b.thumb.left+thumb;}return true;
}
void paint(HWND h,int dragging){
    if(!IsWindowVisible(h))return;RECT window{};GetWindowRect(h,&window);auto dc=GetWindowDC(h);if(!dc)return;
    for(int bar:{SB_VERT,SB_HORZ}){Bar b;if(!geometry(h,bar,b))continue;OffsetRect(&b.rect,-window.left,-window.top);OffsetRect(&b.thumb,-window.left,-window.top);
        HBRUSH track=CreateSolidBrush(light_theme?RGB(241,241,245):RGB(22,22,25));FillRect(dc,&b.rect,track);DeleteObject(track);
        if(b.vertical){int center=(b.rect.left+b.rect.right)/2;b.thumb.left=center-3;b.thumb.right=center+3;}else{int center=(b.rect.top+b.rect.bottom)/2;b.thumb.top=center-3;b.thumb.bottom=center+3;}
        HBRUSH thumb=CreateSolidBrush(light_theme?RGB(128,128,141):RGB(89,89,102));auto brush=SelectObject(dc,thumb),pen=SelectObject(dc,GetStockObject(NULL_PEN));RoundRect(dc,b.thumb.left,b.thumb.top,b.thumb.right,b.thumb.bottom,6,6);SelectObject(dc,brush);SelectObject(dc,pen);DeleteObject(thumb);
    }
                                                                         
    Bar v,horizontal;if(geometry(h,SB_VERT,v)&&geometry(h,SB_HORZ,horizontal)){RECT corner{v.rect.left-window.left,horizontal.rect.top-window.top,v.rect.right-window.left,horizontal.rect.bottom-window.top};HBRUSH brush=CreateSolidBrush(light_theme?RGB(241,241,245):RGB(22,22,25));FillRect(dc,&corner,brush);DeleteObject(brush);}
    wchar_t cls[40]{};GetClassNameW(h,cls,40);
    if(std::wstring(cls)==L"ComboLBox"){
        RECT frame{0,0,window.right-window.left,window.bottom-window.top};HBRUSH brush=CreateSolidBrush(light_theme?RGB(192,192,203):RGB(55,55,64));FrameRect(dc,&frame,brush);InflateRect(&frame,-1,-1);FrameRect(dc,&frame,brush);DeleteObject(brush);
    }
    ReleaseDC(h,dc);
}
void scroll_to(HWND h,int bar,int position){
    SCROLLINFO info{};info.cbSize=sizeof info;info.fMask=SIF_POS;GetScrollInfo(h,bar,&info);
    wchar_t name[40]{};GetClassNameW(h,name,40);std::wstring cls(name);
    if(cls==L"Edit"&&bar==SB_VERT)SendMessageW(h,EM_LINESCROLL,0,position-info.nPos);
    else if(cls==L"Edit")SendMessageW(h,EM_LINESCROLL,position-info.nPos,0);
    else if((cls==L"ListBox"||cls==L"ComboLBox")&&bar==SB_VERT)SendMessageW(h,LB_SETTOPINDEX,position,0);
    else if(cls==WC_LISTVIEWW){
        if(bar==SB_VERT){RECT row{};int height=1;if((GetWindowLongPtrW(h,GWL_STYLE)&LVS_TYPEMASK)==LVS_REPORT && ListView_GetItemRect(h,0,&row,LVIR_BOUNDS))height=std::max(1L,row.bottom-row.top);ListView_Scroll(h,0,(position-info.nPos)*height);}
        else ListView_Scroll(h,position-info.nPos,0);
    }else SendMessageW(h,bar==SB_VERT?WM_VSCROLL:WM_HSCROLL,MAKEWPARAM(SB_THUMBPOSITION,position),0);
}
LRESULT CALLBACK dropdown(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR){
    if(msg==WM_ERASEBKGND)return 1;
    if(msg==WM_PAINT){
        PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT r;GetClientRect(h,&r);
        HBRUSH brush=CreateSolidBrush(light_theme?RGB(255,255,255):RGB(25,25,28));FillRect(dc,&r,brush);DeleteObject(brush);
        brush=CreateSolidBrush(light_theme?RGB(170,170,184):RGB(55,55,64));FrameRect(dc,&r,brush);DeleteObject(brush);
        SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(h,WM_GETFONT,0,0)));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,light_theme?RGB(28,28,36):RGB(231,235,242));
        int selected=static_cast<int>(SendMessageW(h,CB_GETCURSEL,0,0));std::wstring value;
        if(selected!=CB_ERR){int n=static_cast<int>(SendMessageW(h,CB_GETLBTEXTLEN,selected,0));if(n>=0){value.resize(n+1);SendMessageW(h,CB_GETLBTEXT,selected,reinterpret_cast<LPARAM>(value.data()));value.resize(n);}}
        RECT label=r;label.left+=8;label.right-=26;DrawTextW(dc,value.c_str(),-1,&label,DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        int cx=r.right-12,cy=r.bottom/2;POINT arrow[]={{cx-4,cy-2},{cx+4,cy-2},{cx,cy+3}};brush=CreateSolidBrush(RGB(26,242,44));auto old=SelectObject(dc,brush),pen=SelectObject(dc,GetStockObject(NULL_PEN));Polygon(dc,arrow,3);SelectObject(dc,old);SelectObject(dc,pen);DeleteObject(brush);EndPaint(h,&paint);return 0;
    }
    auto result=DefSubclassProc(h,msg,w,l);
    if(msg==WM_NCPAINT){RECT r;GetWindowRect(h,&r);r.right-=r.left;r.bottom-=r.top;r.left=r.top=0;auto dc=GetWindowDC(h);HBRUSH brush=CreateSolidBrush(light_theme?RGB(170,170,184):RGB(55,55,64));FrameRect(dc,&r,brush);InflateRect(&r,-1,-1);FrameRect(dc,&r,brush);DeleteObject(brush);ReleaseDC(h,dc);}
    if(msg==CB_SETCURSEL||msg==WM_SETFOCUS||msg==WM_KILLFOCUS||msg==WM_ENABLE)InvalidateRect(h,nullptr,FALSE);
    if(msg==WM_NCDESTROY)RemoveWindowSubclass(h,dropdown,85);
    return result;
}
LRESULT CALLBACK procedure(HWND h,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data){
    auto state=reinterpret_cast<Scroll*>(data);
                                                                                   
    if(msg==WM_SETTEXT&&l&&(GetWindowLongPtrW(h,GWL_STYLE)&ES_MULTILINE)){
        wchar_t cls[40]{};GetClassNameW(h,cls,40);if(std::wstring(cls)==L"Edit"){
            const auto input=reinterpret_cast<const wchar_t*>(l);std::wstring normalized;
            for(size_t i=0;input[i];++i){if(input[i]==L'\n'&&(i==0||input[i-1]!=L'\r'))normalized+=L'\r';normalized+=input[i];}
            return DefSubclassProc(h,msg,w,reinterpret_cast<LPARAM>(normalized.c_str()));
        }
    }

    if(msg==WM_SETREDRAW)state->redraw=w!=0;
    if(msg==WM_NCLBUTTONDOWN&&(w==HTVSCROLL||w==HTHSCROLL)){
        int bar=w==HTVSCROLL?SB_VERT:SB_HORZ;Bar b;if(!geometry(h,bar,b))return 0;POINT cursor;GetCursorPos(&cursor);
        if(PtInRect(&b.thumb,cursor)){state->drag=bar;state->offset=(b.vertical?cursor.y-b.thumb.top:cursor.x-b.thumb.left);SetCapture(h);paint(h,bar);}
        else{int point=b.vertical?cursor.y:cursor.x,start=b.vertical?b.thumb.top:b.thumb.left;SendMessageW(h,b.vertical?WM_VSCROLL:WM_HSCROLL,point<start?SB_PAGEUP:SB_PAGEDOWN,0);paint(h,-1);}return 0;
    }
    if(msg==WM_MOUSEMOVE&&state->drag!=-1){
        Bar b;if(geometry(h,state->drag,b)){POINT cursor;GetCursorPos(&cursor);int length=b.vertical?b.rect.bottom-b.rect.top:b.rect.right-b.rect.left;
            int thumb=b.vertical?b.thumb.bottom-b.thumb.top:b.thumb.right-b.thumb.left;int offset=(b.vertical?cursor.y-b.rect.top:cursor.x-b.rect.left)-2-state->offset;
            int travel=std::max(1,length-4-thumb);long long maximum=std::max<long long>(b.info.nMin,static_cast<long long>(b.info.nMax)-std::max(1u,b.info.nPage)+1);
            int position=b.info.nMin+static_cast<int>((maximum-b.info.nMin)*std::clamp(offset/static_cast<double>(travel),0.0,1.0));scroll_to(h,state->drag,position);paint(h,state->drag);
        }return 0;
    }
    if((msg==WM_LBUTTONUP||msg==WM_CANCELMODE)&&state->drag!=-1){state->drag=-1;if(GetCapture()==h)ReleaseCapture();paint(h,-1);return 0;}
    if(msg==WM_CAPTURECHANGED){state->drag=-1;paint(h,-1);}
    auto result=DefSubclassProc(h,msg,w,l);
    if(state->redraw&&(msg==WM_NCPAINT||msg==WM_PAINT||msg==WM_NCMOUSEMOVE||msg==WM_NCMOUSELEAVE||msg==WM_MOUSELEAVE||msg==WM_NCACTIVATE||msg==WM_ENABLE||msg==WM_MOUSEWHEEL||msg==WM_MOUSEHWHEEL||msg==WM_VSCROLL||msg==WM_HSCROLL||msg==WM_SIZE||msg==WM_SETTEXT||msg==WM_KEYDOWN||msg==WM_SETREDRAW))paint(h,state->drag);
    if(msg==WM_NCDESTROY){RemoveWindowSubclass(h,procedure,84);RemovePropW(h,L"Headroom.Scrollbar");delete state;}
    return result;
}
}
void scrollbar_theme(bool light){light_theme=light;}
void style_scrollbars(HWND control,bool light){
    light_theme=light;if(!control)return;wchar_t name[40]{};GetClassNameW(control,name,40);std::wstring cls(name);
    if(cls==L"ComboBox"){SetWindowSubclass(control,dropdown,85,0);COMBOBOXINFO info{};info.cbSize=sizeof info;if(GetComboBoxInfo(control,&info))style_scrollbars(info.hwndList,light);return;}
    if(cls!=L"Edit"&&cls!=WC_LISTVIEWW&&cls!=L"ListBox"&&cls!=L"ComboLBox")return;
    if(GetPropW(control,L"Headroom.Scrollbar"))return;
    auto state=new Scroll;if(!SetWindowSubclass(control,procedure,84,reinterpret_cast<DWORD_PTR>(state)))delete state;
    else if(!SetPropW(control,L"Headroom.Scrollbar",state)){RemoveWindowSubclass(control,procedure,84);delete state;}
    else if(cls==L"Edit"&&(GetWindowLongPtrW(control,GWL_STYLE)&ES_MULTILINE)){
        int length=GetWindowTextLengthW(control);std::wstring initial(length+1,L'\0');GetWindowTextW(control,initial.data(),length+1);SetWindowTextW(control,initial.c_str());
    }
}
}
