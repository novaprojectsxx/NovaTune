#include "dialog.hpp"
#include "windows.hpp"
#include <wincodec.h>
#include <dwmapi.h>
#include <algorithm>
#include <stdexcept>
namespace hr {
namespace {
void require(HRESULT result){if(FAILED(result))throw std::runtime_error(error(static_cast<DWORD>(result)));}
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
struct Selection {
    HWND owner=nullptr,window=nullptr;bool screen=false,light=false,done=false,accepted=false,dragging=false;
    HBITMAP bitmap=nullptr,back=nullptr;HFONT font=nullptr;HBRUSH background=nullptr;int original_w=0,original_h=0,preview_w=0,preview_h=0,width=0,height=0,sx=0,sy=0;
    RECT image{},selection{},result{};POINT anchor{};
    ~Selection(){if(bitmap)DeleteObject(bitmap);if(back)DeleteObject(back);DeleteObject(font);DeleteObject(background);}
    COLORREF bg()const{return light?RGB(246,246,250):RGB(13,13,15);}
    COLORREF fg()const{return light?RGB(28,28,36):RGB(242,242,248);}
    void load(const std::wstring& path){
        auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)require(initialized);struct Apartment{bool close;~Apartment(){if(close)CoUninitialize();}} apartment{SUCCEEDED(initialized)};
        Com<IWICImagingFactory> factory;require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(&factory.p)));Com<IWICBitmapDecoder> decoder;require(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder.p));Com<IWICBitmapFrameDecode> frame;require(decoder->GetFrame(0,&frame.p));UINT w=0,h=0;require(frame->GetSize(&w,&h));if(!w||!h||w>16384||h>16384||static_cast<unsigned long long>(w)*h>67108864)throw std::runtime_error("Image exceeds preview size limit");original_w=w;original_h=h;
        double ratio=std::min((width-48)/static_cast<double>(w),(height-140)/static_cast<double>(h));preview_w=std::max(1,static_cast<int>(w*ratio));preview_h=std::max(1,static_cast<int>(h*ratio));Com<IWICBitmapScaler> scaler;require(factory->CreateBitmapScaler(&scaler.p));require(scaler->Initialize(frame.p,preview_w,preview_h,WICBitmapInterpolationModeFant));Com<IWICFormatConverter> converter;require(factory->CreateFormatConverter(&converter.p));require(converter->Initialize(scaler.p,GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof info.bmiHeader;info.bmiHeader.biWidth=preview_w;info.bmiHeader.biHeight=-preview_h;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;void* pixels=nullptr;bitmap=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&pixels,nullptr,0);if(!bitmap)throw std::runtime_error("Cannot create image preview");require(converter->CopyPixels(nullptr,preview_w*4,preview_w*preview_h*4,static_cast<BYTE*>(pixels)));
        int x=(width-preview_w)/2,y=64+(height-140-preview_h)/2;image={x,y,x+preview_w,y+preview_h};
    }
    POINT clamp_point(LPARAM value)const{return {std::clamp(static_cast<LONG>(static_cast<short>(LOWORD(value))),image.left,image.right),std::clamp(static_cast<LONG>(static_cast<short>(HIWORD(value))),image.top,image.bottom)};}
    void update(POINT point){RECT before=selection;selection={std::min(anchor.x,point.x),std::min(anchor.y,point.y),std::max(anchor.x,point.x),std::max(anchor.y,point.y)};RECT dirty{};UnionRect(&dirty,&before,&selection);InflateRect(&dirty,3,3);InvalidateRect(window,&dirty,FALSE);}
    void finish(){
        if(selection.right<=selection.left||selection.bottom<=selection.top)return;
        if(screen)result={selection.left+sx,selection.top+sy,selection.right+sx,selection.bottom+sy};
        else result={MulDiv(selection.left-image.left,original_w,preview_w),MulDiv(selection.top-image.top,original_h,preview_h),MulDiv(selection.right-image.left,original_w,preview_w),MulDiv(selection.bottom-image.top,original_h,preview_h)};
        accepted=result.right>result.left&&result.bottom>result.top;DestroyWindow(window);
    }
    static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
        auto self=reinterpret_cast<Selection*>(GetWindowLongPtrW(h,GWLP_USERDATA));if(msg==WM_NCCREATE){self=static_cast<Selection*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);self->window=h;SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}if(!self)return DefWindowProcW(h,msg,w,l);
        switch(msg){
        case WM_CREATE:{if(!self->screen){int corner=2;DwmSetWindowAttribute(h,33,&corner,sizeof corner);COLORREF border=0xFFFFFFFE;DwmSetWindowAttribute(h,34,&border,sizeof border);auto button=[&](const wchar_t* label,int id,int x){auto b=CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,self->height-56,112,34,h,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(self->font),TRUE);};button(L"Crop",IDOK,self->width-264);button(L"Cancel",IDCANCEL,self->width-136);}return 0;}
        case WM_LBUTTONDOWN:{POINT raw{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};auto point=self->clamp_point(l);if(PtInRect(&self->image,raw)){self->anchor=point;self->dragging=true;SetCapture(h);self->update(point);}return 0;}
        case WM_MOUSEMOVE:if(self->dragging)self->update(self->clamp_point(l));return 0;
        case WM_LBUTTONUP:if(self->dragging){self->update(self->clamp_point(l));self->dragging=false;ReleaseCapture();if(self->screen)self->finish();}return 0;
        case WM_CAPTURECHANGED:self->dragging=false;return 0;
        case WM_KEYDOWN:if(w==VK_ESCAPE){DestroyWindow(h);return 0;}break;
        case WM_COMMAND:if(LOWORD(w)==IDCANCEL){DestroyWindow(h);return 0;}if(LOWORD(w)==IDOK){self->finish();return 0;}break;
        case WM_CLOSE:DestroyWindow(h);return 0;
        case WM_DESTROY:self->done=true;return 0;
        case WM_ERASEBKGND:return 1;
        case WM_DRAWITEM:{auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);auto dc=item->hDC;FillRect(dc,&item->rcItem,self->background);HBRUSH brush=CreateSolidBrush(self->light?RGB(226,226,234):RGB(30,30,35));HPEN pen=CreatePen(PS_SOLID,1,RGB(26,242,44));auto b=SelectObject(dc,brush),p=SelectObject(dc,pen);auto r=item->rcItem;RoundRect(dc,r.left+1,r.top+1,r.right-1,r.bottom-1,10,10);SelectObject(dc,b);SelectObject(dc,p);DeleteObject(brush);DeleteObject(pen);wchar_t label[40]{};GetWindowTextW(item->hwndItem,label,40);SelectObject(dc,self->font);SetTextColor(dc,self->fg());SetBkMode(dc,TRANSPARENT);DrawTextW(dc,label,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);return TRUE;}
        case WM_PAINT:{PAINTSTRUCT paint;auto dc=BeginPaint(h,&paint);RECT r;GetClientRect(h,&r);HDC memory=CreateCompatibleDC(dc);auto back=self->back;auto old=SelectObject(memory,back);IntersectClipRect(memory,paint.rcPaint.left,paint.rcPaint.top,paint.rcPaint.right,paint.rcPaint.bottom);FillRect(memory,&r,self->background);
            if(self->bitmap){HDC source=CreateCompatibleDC(dc);auto bitmap=SelectObject(source,self->bitmap);BitBlt(memory,self->image.left,self->image.top,self->preview_w,self->preview_h,source,0,0,SRCCOPY);SelectObject(source,bitmap);DeleteDC(source);}
            SelectObject(memory,self->font);SetBkMode(memory,TRANSPARENT);SetTextColor(memory,self->fg());RECT title{24,20,self->width-24,52};DrawTextW(memory,self->screen?L"Drag to choose the capture region. Esc cancels.":L"Drag to select image pixels, then choose Crop. Original is preserved.",-1,&title,DT_SINGLELINE|DT_END_ELLIPSIS);
            if(self->selection.right>self->selection.left&&self->selection.bottom>self->selection.top){HPEN pen=CreatePen(PS_SOLID,2,RGB(26,242,44));auto p=SelectObject(memory,pen),b=SelectObject(memory,GetStockObject(NULL_BRUSH));Rectangle(memory,self->selection.left,self->selection.top,self->selection.right,self->selection.bottom);SelectObject(memory,p);SelectObject(memory,b);DeleteObject(pen);}
            BitBlt(dc,0,0,r.right,r.bottom,memory,0,0,SRCCOPY);SelectObject(memory,old);DeleteDC(memory);EndPaint(h,&paint);return 0;}
        }return DefWindowProcW(h,msg,w,l);
    }
    std::optional<RECT> run(const std::wstring& path={}){
        RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);WINDOWPLACEMENT placement{};placement.length=sizeof placement;GetWindowPlacement(owner,&placement);bool visible=IsWindowVisible(owner)!=FALSE;
        width=screen?GetSystemMetrics(SM_CXVIRTUALSCREEN):std::min(1000L,work.right-work.left-60);height=screen?GetSystemMetrics(SM_CYVIRTUALSCREEN):std::min(700L,work.bottom-work.top-60);sx=screen?GetSystemMetrics(SM_XVIRTUALSCREEN):work.left+(work.right-work.left-width)/2;sy=screen?GetSystemMetrics(SM_YVIRTUALSCREEN):work.top+(work.bottom-work.top-height)/2;
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");background=CreateSolidBrush(bg());if(static_cast<unsigned long long>(width)*height>67108864)throw std::runtime_error("Desktop exceeds visual-picker capacity; enter region coordinates instead");if(screen)image={0,0,width,height};else load(path);auto display=GetDC(nullptr);back=CreateCompatibleBitmap(display,width,height);ReleaseDC(nullptr,display);if(!back)throw std::runtime_error("Cannot create selection surface");
        WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.hCursor=LoadCursorW(nullptr,IDC_CROSS);cls.lpszClassName=L"HeadroomSelection";RegisterClassW(&cls);
        if(screen&&visible)ShowWindow(owner,SW_HIDE);window=CreateWindowExW(screen?WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED:WS_EX_TOOLWINDOW,cls.lpszClassName,screen?L"Choose capture region":L"Crop screenshot",WS_POPUP|WS_CLIPCHILDREN,sx,sy,width,height,screen?nullptr:owner,nullptr,cls.hInstance,this);
        if(!window){if(screen&&visible)SetWindowPlacement(owner,&placement);throw std::runtime_error("Selection window could not open");}if(screen)SetLayeredWindowAttributes(window,0,150,LWA_ALPHA);EnableWindow(owner,FALSE);ShowWindow(window,SW_SHOW);SetForegroundWindow(window);
        MSG msg;while(!done){int value=GetMessageW(&msg,nullptr,0,0);if(value<=0){if(!value)PostQuitMessage(static_cast<int>(msg.wParam));break;}if(msg.message==WM_KEYDOWN&&msg.wParam==VK_ESCAPE){DestroyWindow(window);continue;}if(!IsDialogMessageW(window,&msg)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
        if(IsWindow(window))DestroyWindow(window);EnableWindow(owner,TRUE);if(screen&&visible)SetWindowPlacement(owner,&placement);SetForegroundWindow(owner);if(accepted)return result;return {};
    }
};
}
std::optional<RECT> app_region(HWND owner){Selection selector;selector.owner=owner;selector.screen=true;return selector.run();}
std::optional<RECT> app_crop_region(HWND owner,const std::wstring& image,bool light){Selection selector;selector.owner=owner;selector.light=light;return selector.run(image);}
}
