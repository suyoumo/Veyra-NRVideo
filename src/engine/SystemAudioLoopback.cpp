#include "veyra/engine/SystemAudioLoopback.h"

// The audio client headers come before AudioFormat.h (ks.h), as in
// engine/MicrophoneCapture.cpp.
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>

#include "veyra/sink/AudioFormat.h"
#include "veyra/sink/RecordingAudioTap.h"
#include "veyra/Log.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <format>
#include <mutex>
#include <thread>

extern "C" {
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace veyra::engine {
using Microsoft::WRL::ComPtr;
namespace {

constexpr int kRate=48000;
const PROPERTYKEY kFriendlyName{{0xa45c254e,0xdf1c,0x4efd,{0x80,0x20,0x67,0xd1,0x46,0xa8,0x50,0xe0}},14};

std::string utf8(const std::wstring& text){
    if(text.empty())return {};
    const int size=WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
    std::string out(size_t(size),'\0');
    WideCharToMultiByte(CP_UTF8,0,text.data(),int(text.size()),out.data(),size,nullptr,nullptr);
    return out;
}
struct ComScope {HRESULT hr=CoInitializeEx(nullptr,COINIT_MULTITHREADED);~ComScope(){if(SUCCEEDED(hr))CoUninitialize();}};
struct Handle {HANDLE value=nullptr;~Handle(){if(value)CloseHandle(value);}};
std::wstring friendlyName(IMMDevice* device){
    std::wstring name;
    ComPtr<IPropertyStore> properties;
    if(FAILED(device->OpenPropertyStore(STGM_READ,&properties)))return name;
    PROPVARIANT value;PropVariantInit(&value);
    if(SUCCEEDED(properties->GetValue(kFriendlyName,&value))&&value.vt==VT_LPWSTR&&value.pwszVal)name=value.pwszVal;
    PropVariantClear(&value);
    return name;
}
std::wstring describe(HRESULT hr){
    if(hr==AUDCLNT_E_DEVICE_INVALIDATED||hr==HRESULT_FROM_WIN32(ERROR_NOT_FOUND)||hr==E_NOTFOUND)
        return L"没有可用的播放设备，无法录制系统声音";
    if(hr==AUDCLNT_E_UNSUPPORTED_FORMAT)return L"播放设备的格式不支持系统声音回环";
    return std::format(L"系统声音回环无法打开（0x{:08X}）",uint32_t(hr));
}

// Mix-format packets to interleaved float stereo at the endpoint's rate.
void toStereo(const BYTE* data,UINT frames,const sink::WavePcmFormat& format,std::vector<float>& out){
    const unsigned channels=format.layout.channels,bytes=format.wave.wBitsPerSample/8;
    out.resize(size_t(frames)*2);
    auto sample=[&](const BYTE* p)->float{
        if(format.floating){float v;std::memcpy(&v,p,4);return std::isfinite(v)?v:0.0f;}
        if(bytes==2){int16_t v;std::memcpy(&v,p,2);return float(v)/32768.0f;}
        if(bytes==3){const int32_t v=int32_t(uint32_t(p[0])<<8|uint32_t(p[1])<<16|uint32_t(p[2])<<24)>>8;return float(v)/8388608.0f;}
        int32_t v;std::memcpy(&v,p,4);return float(double(v)/2147483648.0);
    };
    for(UINT i=0;i<frames;++i){
        const BYTE* frame=data+size_t(i)*format.wave.nBlockAlign;
        const float left=sample(frame);
        out[size_t(i)*2]=left;
        out[size_t(i)*2+1]=channels>1?sample(frame+bytes):left;
    }
}

struct Hub {
    std::mutex m;
    std::condition_variable cv;
    std::vector<std::shared_ptr<SystemAudioSink>> sinks;
    uint64_t generation=0;
    std::atomic<uint64_t> current{0};
    SystemAudioStatus status;
    bool started=false;

    void deliver(const float* pcm,size_t frames,int64_t capturedUs){
        std::lock_guard lock(m);
        for(const auto& sink:sinks)sink->systemPcm(pcm,frames,capturedUs);
        ++status.packets;
    }
    bool wanted(uint64_t gen) const {return current.load()==gen;}

    HRESULT stream(uint64_t gen){
        ComPtr<IMMDeviceEnumerator> enumerator;ComPtr<IMMDevice> device;ComPtr<IAudioClient> client;ComPtr<IAudioCaptureClient> capture;
        HRESULT hr=CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&enumerator));
        if(FAILED(hr))return hr;
        hr=enumerator->GetDefaultAudioEndpoint(eRender,eConsole,&device);
        if(FAILED(hr))return hr;
        const std::wstring name=friendlyName(device.Get());
        {std::lock_guard lock(m);status.device=name.empty()?L"WASAPI playback endpoint":name;}
        hr=device->Activate(__uuidof(IAudioClient),CLSCTX_INPROC_SERVER,nullptr,reinterpret_cast<void**>(client.GetAddressOf()));
        if(FAILED(hr))return hr;
        WAVEFORMATEX* raw=nullptr;
        hr=client->GetMixFormat(&raw);
        if(FAILED(hr))return hr;
        auto freeWave=[](WAVEFORMATEX* value){CoTaskMemFree(value);};
        std::unique_ptr<WAVEFORMATEX,decltype(freeWave)> wave(raw,freeWave);
        sink::WavePcmFormat format;
        if(!raw||!sink::parseWavePcm(raw,sizeof(WAVEFORMATEX)+raw->cbSize,format))return AUDCLNT_E_UNSUPPORTED_FORMAT;
        // Loopback from the endpoint everything plays to; a shared-mode stream.
        hr=client->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK,200000,0,raw,nullptr);
        if(FAILED(hr))return hr;
        Handle ready;ready.value=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!ready.value)return HRESULT_FROM_WIN32(GetLastError());
        if(FAILED(hr=client->SetEventHandle(ready.value)))return hr;
        if(FAILED(hr=client->GetService(IID_PPV_ARGS(&capture))))return hr;
        const unsigned rate=raw->nSamplesPerSec;
        SwrContext* swr=nullptr;
        struct FreeSwr {SwrContext** swr;~FreeSwr(){if(*swr)swr_free(swr);}} freeSwr{&swr};
        if(rate!=unsigned(kRate)){
            const AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
            if(swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_FLT,kRate,&stereo,AV_SAMPLE_FMT_FLT,int(rate),0,nullptr)<0||!swr||swr_init(swr)<0)
                return AUDCLNT_E_UNSUPPORTED_FORMAT;
        }
        LARGE_INTEGER frequency{},counter{};
        QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&counter);
        const int64_t offsetUs=sink::recordingClockUs()-int64_t(double(counter.QuadPart)*1e6/double(std::max<LONGLONG>(1,frequency.QuadPart)));
        if(FAILED(hr=client->Start()))return hr;
        struct StopClient {IAudioClient* client;~StopClient(){client->Stop();}} stopClient{client.Get()};
        {std::lock_guard lock(m);status.open=true;status.error.clear();status.rate=rate;status.channels=raw->nChannels;++status.opens;}
        veyra::log::info("system-audio",std::format("loopback open device={} rate={} channels={} bits={} float={} resample={}",
            utf8(name),rate,raw->nChannels,raw->wBitsPerSample,format.floating,swr!=nullptr));
        std::vector<float> stereo,resampled;
        int64_t nextUs=0;bool haveNext=false;
        while(wanted(gen)){
            const DWORD wait=WaitForSingleObject(ready.value,100);
            if(wait==WAIT_FAILED)return HRESULT_FROM_WIN32(GetLastError());
            UINT next=0;
            if(FAILED(hr=capture->GetNextPacketSize(&next)))return hr;
            // Silence yields no packets; that is normal here, never a timeout.
            while(next&&wanted(gen)){
                BYTE* data=nullptr;UINT frames=0;DWORD flags=0;UINT64 position=0,qpc=0;
                hr=capture->GetBuffer(&data,&frames,&flags,&position,&qpc);
                if(FAILED(hr))return hr;
                if(hr==AUDCLNT_S_BUFFER_EMPTY)break;
                if(frames){
                    if((flags&AUDCLNT_BUFFERFLAGS_SILENT)||!data){stereo.assign(size_t(frames)*2,0.0f);}
                    else toStereo(data,frames,format,stereo);
                }
                if(FAILED(hr=capture->ReleaseBuffer(frames)))return hr;
                if(frames){
                    int64_t at=(qpc&&!(flags&AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR))?int64_t(qpc/10)+offsetUs
                              :haveNext?nextUs:sink::recordingClockUs()-int64_t(frames)*1000000/int64_t(rate);
                    nextUs=at+int64_t(frames)*1000000/int64_t(rate);haveNext=true;
                    if(swr){
                        at-=swr_get_delay(swr,int64_t(rate))*1000000/int64_t(rate);
                        const int capacity=swr_get_out_samples(swr,int(frames))+32;
                        resampled.resize(size_t(std::max(capacity,0))*2);
                        uint8_t* out[1]={reinterpret_cast<uint8_t*>(resampled.data())};
                        const uint8_t* in[1]={reinterpret_cast<const uint8_t*>(stereo.data())};
                        const int got=swr_convert(swr,out,capacity,in,int(frames));
                        if(got>0)deliver(resampled.data(),size_t(got),at);
                    }else deliver(stereo.data(),frames,at);
                }
                if(FAILED(hr=capture->GetNextPacketSize(&next)))return hr;
            }
        }
        return S_OK;
    }

    void run(){
        ComScope apartment;
        unsigned attempt=0;
        for(;;){
            uint64_t gen=0;
            {
                std::unique_lock lock(m);
                cv.wait(lock,[&]{return !sinks.empty()||current.load()!=gen;});
                if(sinks.empty()){status.open=false;return;}
                gen=current.load();
            }
            bool retry=false;
            {
                const HRESULT hr=stream(gen);
                std::lock_guard lock(m);
                status.open=false;
                if(FAILED(hr)&&generation==gen){
                    status.error=describe(hr);++status.failures;retry=true;
                    veyra::log::warn("system-audio",std::format("loopback failed hr=0x{:08X} device={} attempt={}",uint32_t(hr),utf8(status.device),attempt+1));
                }else{
                    attempt=0;
                    veyra::log::info("system-audio",std::format("loopback closed hr=0x{:08X} listeners={}",uint32_t(hr),sinks.size()));
                }
            }
            if(retry){
                const auto backoff=std::chrono::milliseconds(std::min(attempt+1,5u)*1000);++attempt;
                std::unique_lock lock(m);
                cv.wait_for(lock,backoff,[&]{return generation!=gen;});
            }
        }
    }
};

Hub& hub(){
    static Hub* instance=new Hub;
    return *instance;
}

} // namespace

void attachSystemAudio(std::shared_ptr<SystemAudioSink> sink){
    if(!sink)return;
    auto& h=hub();
    {
        std::lock_guard lock(h.m);
        const bool first=h.sinks.empty();
        if(std::find(h.sinks.begin(),h.sinks.end(),sink)==h.sinks.end())h.sinks.push_back(std::move(sink));
        if(first)h.current=++h.generation;
        if(!h.started){
            h.started=true;
            std::thread([&h]{h.run();}).detach();
        }
    }
    h.cv.notify_all();
}

void detachSystemAudio(const SystemAudioSink* sink){
    auto& h=hub();
    {
        std::lock_guard lock(h.m);
        const auto before=h.sinks.size();
        std::erase_if(h.sinks,[sink](const auto& s){return s.get()==sink;});
        if(before&&h.sinks.empty())h.current=++h.generation;
    }
    h.cv.notify_all();
}

SystemAudioStatus systemAudioStatus(){
    auto& h=hub();
    std::lock_guard lock(h.m);
    return h.status;
}

} // namespace veyra::engine
