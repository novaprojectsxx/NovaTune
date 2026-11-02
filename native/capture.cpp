#include "capture.hpp"
#include <shlobj.h>
#include <shellapi.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shobjidl.h>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace hr {
namespace {
void require(HRESULT r,const char* action){if(FAILED(r))throw std::runtime_error(std::string(action)+": "+hr::error(static_cast<DWORD>(r)));}
template<class T>struct Com {T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
struct Apartment {HRESULT result;Apartment():result(CoInitializeEx(nullptr,COINIT_MULTITHREADED)){require(result,"COM");}~Apartment(){CoUninitialize();}};
struct Surface {
    HDC screen=nullptr,dc=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ old=nullptr;
    BYTE* pixels=nullptr;
    int width,height;
    Surface(int w,int h):width(w),height(h){
        screen=GetDC(nullptr);dc=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=w;info.bmiHeader.biHeight=-h;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,reinterpret_cast<void**>(&pixels),nullptr,0);
        if(!dc||!bitmap){if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);throw std::runtime_error("Could not create capture surface");}
        old=SelectObject(dc,bitmap);
    }
    ~Surface(){if(old)SelectObject(dc,old);if(bitmap)DeleteObject(bitmap);if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);}
    void capture(const CaptureOptions& options={}){
        int sx=0,sy=0,sw=GetSystemMetrics(SM_CXSCREEN),sh=GetSystemMetrics(SM_CYSCREEN);
        if(options.region.right>options.region.left&&options.region.bottom>options.region.top){sx=options.region.left;sy=options.region.top;sw=options.region.right-options.region.left;sh=options.region.bottom-options.region.top;}
        PatBlt(dc,0,0,width,height,BLACKNESS);
        double scale=std::min(static_cast<double>(width)/sw,static_cast<double>(height)/sh);
        int w=static_cast<int>(sw*scale),h=static_cast<int>(sh*scale),x=(width-w)/2,y=(height-h)/2;
        SetStretchBltMode(dc,HALFTONE);SetBrushOrgEx(dc,0,0,nullptr);
        if(!StretchBlt(dc,x,y,w,h,screen,sx,sy,sw,sh,SRCCOPY|CAPTUREBLT))throw std::runtime_error("Desktop capture failed");
        CURSORINFO cursor{};cursor.cbSize=sizeof cursor;
        if(options.cursor&&GetCursorInfo(&cursor) && cursor.flags==CURSOR_SHOWING){
            ICONINFO icon{};
            if(GetIconInfo(cursor.hCursor,&icon)){
                int cx=x+static_cast<int>((cursor.ptScreenPos.x-sx-icon.xHotspot)*scale),cy=y+static_cast<int>((cursor.ptScreenPos.y-sy-icon.yHotspot)*scale);
                DrawIconEx(dc,cx,cy,cursor.hCursor,static_cast<int>(GetSystemMetrics(SM_CXCURSOR)*scale),static_cast<int>(GetSystemMetrics(SM_CYCURSOR)*scale),0,nullptr,DI_NORMAL);
                if(icon.hbmColor)DeleteObject(icon.hbmColor);if(icon.hbmMask)DeleteObject(icon.hbmMask);
            }
        }
        GdiFlush();
    }
};
std::wstring filename(const wchar_t* prefix,const wchar_t* extension){
    static std::atomic<unsigned long long> sequence{0};SYSTEMTIME t;GetLocalTime(&t);wchar_t text[160];
    swprintf(text,160,L"%ls-%04u%02u%02u-%02u%02u%02u-%03u-%lu-%llu%ls",prefix,t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId(),++sequence,extension);return text;
}
BYTE clamp(int value){return static_cast<BYTE>(std::clamp(value,0,255));}
void nv12(const Surface& surface,BYTE* output){
    const int w=surface.width,h=surface.height;
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        auto p=surface.pixels+(y*w+x)*4;int b=p[0],g=p[1],r=p[2];
        output[y*w+x]=clamp(((66*r+129*g+25*b+128)>>8)+16);
    }
    auto uv=output+w*h;
    for(int y=0;y<h;y+=2)for(int x=0;x<w;x+=2){
        int r=0,g=0,b=0;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){auto p=surface.pixels+((y+dy)*w+x+dx)*4;b+=p[0];g+=p[1];r+=p[2];}
        r/=4;g/=4;b/=4;
        uv[(y/2)*w+x]=clamp(((-38*r-74*g+112*b+128)>>8)+128);
        uv[(y/2)*w+x+1]=clamp(((112*r-94*g-18*b+128)>>8)+128);
    }
}
}
std::filesystem::path capture_folder(){
    PWSTR value=nullptr;require(SHGetKnownFolderPath(FOLDERID_Videos,KF_FLAG_CREATE,nullptr,&value),"Videos folder");
    std::filesystem::path root(value);CoTaskMemFree(value);return migrate_app_folder(root);
}
void validate_capture_options(const CaptureOptions& options){
    const auto& r=options.region;if(r.left||r.top||r.right||r.bottom){int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        long long w=static_cast<long long>(r.right)-r.left,h=static_cast<long long>(r.bottom)-r.top;if(w<1||h<1||w>16384||h>16384||w*h>67108864||r.left<x||r.top<y||r.right>static_cast<long long>(x)+width||r.bottom>static_cast<long long>(y)+height)throw std::runtime_error("Capture region is invalid or outside the current desktop; select it again. No fullscreen fallback is used.");}
    if(!options.output.empty()){auto base=std::filesystem::weakly_canonical(capture_folder()),parent=std::filesystem::weakly_canonical(options.output.parent_path());auto root=lower(utf8(base.wstring())),path=lower(utf8(parent.wstring()));if(!options.output.is_absolute()||std::filesystem::exists(options.output)||(path!=root&&path.rfind(root+"\\",0)!=0))throw std::runtime_error("Recording output must be a new file under NovaTune captures");}
}
std::filesystem::path screenshot(){return screenshot(CaptureOptions{});}
std::filesystem::path screenshot(const CaptureOptions& options){
    validate_capture_options(options);
    int width=GetSystemMetrics(SM_CXSCREEN),height=GetSystemMetrics(SM_CYSCREEN);if(options.region.right>options.region.left&&options.region.bottom>options.region.top){width=options.region.right-options.region.left;height=options.region.bottom-options.region.top;}if(width<1||height<1||width>16384||height>16384)throw std::runtime_error("Capture region is outside supported dimensions");
    Apartment apartment;Surface surface(width,height);surface.capture(options);
    auto output=capture_folder()/filename(L"Screenshot",L".png");
    Com<IWICImagingFactory> factory;
    require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(&factory.p)),"PNG factory");
    Com<IWICStream> stream;require(factory->CreateStream(&stream.p),"PNG stream");require(stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE),"PNG output");
    Com<IWICBitmapEncoder> encoder;require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder.p),"PNG encoder");require(encoder->Initialize(stream.p,WICBitmapEncoderNoCache),"PNG encoder setup");
    Com<IWICBitmapFrameEncode> frame;Com<IPropertyBag2> properties;require(encoder->CreateNewFrame(&frame.p,&properties.p),"PNG frame");require(frame->Initialize(properties.p),"PNG frame setup");
    require(frame->SetSize(surface.width,surface.height),"PNG size");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGR;
    require(frame->SetPixelFormat(&format),"PNG pixels");
    Com<IWICBitmap> bitmap;require(factory->CreateBitmapFromHBITMAP(surface.bitmap,nullptr,WICBitmapIgnoreAlpha,&bitmap.p),"PNG bitmap");
    require(frame->WriteSource(bitmap.p,nullptr),"PNG write");require(frame->Commit(),"PNG frame commit");require(encoder->Commit(),"PNG commit");
    return output;
}
Recorder::~Recorder(){stop();}
unsigned Recorder::elapsed()const{return running&&started.load()?static_cast<unsigned>((GetTickCount64()-started.load())/1000):0;}
std::string Recorder::error()const{std::lock_guard<std::mutex> lock(mutex);return problem;}
std::filesystem::path Recorder::file()const{std::lock_guard<std::mutex> lock(mutex);return output;}
void Recorder::stop(){stopping=true;if(worker.joinable())worker.join();running=false;}
void Recorder::start(){start(CaptureOptions{});}
void Recorder::start(const CaptureOptions& options){
    validate_capture_options(options);
    if(running)throw std::runtime_error("Recording is already active");
    stop();stopping=false;
    {std::lock_guard<std::mutex> lock(mutex);problem.clear();output=options.output.empty()?capture_folder()/filename(L"Recording",L".mp4"):options.output;}
    running=true;started=0;written_frames=0;
    worker=std::thread([this,options]{
        try{
            Apartment apartment;require(MFStartup(MF_VERSION,MFSTARTUP_FULL),"Media Foundation");
            struct Shutdown{~Shutdown(){MFShutdown();}} shutdown;
            const UINT32 width=1280,height=720,fps=30;
            Com<IMFAttributes> attributes;require(MFCreateAttributes(&attributes.p,2),"Recorder settings");
            attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,TRUE);
            Com<IMFSinkWriter> writer;auto path=file();require(MFCreateSinkWriterFromURL(path.c_str(),nullptr,attributes.p,&writer.p),"MP4 writer");
            Com<IMFMediaType> output_type;require(MFCreateMediaType(&output_type.p),"H264 type");
            output_type->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);output_type->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_H264);
            output_type->SetUINT32(MF_MT_AVG_BITRATE,6000000);output_type->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
            MFSetAttributeSize(output_type.p,MF_MT_FRAME_SIZE,width,height);MFSetAttributeRatio(output_type.p,MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(output_type.p,MF_MT_PIXEL_ASPECT_RATIO,1,1);
            DWORD index=0;require(writer->AddStream(output_type.p,&index),"H264 stream");
            Com<IMFMediaType> input;require(MFCreateMediaType(&input.p),"NV12 type");
            input->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Video);input->SetGUID(MF_MT_SUBTYPE,MFVideoFormat_NV12);
            input->SetUINT32(MF_MT_INTERLACE_MODE,MFVideoInterlace_Progressive);
            MFSetAttributeSize(input.p,MF_MT_FRAME_SIZE,width,height);MFSetAttributeRatio(input.p,MF_MT_FRAME_RATE,fps,1);MFSetAttributeRatio(input.p,MF_MT_PIXEL_ASPECT_RATIO,1,1);
            require(writer->SetInputMediaType(index,input.p,nullptr),"NV12 input");require(writer->BeginWriting(),"Start MP4");started=GetTickCount64();
            Surface surface(width,height);
            auto start=std::chrono::steady_clock::now(),next=start;
            LONGLONG previous=0;
            while(!stopping){
                if(options.seconds&&GetTickCount64()-started.load()>=options.seconds*1000ULL)break;
                auto now=std::chrono::steady_clock::now();
                auto time=std::chrono::duration_cast<std::chrono::nanoseconds>(now-start).count()/100;
                if(time<=previous)time=previous+1;previous=time;
                surface.capture(options);
                Com<IMFMediaBuffer> buffer;require(MFCreateMemoryBuffer(width*height*3/2,&buffer.p),"Frame buffer");
                BYTE* bytes=nullptr;require(buffer->Lock(&bytes,nullptr,nullptr),"Frame lock");nv12(surface,bytes);require(buffer->Unlock(),"Frame unlock");
                require(buffer->SetCurrentLength(width*height*3/2),"Frame size");
                Com<IMFSample> sample;require(MFCreateSample(&sample.p),"Frame sample");sample->AddBuffer(buffer.p);sample->SetSampleTime(time);sample->SetSampleDuration(10000000/fps);
                require(writer->WriteSample(index,sample.p),"Encode frame");++written_frames;
                next+=std::chrono::microseconds(1000000/fps);std::this_thread::sleep_until(next);
                if(std::chrono::steady_clock::now()-next>std::chrono::seconds(1))next=std::chrono::steady_clock::now();
            }
            require(writer->Finalize(),"Finalize MP4");
        }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);problem=e.what();}
        running=false;
    });
}
std::string encoder_report(){
    Apartment apartment;require(MFStartup(MF_VERSION,MFSTARTUP_FULL),"Media Foundation");struct Shutdown{~Shutdown(){MFShutdown();}} shutdown;
    std::ostringstream out;out<<"H.264 ENCODER CAPABILITIES\nAvailability is not a throughput guarantee. Current recorder is silent 720p, up to 30 FPS.\n\n";
    for(bool hardware:{true,false}){MFT_REGISTER_TYPE_INFO input{MFMediaType_Video,MFVideoFormat_NV12},output{MFMediaType_Video,MFVideoFormat_H264};IMFActivate** activations=nullptr;UINT32 count=0;auto flags=hardware?MFT_ENUM_FLAG_HARDWARE|MFT_ENUM_FLAG_SORTANDFILTER:MFT_ENUM_FLAG_SYNCMFT|MFT_ENUM_FLAG_ASYNCMFT|MFT_ENUM_FLAG_LOCALMFT|MFT_ENUM_FLAG_SORTANDFILTER;
        auto result=MFTEnumEx(MFT_CATEGORY_VIDEO_ENCODER,flags,&input,&output,&activations,&count);out<<(hardware?"Hardware MFTs: ":"Software/local MFTs: ")<<(SUCCEEDED(result)?std::to_string(count):"query unavailable")<<"\n";
        if(SUCCEEDED(result))for(UINT32 i=0;i<count;++i){wchar_t* name=nullptr;UINT32 length=0;if(SUCCEEDED(activations[i]->GetAllocatedString(MFT_FRIENDLY_NAME_Attribute,&name,&length))){out<<"  "<<utf8(name)<<"\n";CoTaskMemFree(name);}activations[i]->Release();}CoTaskMemFree(activations);
    }out<<"\nTest a short capture in the same game scene before streaming. Other capture tools can compete for encoder resources.";return out.str();
}
std::filesystem::path crop_screenshot(const std::filesystem::path& input,const RECT& crop){
    if(lower(utf8(input.extension().wstring()))!=".png"||crop.left<0||crop.top<0||crop.right<=crop.left||crop.bottom<=crop.top)throw std::runtime_error("Choose a PNG and a positive crop rectangle");
    Apartment apartment;Com<IWICImagingFactory> factory;require(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(&factory.p)),"Image factory");Com<IWICBitmapDecoder> decoder;require(factory->CreateDecoderFromFilename(input.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder.p),"Read PNG");Com<IWICBitmapFrameDecode> frame;require(decoder->GetFrame(0,&frame.p),"Read image frame");UINT width=0,height=0;frame->GetSize(&width,&height);if(crop.right>static_cast<LONG>(width)||crop.bottom>static_cast<LONG>(height)||width>16384||height>16384)throw std::runtime_error("Crop is outside the image bounds");
    Com<IWICBitmapClipper> clipper;require(factory->CreateBitmapClipper(&clipper.p),"Crop setup");WICRect bounds{crop.left,crop.top,crop.right-crop.left,crop.bottom-crop.top};require(clipper->Initialize(frame.p,&bounds),"Crop bounds");auto output=capture_folder()/filename(L"Cropped",L".png");Com<IWICStream> stream;require(factory->CreateStream(&stream.p),"Image stream");require(stream->InitializeFromFilename(output.c_str(),GENERIC_WRITE),"New crop output");Com<IWICBitmapEncoder> encoder;require(factory->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder.p),"PNG writer");require(encoder->Initialize(stream.p,WICBitmapEncoderNoCache),"PNG writer setup");Com<IWICBitmapFrameEncode> target;Com<IPropertyBag2> props;require(encoder->CreateNewFrame(&target.p,&props.p),"PNG frame");require(target->Initialize(props.p),"PNG frame setup");require(target->SetSize(bounds.Width,bounds.Height),"Crop dimensions");WICPixelFormatGUID format=GUID_WICPixelFormat32bppBGRA;require(target->SetPixelFormat(&format),"Crop format");require(target->WriteSource(clipper.p,nullptr),"Write crop");require(target->Commit(),"Commit crop");require(encoder->Commit(),"Commit PNG");return output;
}
std::vector<MediaEntry> media_entries(){
    Apartment apartment;std::vector<std::filesystem::directory_entry> files;for(const auto& file:std::filesystem::directory_iterator(capture_folder())){auto attributes=GetFileAttributesW(file.path().c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT)||!file.is_regular_file())continue;auto ext=lower(utf8(file.path().extension().wstring()));if(ext==".png"||ext==".mp4")files.push_back(file);}std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});std::vector<MediaEntry> result;
    for(size_t i=0;i<std::min<size_t>(200,files.size());++i){MediaEntry entry;entry.path=files[i].path();entry.bytes=files[i].file_size();Com<IShellItemImageFactory> factory;
        if(SUCCEEDED(SHCreateItemFromParsingName(entry.path.c_str(),nullptr,__uuidof(IShellItemImageFactory),reinterpret_cast<void**>(&factory.p)))){HBITMAP bitmap=nullptr;if(SUCCEEDED(factory->GetImage({112,84},SIIGBF_THUMBNAILONLY,&bitmap))&&bitmap)entry.thumbnail=std::shared_ptr<void>(bitmap,[](void* h){DeleteObject(static_cast<HBITMAP>(h));});}result.push_back(std::move(entry));
    }return result;
}
std::string capture_gallery(){
    std::vector<std::filesystem::directory_entry> files;for(const auto& entry:std::filesystem::directory_iterator(capture_folder())){auto extension=lower(utf8(entry.path().extension().wstring()));if(entry.is_regular_file()&&(extension==".png"||extension==".mp4"))files.push_back(entry);}std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return a.last_write_time()>b.last_write_time();});std::ostringstream out;out<<"LOCAL CAPTURE GALLERY\nOriginal files are preserved by crop/export operations.\n\n";for(size_t i=0;i<std::min<size_t>(200,files.size());++i)out<<utf8(files[i].path().filename().wstring())<<" / "<<files[i].file_size()/1024<<" KB\n"<<utf8(files[i].path().wstring())<<"\n\n";if(files.empty())out<<"No PNG/MP4 captures found yet.";return out.str();
}
std::vector<Drive> drives(){
    wchar_t names[512]{};if(!GetLogicalDriveStringsW(512,names))throw std::runtime_error(error());
    std::vector<Drive> out;
    for(auto root=names;*root;root+=wcslen(root)+1){
        if(GetDriveTypeW(root)!=DRIVE_FIXED)continue;
        ULARGE_INTEGER free{},total{};wchar_t fs[64]{};
        if(!GetDiskFreeSpaceExW(root,&free,&total,nullptr))continue;
        GetVolumeInformationW(root,nullptr,0,nullptr,nullptr,nullptr,fs,64);
        out.push_back({root,fs,free.QuadPart,total.QuadPart});
    }
    return out;
}
void optimize_drive(HWND owner,const std::wstring& root,bool analyze){
    if(root.size()!=3 || root[1]!=L':' || root[2]!=L'\\' || root[0]<L'A' || root[0]>L'Z' || GetDriveTypeW(root.c_str())!=DRIVE_FIXED)throw std::runtime_error("Select a fixed drive");
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);
    auto exe=std::filesystem::path(system)/L"defrag.exe";
    auto parameters=root.substr(0,2)+(analyze?L" /A /V":L" /O /U /V");
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof info;info.hwnd=owner;info.lpVerb=L"runas";info.lpFile=exe.c_str();info.lpParameters=parameters.c_str();info.nShow=SW_SHOWNORMAL;
    if(!ShellExecuteExW(&info))throw std::runtime_error(error());
}
void disk_cleanup(){
    wchar_t system[MAX_PATH]{};GetSystemDirectoryW(system,MAX_PATH);shell((std::filesystem::path(system)/L"cleanmgr.exe").wstring());
}
}
