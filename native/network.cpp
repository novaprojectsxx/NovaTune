#include <winsock2.h>
#include <ws2tcpip.h>
#include "network.hpp"
#include "windows.hpp"
#include <windows.h>
#include <winhttp.h>
#include <iphlpapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <vector>
namespace hr {
namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start){return std::chrono::duration<double>(Clock::now()-start).count();}
struct Internet {HINTERNET h=nullptr;explicit Internet(HINTERNET value):h(value){if(!h)throw std::runtime_error("Windows HTTP error "+std::to_string(GetLastError()));}~Internet(){WinHttpCloseHandle(h);}Internet(const Internet&)=delete;};
void check(BOOL value){if(!value)throw std::runtime_error("Windows HTTP error "+std::to_string(GetLastError())+". Check the connection, proxy or firewall and retry.");}
void status(HINTERNET request){DWORD code=0,size=sizeof code;check(WinHttpQueryHeaders(request,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&code,&size,WINHTTP_NO_HEADER_INDEX));if(code<200||code>=300)throw std::runtime_error("Test server returned HTTP "+std::to_string(code)+". No speed is inferred from an error response.");}
std::string value(double n,const char* unit){if(n<0)return "Not measured";std::ostringstream out;out<<std::fixed<<std::setprecision(2)<<n<<unit;return out.str();}
}
double network_mbps(unsigned long long bytes,double duration){if(!std::isfinite(duration)||duration<=0)throw std::invalid_argument("Measurement duration must be positive and finite");return static_cast<double>(bytes)*8.0/duration/1000000.0;}
std::string internet_speed(bool standard,std::atomic<bool>& cancel,const std::function<void(const NetworkProgress&)>& notify){
    NetworkProgress state;auto publish=[&](const std::string& phase){state.phase=phase;if(notify)notify(state);};std::string failure;
    try{
        if(cancel){publish("Cancelled");return "Speed test cancelled before connecting.";}
        Internet session(WinHttpOpen(L"NovaTune/2 NetworkTest",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
        check(WinHttpSetTimeouts(session.h,3000,5000,5000,3000));Internet connection(WinHttpConnect(session.h,L"speed.cloudflare.com",INTERNET_DEFAULT_HTTPS_PORT,0));
        auto request=[&](unsigned bytes,bool upload,bool latency)->double{
            if(cancel)return 0;auto path=upload?L"/__up":L"/__down?bytes="+std::to_wstring(bytes)+L"&headroom="+std::to_wstring(GetTickCount64());
            Internet req(WinHttpOpenRequest(connection.h,upload?L"POST":L"GET",path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE|WINHTTP_FLAG_REFRESH));
            DWORD disable=WINHTTP_DISABLE_COOKIES|WINHTTP_DISABLE_REDIRECTS;check(WinHttpSetOption(req.h,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof disable));
            std::vector<unsigned char> data(upload?bytes:65536);if(upload&&BCryptGenRandom(nullptr,data.data(),bytes,BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw std::runtime_error("Cannot generate upload test data");
            auto start=Clock::now();check(WinHttpSendRequest(req.h,L"Cache-Control: no-cache\r\nContent-Type: application/octet-stream\r\n",static_cast<DWORD>(-1),upload?data.data():nullptr,upload?bytes:0,upload?bytes:0,0));check(WinHttpReceiveResponse(req.h,nullptr));status(req.h);double headers=seconds(start);unsigned long long received=0;
            while(!cancel){DWORD got=0;check(WinHttpReadData(req.h,data.data(),static_cast<DWORD>(data.size()),&got));if(!got)break;received+=got;if(received>(upload?65536ULL:bytes))throw std::runtime_error("Unexpected speed-test response size");if(!upload&&!latency){state.bytes+=got;state.mbps=network_mbps(received,seconds(start));if(notify)notify(state);}}
            if(cancel)return 0;if(!upload&&received!=bytes)throw std::runtime_error("Incomplete test download; a partial transfer is not a completed speed sample");if(upload)state.bytes+=bytes;return latency?headers:seconds(start);
        };
        publish("Connecting");request(65536,false,false);state.mbps=0;
        std::vector<double> latencies;publish("Latency");for(int n=0;n<5&&!cancel;++n)latencies.push_back(request(0,false,true)*1000);
        if(!cancel&&!latencies.empty()){auto sorted=latencies;std::sort(sorted.begin(),sorted.end());state.latency=sorted[sorted.size()/2];double differences=0;for(size_t n=1;n<latencies.size();++n)differences+=std::abs(latencies[n]-latencies[n-1]);state.jitter=differences/(latencies.size()-1);}
        for(bool upload:{false,true}){if(cancel)break;state.mbps=0;publish(upload?"Upload":"Download");auto start=Clock::now();unsigned long long transferred=0,limit=standard?(upload?16ULL:64ULL)*1024*1024:(upload?4ULL:8ULL)*1024*1024;
            while(!cancel&&transferred<limit&&seconds(start)<12){auto chunk=static_cast<unsigned>(std::min(1024ULL*1024,limit-transferred));request(chunk,upload,false);if(cancel)break;transferred+=chunk;state.mbps=network_mbps(transferred,seconds(start));if(upload)state.upload=state.mbps;else state.download=state.mbps;if(notify)notify(state);}
        }
    }catch(const std::exception& e){failure=e.what();}
    publish(cancel?"Cancelled":failure.empty()?"Complete":"Failed");std::ostringstream out;out<<"INTERNET SPEED / "<<state.phase<<"\n\nDownload: "<<value(state.download," Mbps")<<"\nUpload: "<<value(state.upload," Mbps")<<"\nHTTP latency (median): "<<value(state.latency," ms")<<"\nHTTP successive-sample jitter: "<<value(state.jitter," ms")<<"\nObserved/verified test payload: "<<state.bytes<<" bytes\n";
    if(!failure.empty())out<<"\n"<<failure<<"\n";out<<"\nServer: speed.cloudflare.com, HTTPS. Single-stream application throughput includes request overhead; not an ISP line-rate certification. Latency is HTTPS response time, not ICMP/game ping. Partial/cancelled results are not a completed test.\nNo files, browsing history or microphone data are uploaded; upload payload is generated in memory. Results stay in NovaTune locally. The server sees the connection IP.\n";return out.str();
}
std::string network_health(){
    ULONG size=16384,status_code=0;std::vector<unsigned char> buffer(size);for(int tries=0;tries<3;++tries){status_code=GetAdaptersAddresses(AF_UNSPEC,GAA_FLAG_INCLUDE_GATEWAYS,nullptr,reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data()),&size);if(status_code!=ERROR_BUFFER_OVERFLOW)break;buffer.resize(size);}if(status_code!=NO_ERROR)throw std::runtime_error("Cannot inspect adapters: "+std::to_string(status_code));
    std::ostringstream out;out<<"NETWORK OPTIMIZER / LOCAL DIAGNOSIS\n\n";unsigned active=0;for(auto a=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());a;a=a->Next){if(a->IfType==IF_TYPE_SOFTWARE_LOOPBACK)continue;bool up=a->OperStatus==IfOperStatusUp;out<<utf8(a->FriendlyName)<<" / "<<(up?"Connected":"Disconnected")<<"\n";if(!up)continue;++active;MIB_IF_ROW2 row{};row.InterfaceLuid=a->Luid;if(GetIfEntry2(&row)==NO_ERROR)out<<"Link receive / transmit: "<<row.ReceiveLinkSpeed/1000000<<" / "<<row.TransmitLinkSpeed/1000000<<" Mbps (local adapter, not internet speed)\n";
        unsigned dns=0;for(auto d=a->FirstDnsServerAddress;d;d=d->Next)++dns;out<<"DNS servers configured: "<<dns<<". Default gateway: "<<(a->FirstGatewayAddress?"present":"not reported")<<"\n";
        if(!dns)out<<"Suggested fix: check DNS configuration in Windows network settings.\n";if(a->IfType==IF_TYPE_IEEE80211)out<<"Suggestion: compare Ethernet or a stronger Wi-Fi signal before changing Windows settings.\n";out<<"\n";
    }
    if(!active)out<<"No active adapter found. Connect to a network, then retest.\n";out<<"AVAILABLE REPAIRS\nFlush DNS: clears cached name lookups; useful for stale DNS, not a bandwidth boost.\nAdvanced repair: review Winsock reset in System tools; it can affect VPN software and needs a restart.\nClose downloads/streams and compare before/after speed tests on the same connection.\nNo DNS server, TCP registry, MTU, adapter power setting or security setting is changed by inspection.\n";return out.str();
}
}
