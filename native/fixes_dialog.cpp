#include "dialog.hpp"
#include "windows.hpp"
#include <dwmapi.h>
#include <algorithm>
#include <stdexcept>
namespace hr {
namespace {
constexpr UINT update_message=WM_APP+45;
struct Update {std::string id;std::wstring status;bool complete;};
struct Review {
    HWND owner=nullptr,window=nullptr;bool light=false,done=false;
    HFONT font=nullptr,heading=nullptr;HBRUSH background=nullptr;
    std::vector<FixItem> items;std::vector<HWND> buttons,statuses;
    std::function<void(HWND,const std::string&)> apply;int dpi=96;
    int px(int n)const{return MulDiv(n,dpi,96);}
    COLORREF bg()const{return light?RGB(246,247,249):RGB(13,13,15);}
    COLORREF fg()const{return light?RGB(28,28,36):RGB(241,241,247);}
    ~Review(){DeleteObject(font);DeleteObject(heading);DeleteObject(background);}
    HWND control(const wchar_t* cls,const std::wstring& text,DWORD style,int id,int x,int y,int w,int h){
        auto c=CreateWindowExW(0,cls,text.c_str(),WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        SendMessageW(c,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return c;
    }
    void update(const Update& u){for(size_t i=0;i<items.size();++i)if(items[i].id==u.id){SetWindowTextW(statuses[i],u.status.c_str());SetWindowTextW(buttons[i],u.complete?L"Fixed":L"Retry");EnableWindow(buttons[i],!u.complete);}}
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Review*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(msg==WM_NCCREATE){self=static_cast<Review*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(!self)return DefWindowProcW(h,msg,w,l);
        switch(msg){
        case WM_CREATE:{
            int corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof corner);COLORREF no_border=0xFFFFFFFE;DwmSetWindowAttribute(h,34,&no_border,sizeof no_border);BOOL dark=!self->light;DwmSetWindowAttribute(h,20,&dark,sizeof dark);
            auto title=self->control(L"STATIC",L"Suggested fixes",0,0,28,22,620,34);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(self->heading),TRUE);
            self->control(L"STATIC",L"Review each change. Its Fix button approves only that action.",0,0,28,66,660,28);
            if(self->items.empty())self->control(L"STATIC",L"No supported fixes found in the temp-file and background-capture checks. This scan does not certify all hardware or drivers.",0,0,40,122,620,56);
            for(size_t i=0;i<self->items.size();++i){int y=112+static_cast<int>(i)*190;
                self->control(L"STATIC",self->items[i].title,0,0,42,y+12,500,26);
                self->control(L"STATIC",self->items[i].description,0,0,42,y+44,500,90);
                self->statuses.push_back(self->control(L"STATIC",L"Awaiting your approval",0,500+static_cast<int>(i),42,y+139,640,34));
                self->buttons.push_back(self->control(L"BUTTON",L"Fix",BS_OWNERDRAW|WS_TABSTOP,100+static_cast<int>(i),576,y+44,112,38));
            }
            self->control(L"BUTTON",L"Close",BS_OWNERDRAW|WS_TABSTOP,IDCANCEL,576,126+static_cast<int>(std::max<size_t>(1,self->items.size()))*190,112,38);return 0;
        }
        case WM_COMMAND:{int id=LOWORD(w);if(id==IDCANCEL){DestroyWindow(h);return 0;}
            if(id>=100&&static_cast<size_t>(id-100)<self->items.size()){
                size_t i=id-100;if(!IsWindowEnabled(self->buttons[i]))return 0;
                EnableWindow(self->buttons[i],FALSE);SetWindowTextW(self->buttons[i],L"Working...");SetWindowTextW(self->statuses[i],L"Applying this fix...");UpdateWindow(h);
                try{self->apply(h,self->items[i].id);}catch(const std::exception& e){self->update({self->items[i].id,wide(e.what()),false});}
            }return 0;
        }
        case update_message:self->update(*reinterpret_cast<Update*>(l));return 0;
        case WM_CTLCOLORSTATIC:{auto dc=reinterpret_cast<HDC>(w);SetBkColor(dc,self->bg());SetTextColor(dc,self->fg());return reinterpret_cast<LRESULT>(self->background);}
        case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);if(item->CtlType!=ODT_BUTTON)break;
            FillRect(item->hDC,&item->rcItem,self->background);HBRUSH brush=CreateSolidBrush(self->light?RGB(229,229,235):RGB(30,30,35));HPEN pen=CreatePen(PS_SOLID,1,IsWindowEnabled(item->hwndItem)?RGB(78,153,90):RGB(75,75,83));
            auto b=SelectObject(item->hDC,brush),p=SelectObject(item->hDC,pen);auto r=item->rcItem;RoundRect(item->hDC,r.left+1,r.top+1,r.right-1,r.bottom-1,12,12);SelectObject(item->hDC,b);SelectObject(item->hDC,p);DeleteObject(brush);DeleteObject(pen);
            wchar_t text[80]{};GetWindowTextW(item->hwndItem,text,80);SelectObject(item->hDC,self->font);SetBkMode(item->hDC,TRANSPARENT);SetTextColor(item->hDC,self->fg());DrawTextW(item->hDC,text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);if(item->itemState&ODS_FOCUS){InflateRect(&r,-4,-4);HPEN focus=CreatePen(PS_SOLID,1,RGB(174,87,246));auto old=SelectObject(item->hDC,focus);auto hollow=SelectObject(item->hDC,GetStockObject(NULL_BRUSH));RoundRect(item->hDC,r.left,r.top,r.right,r.bottom,8,8);SelectObject(item->hDC,old);SelectObject(item->hDC,hollow);DeleteObject(focus);}return TRUE;
        }
        case WM_ERASEBKGND:{RECT r;GetClientRect(h,&r);FillRect(reinterpret_cast<HDC>(w),&r,self->background);return 1;}
        case WM_CLOSE:DestroyWindow(h);return 0;
        case WM_DESTROY:self->done=true;return 0;
        }
        return DefWindowProcW(h,msg,w,l);
    }
    void run(){
        dpi=GetDpiForWindow(owner);if(!dpi)dpi=96;
        background=CreateSolidBrush(bg());font=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");heading=CreateFontW(-px(25),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.lpszClassName=L"HeadroomFixReview";cls.style=CS_DROPSHADOW;RegisterClassW(&cls);
        RECT r;GetWindowRect(owner,&r);int width=px(728),height=px(180+static_cast<int>(std::max<size_t>(1,items.size()))*190);
        window=CreateWindowExW(0,cls.lpszClassName,L"Suggested fixes",WS_POPUP|WS_CLIPCHILDREN,r.left+(r.right-r.left-width)/2,r.top+(r.bottom-r.top-height)/2,width,height,owner,nullptr,cls.hInstance,this);
        if(!window)throw std::runtime_error("Could not open suggested fixes");EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);SetFocus(GetDlgItem(window,IDCANCEL));
        MSG msg;while(!done){int result=GetMessageW(&msg,nullptr,0,0);if(result<=0){if(result==0)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);SetForegroundWindow(owner);
    }
};
}
void app_fix_review(HWND owner,const std::vector<FixItem>& items,const std::function<void(HWND,const std::string&)>& apply,bool light){Review review;review.owner=owner;review.items=items;review.apply=apply;review.light=light;review.run();}
void update_fix_review(HWND window,const std::string& id,const std::wstring& status,bool complete){if(window&&IsWindow(window)){Update update{id,status,complete};SendMessageW(window,update_message,0,reinterpret_cast<LPARAM>(&update));}}
}
