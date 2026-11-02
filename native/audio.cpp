#include "audio.hpp"
#include "capture.hpp"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
namespace hr {
                                                                            
struct AudioPolicy : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR,INT,WAVEFORMATEX**)=0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR,WAVEFORMATEX*,WAVEFORMATEX*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR,INT,INT64*,INT64*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR,INT64*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR,void*)=0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(PCWSTR,INT,const PROPERTYKEY&,PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(PCWSTR,INT,const PROPERTYKEY&,const PROPVARIANT*)=0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR,ERole)=0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR,INT)=0;
};
namespace {
void check(HRESULT code,const char* text){if(FAILED(code))throw std::runtime_error(std::string(text)+": "+error(static_cast<DWORD>(code)));}
struct Apartment {HRESULT result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);Apartment(){if(FAILED(result)&&result!=RPC_E_CHANGED_MODE)check(result,"Audio COM");}~Apartment(){if(SUCCEEDED(result))CoUninitialize();}};
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
void enumerator(Com<IMMDeviceEnumerator>& value){check(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,__uuidof(IMMDeviceEnumerator),reinterpret_cast<void**>(&value.p)),"Audio devices");}
const PROPERTYKEY friendly_name={{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};
const GUID pcm_subtype={1,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
const GUID float_subtype={3,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
const GUID policy_class={0x870af99c,0x171d,0x4f9e,{0xaf,0x0d,0xe6,0x3d,0xf4,0x0c,0x2b,0xc9}};
const GUID policy_interface={0xf8679f50,0x850a,0x41cf,{0x9c,0x72,0x43,0x0f,0x29,0x02,0x90,0xc8}};
                                                                          
                                                                              

bool float_format(const WAVEFORMATEX& f){if(f.wFormatTag==WAVE_FORMAT_IEEE_FLOAT)return true;if(f.wFormatTag==WAVE_FORMAT_EXTENSIBLE&&f.cbSize>=22){auto& extended=reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(f);return IsEqualGUID(extended.SubFormat,float_subtype);}return false;}
bool pcm_format(const WAVEFORMATEX& f){if(f.wFormatTag==WAVE_FORMAT_PCM)return true;if(f.wFormatTag==WAVE_FORMAT_EXTENSIBLE&&f.cbSize>=22)return IsEqualGUID(reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(f).SubFormat,pcm_subtype);return false;}
void write_wav(const std::filesystem::path& file,const WAVEFORMATEX& format,const std::vector<BYTE>& audio){
    DWORD format_bytes=sizeof(WAVEFORMATEX)+format.cbSize,padding=format_bytes&1,data_bytes=static_cast<DWORD>(audio.size()),size=4+8+format_bytes+padding+12+8+data_bytes+(data_bytes&1);std::ofstream output(file,std::ios::binary|std::ios::trunc);if(!output)throw std::runtime_error("Cannot create microphone recording");auto tag=[&](const char* text){output.write(text,4);};auto integer=[&](DWORD value){output.write(reinterpret_cast<const char*>(&value),4);};tag("RIFF");integer(size);tag("WAVE");tag("fmt ");integer(format_bytes);output.write(reinterpret_cast<const char*>(&format),format_bytes);if(padding)output.put(0);tag("fact");integer(4);integer(data_bytes/format.nBlockAlign);tag("data");integer(data_bytes);output.write(reinterpret_cast<const char*>(audio.data()),audio.size());if(data_bytes&1)output.put(0);output.flush();if(!output)throw std::runtime_error("Microphone recording write failed");
}
}
std::vector<AudioEndpoint> audio_endpoints(){Apartment apartment;Com<IMMDeviceEnumerator> devices;enumerator(devices);std::vector<AudioEndpoint> result;for(auto flow:{eRender,eCapture}){Com<IMMDeviceCollection> collection;check(devices->EnumAudioEndpoints(flow,DEVICE_STATE_ACTIVE,&collection.p),"Enumerate audio endpoints");UINT count=0;collection->GetCount(&count);for(UINT n=0;n<count;++n){Com<IMMDevice> device;if(FAILED(collection->Item(n,&device.p)))continue;LPWSTR id=nullptr;if(FAILED(device->GetId(&id)))continue;AudioEndpoint entry{utf8(id),utf8(id),flow==eCapture};CoTaskMemFree(id);Com<IPropertyStore> properties;if(SUCCEEDED(device->OpenPropertyStore(STGM_READ,&properties.p))){PROPVARIANT value{};if(SUCCEEDED(properties->GetValue(friendly_name,&value))&&value.vt==VT_LPWSTR)entry.name=utf8(value.pwszVal);PropVariantClear(&value);}result.push_back(std::move(entry));}}return result;}
std::string default_audio(bool capture,int role){if(role<0||role>2)throw std::runtime_error("Invalid audio role");Apartment apartment;Com<IMMDeviceEnumerator> devices;enumerator(devices);Com<IMMDevice> device;auto code=devices->GetDefaultAudioEndpoint(capture?eCapture:eRender,static_cast<ERole>(role),&device.p);if(code==E_NOTFOUND)return {};check(code,"Read default audio endpoint");LPWSTR id=nullptr;check(device->GetId(&id),"Audio endpoint ID");std::string result=utf8(id);CoTaskMemFree(id);return result;}
bool audio_switch_available(){Apartment apartment;Com<AudioPolicy> policy;return SUCCEEDED(CoCreateInstance(policy_class,nullptr,CLSCTX_ALL,policy_interface,reinterpret_cast<void**>(&policy.p)));}
void set_default_audio(const std::string& id,bool capture,int role){
    if(id.empty()||id.size()>4096||role<0||role>2)throw std::runtime_error("Choose a connected audio device and role");auto devices=audio_endpoints();if(std::none_of(devices.begin(),devices.end(),[&](const AudioEndpoint& device){return device.id==id&&device.capture==capture;}))throw std::runtime_error("Selected audio device is disconnected or has the wrong direction");Apartment apartment;Com<AudioPolicy> policy;check(CoCreateInstance(policy_class,nullptr,CLSCTX_ALL,policy_interface,reinterpret_cast<void**>(&policy.p)),"Default-device switching unavailable on this Windows build");check(policy->SetDefaultEndpoint(wide(id).c_str(),static_cast<ERole>(role)),"Switch default audio device");if(default_audio(capture,role)!=id)throw std::runtime_error("Windows did not retain the requested audio endpoint");
}
AudioLevels measure_audio(const BYTE* bytes,size_t frames,const WAVEFORMATEX& format,bool silent){
    if(!format.nChannels||format.nChannels>32||format.wBitsPerSample%8||format.nBlockAlign!=format.nChannels*(format.wBitsPerSample/8))throw std::runtime_error("Unsupported microphone frame layout");bool floating=float_format(format),pcm=pcm_format(format);if((!floating&&!pcm)||(floating&&format.wBitsPerSample!=32)||(pcm&&format.wBitsPerSample!=16&&format.wBitsPerSample!=24&&format.wBitsPerSample!=32))throw std::runtime_error("Microphone meter supports float32 or PCM 16/24/32");AudioLevels result;result.samples=frames*format.nChannels;if(silent||!result.samples)return result;if(!bytes)throw std::runtime_error("Missing microphone samples");double sum=0;size_t width=format.wBitsPerSample/8;for(size_t n=0;n<result.samples;++n){const BYTE* input=bytes+n*width;double value=0;if(floating){float sample;memcpy(&sample,input,4);value=std::isfinite(sample)?sample:0;}else if(width==2){int16_t sample;memcpy(&sample,input,2);value=sample/32768.0;}else if(width==3){int32_t sample=input[0]|(input[1]<<8)|(input[2]<<16);if(sample&0x800000)sample|=static_cast<int32_t>(0xff000000);value=sample/8388608.0;}else{int32_t sample;memcpy(&sample,input,4);value=sample/2147483648.0;}value=std::abs(value);result.peak=std::max(result.peak,value);sum+=value*value;if(value>=.99)++result.clipped;}result.rms=std::sqrt(sum/result.samples);return result;
}
MicrophoneTest::~MicrophoneTest(){stop();}
AudioLevels MicrophoneTest::reading()const{std::lock_guard<std::mutex> lock(mutex);return levels;}
std::string MicrophoneTest::error()const{std::lock_guard<std::mutex> lock(mutex);return problem;}
std::filesystem::path MicrophoneTest::file()const{std::lock_guard<std::mutex> lock(mutex);return output;}
void MicrophoneTest::stop(){stopping=true;if(worker.joinable())worker.join();running=false;}
void MicrophoneTest::start(const std::string& endpoint,unsigned seconds){
    if(active())throw std::runtime_error("Microphone test is already recording");if(seconds<1||seconds>60)throw std::runtime_error("Microphone test duration must be 1 to 60 seconds");stop();stopping=false;running=true;{std::lock_guard<std::mutex> lock(mutex);problem.clear();levels={};output.clear();}
    worker=std::thread([this,endpoint,seconds]{try{Apartment apartment;Com<IMMDeviceEnumerator> devices;enumerator(devices);Com<IMMDevice> device;if(endpoint.empty())check(devices->GetDefaultAudioEndpoint(eCapture,eCommunications,&device.p),"No default microphone; check Windows microphone access");else{check(devices->GetDevice(wide(endpoint).c_str(),&device.p),"Selected microphone");Com<IMMEndpoint> direction;check(device->QueryInterface(__uuidof(IMMEndpoint),reinterpret_cast<void**>(&direction.p)),"Microphone direction");EDataFlow flow{};check(direction->GetDataFlow(&flow),"Microphone direction");if(flow!=eCapture)throw std::runtime_error("Choose a recording device");}
        Com<IAudioClient> client;check(device->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,reinterpret_cast<void**>(&client.p)),"Microphone capture");WAVEFORMATEX* format=nullptr;check(client->GetMixFormat(&format),"Microphone format");struct Free{WAVEFORMATEX* p;~Free(){CoTaskMemFree(p);}} free_format{format};measure_audio(nullptr,0,*format,true);if(format->nSamplesPerSec<8000||format->nSamplesPerSec>384000||format->nAvgBytesPerSec>4194304||static_cast<unsigned long long>(format->nAvgBytesPerSec)*seconds>64ULL*1024*1024)throw std::runtime_error("Microphone format exceeds recording limit");check(client->Initialize(AUDCLNT_SHAREMODE_SHARED,0,1000000,0,format,nullptr),"Microphone access (check Windows privacy permission)");Com<IAudioCaptureClient> capture;check(client->GetService(__uuidof(IAudioCaptureClient),reinterpret_cast<void**>(&capture.p)),"Microphone stream");std::vector<BYTE> audio;audio.reserve(static_cast<size_t>(format->nAvgBytesPerSec)*seconds);check(client->Start(),"Start microphone");struct Stop{IAudioClient* p;~Stop(){p->Stop();}} stop_client{client.p};began=GetTickCount64();unsigned long long all_samples=0,all_clipped=0;double squared=0,highest=0;
        while(!stopping&&GetTickCount64()-began<seconds*1000ULL){UINT32 pending=0;check(capture->GetNextPacketSize(&pending),"Microphone packet");while(pending){BYTE* data=nullptr;UINT32 frames=0;DWORD flags=0;check(capture->GetBuffer(&data,&frames,&flags,nullptr,nullptr),"Microphone buffer");try{auto measured=measure_audio(data,frames,*format,(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0);size_t length=static_cast<size_t>(frames)*format->nBlockAlign;if(audio.size()+length>static_cast<size_t>(format->nAvgBytesPerSec)*seconds+format->nAvgBytesPerSec)throw std::runtime_error("Microphone recording limit reached");size_t previous=audio.size();audio.resize(previous+length);if(flags&AUDCLNT_BUFFERFLAGS_SILENT)memset(audio.data()+previous,0,length);else memcpy(audio.data()+previous,data,length);all_samples+=measured.samples;all_clipped+=measured.clipped;squared+=measured.rms*measured.rms*measured.samples;highest=std::max(highest,measured.peak);{std::lock_guard<std::mutex> lock(mutex);levels={measured.peak,measured.rms,all_samples,all_clipped};}}catch(...){capture->ReleaseBuffer(frames);throw;}check(capture->ReleaseBuffer(frames),"Release microphone buffer");check(capture->GetNextPacketSize(&pending),"Microphone packet");}Sleep(15);}
        client->Stop();if(!audio.empty()){static std::atomic<unsigned> sequence{0};auto path=capture_folder()/(L"Microphone-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64())+L"-"+std::to_wstring(++sequence)+L".wav");write_wav(path,*format,audio);std::lock_guard<std::mutex> lock(mutex);output=path;levels={highest,all_samples?std::sqrt(squared/all_samples):0,all_samples,all_clipped};}
    }catch(const std::exception& e){std::lock_guard<std::mutex> lock(mutex);problem=e.what();}running=false;});
}
}
