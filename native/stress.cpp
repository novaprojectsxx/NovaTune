#include "stress.hpp"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>
namespace hr {
namespace {
template<class T>struct Com{T* p=nullptr;~Com(){if(p)p->Release();}T* operator->()const{return p;}};
uint32_t mix(uint32_t value,unsigned rounds){for(unsigned i=0;i<rounds;++i){value=(value<<5)|(value>>27);value^=0x9e3779b9u;value=value*1664525u+1013904223u;}return value;}
uint32_t unmix(uint32_t value,unsigned rounds){for(unsigned i=0;i<rounds;++i){value=(value-1013904223u)*4276115653u;value^=0x9e3779b9u;value=(value>>5)|(value<<27);}return value;}
}
StressTest::~StressTest(){stop();}
void StressTest::stop(){stopping=true;if(worker.joinable())worker.join();running=false;}
StressSnapshot StressTest::snapshot()const{
    std::lock_guard<std::mutex> lock(mutex);
    return {running.load(),elapsed.load(),duration,workers,cpu.load(),ram.load(),gpu.load(),errors.load(),gpu_status};
}
void StressTest::start(unsigned seconds,bool cpu_load,bool ram_load,bool gpu_load){
    if(running)throw std::runtime_error("A stress test is already running");
    if(seconds<5||seconds>300||(!cpu_load&&!ram_load&&!gpu_load))throw std::runtime_error("Choose a load and a duration of 5-300 seconds");
    stop();stopping=false;cpu=0;ram=0;gpu=0;errors=0;elapsed=0;
    {std::lock_guard<std::mutex> lock(mutex);duration=seconds;
        unsigned processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);workers=cpu_load?std::clamp(processors,1u,64u):0;
        gpu_status=gpu_load?"Starting GPU compute test":"GPU test off";}
    running=true;
    worker=std::thread([this,cpu_load,ram_load,gpu_load]{
        std::vector<std::thread> tasks;
        try{
        if(cpu_load)for(unsigned n=0;n<workers;++n)tasks.emplace_back([this,n]{
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
            volatile uint32_t output=0x12345678u+n;
            while(!stopping){uint32_t input=output;output=mix(input,65536);auto observed=output;if(unmix(observed,65536)!=input)++errors;++cpu;}
        });
        if(ram_load)tasks.emplace_back([this]{
            try{
                MEMORYSTATUSEX info{};info.dwLength=sizeof info;GlobalMemoryStatusEx(&info);
                size_t bytes=static_cast<size_t>(std::min<unsigned long long>(256ULL*1024*1024,info.ullAvailPhys/8));
                if(bytes<1024*1024)throw std::runtime_error("Insufficient free RAM for a bounded memory test");
                std::vector<uint64_t> memory(bytes/sizeof(uint64_t));uint64_t pattern=0x55aa55aa55aa55aaULL;
                while(!stopping){
                    for(size_t i=0;i<memory.size();++i){memory[i]=pattern^i;if((i&65535)==0 && stopping)return;}
                    for(size_t i=0;i<memory.size();++i){if(memory[i]!=(pattern^i))++errors;if((i&65535)==0 && stopping)return;}
                    ram+=memory.size()*sizeof(uint64_t);pattern=~pattern;
                }
            }catch(const std::exception& e){++errors;std::lock_guard<std::mutex> lock(mutex);gpu_status+=std::string(" | RAM: ")+e.what();}
        });
        if(gpu_load)tasks.emplace_back([this]{
            auto status=[this](const std::string& text){std::lock_guard<std::mutex> lock(mutex);gpu_status=text;};
            try{
                Com<ID3D11Device> device;Com<ID3D11DeviceContext> context;
                D3D_FEATURE_LEVEL requested=D3D_FEATURE_LEVEL_11_0,level{};
                auto result=D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,&requested,1,D3D11_SDK_VERSION,&device.p,&level,&context.p);
                if(FAILED(result)){status("GPU compute unavailable on this adapter. CPU/RAM tests continue.");return;}
                Com<ID3DBlob> shader,diagnostics;
                const char* source="RWStructuredBuffer<uint> output : register(u0); [numthreads(64,1,1)] void main(uint3 id:SV_DispatchThreadID){uint v=id.x+1; [loop] for(uint i=0;i<1024;i++) v=v*1664525u+1013904223u; output[id.x]=v;}";
                result=D3DCompile(source,strlen(source),nullptr,nullptr,nullptr,"main","cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader.p,&diagnostics.p);
                if(FAILED(result))throw std::runtime_error("GPU stress shader could not compile");
                Com<ID3D11ComputeShader> compute;
                if(FAILED(device->CreateComputeShader(shader->GetBufferPointer(),shader->GetBufferSize(),nullptr,&compute.p)))throw std::runtime_error("GPU compute shader unavailable");
                constexpr UINT count=16384;
                D3D11_BUFFER_DESC desc{};desc.ByteWidth=count*sizeof(uint32_t);desc.Usage=D3D11_USAGE_DEFAULT;desc.BindFlags=D3D11_BIND_UNORDERED_ACCESS;desc.MiscFlags=D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;desc.StructureByteStride=sizeof(uint32_t);
                Com<ID3D11Buffer> buffer;
                if(FAILED(device->CreateBuffer(&desc,nullptr,&buffer.p)))throw std::runtime_error("GPU stress buffer unavailable");
                D3D11_UNORDERED_ACCESS_VIEW_DESC view{};view.Format=DXGI_FORMAT_UNKNOWN;view.ViewDimension=D3D11_UAV_DIMENSION_BUFFER;view.Buffer.NumElements=count;
                Com<ID3D11UnorderedAccessView> uav;
                if(FAILED(device->CreateUnorderedAccessView(buffer.p,&view,&uav.p)))throw std::runtime_error("GPU stress view unavailable");
                D3D11_QUERY_DESC query_desc{D3D11_QUERY_EVENT,0};Com<ID3D11Query> query;
                if(FAILED(device->CreateQuery(&query_desc,&query.p)))throw std::runtime_error("GPU timing query unavailable");
                context->CSSetShader(compute.p,nullptr,0);context->CSSetUnorderedAccessViews(0,1,&uav.p,nullptr);status("Hardware GPU compute active");
                while(!stopping){
                    context->Dispatch(count/64,1,1);context->End(query.p);context->Flush();
                    auto start=GetTickCount64();BOOL complete=FALSE;
                    while(context->GetData(query.p,&complete,sizeof complete,0)==S_FALSE){
                        if(stopping)return;
                        if(GetTickCount64()-start>5000 || FAILED(device->GetDeviceRemovedReason()))throw std::runtime_error("GPU compute stopped or timed out");
                        Sleep(2);
                    }
                    ++gpu;
                }
                context->CSSetShader(nullptr,nullptr,0);
                status("Hardware GPU compute completed");
            }catch(const std::exception& e){++errors;status(e.what());}
        });
        auto start=std::chrono::steady_clock::now();
        while(!stopping){
            auto seconds=static_cast<unsigned>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now()-start).count());
            elapsed=seconds;if(seconds>=duration){stopping=true;break;}Sleep(50);
        }
        }catch(const std::exception& e){++errors;stopping=true;std::lock_guard<std::mutex> lock(mutex);gpu_status+=std::string(" | Worker startup: ")+e.what();}
        for(auto& task:tasks)if(task.joinable())task.join();
        running=false;
    });
}
}
