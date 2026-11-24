#include "AudioProbe.h"
#include "veyra/engine/MicrophoneCapture.h"
#include "veyra/engine/SystemAudioLoopback.h"
#include "veyra/sink/RecordingAudioTap.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <vector>

namespace veyra::ui {
namespace {
// The player renders 48 kHz; a microphone opened at another rate is resampled
// so both tracks line up in the sample.
constexpr uint32_t kSampleRate=48000;

struct Track {
    std::vector<float> samples;   // interleaved, `channels` per frame
    uint32_t rate=0,channels=0;
};

struct Probe final : sink::RecordingAudioSink, engine::MicrophoneSink, engine::SystemAudioSink {
    std::atomic<float> gamePeak{0.0f},micPeak{0.0f};
    std::atomic<uint64_t> gameFrames{0},micFrames{0};
    std::mutex sampleMutex;
    bool sampling=false;
    Track gameTrack,micTrack;

    void append(Track& track,const float* pcm,size_t frames,uint32_t channels,uint32_t rate){
        if(!channels)return;
        track.rate=rate;track.channels=channels;
        track.samples.insert(track.samples.end(),pcm,pcm+frames*channels);
    }

    // Audio owner thread: keep it to two passes over one block.
    void renderedPcm(const float* pcm,size_t frames,const sink::AudioFormat& format,int64_t,const void* renderer) override {
        if(!pcm||!frames||!format.channels)return;
        float peak=0;
        const size_t count=frames*format.channels;
        for(size_t i=0;i<count;++i)peak=std::max(peak,std::fabs(pcm[i]));
        float seen=gamePeak.load();
        while(peak>seen&&!gamePeak.compare_exchange_weak(seen,peak)){}
        gameFrames.fetch_add(frames);
        if(sampling){std::lock_guard lock(sampleMutex);if(sampling)append(gameTrack,pcm,frames,format.channels,kSampleRate);}
    }
    void microphonePcm(const float* stereo,size_t frames,int64_t) override {
        if(!stereo||!frames)return;
        float peak=0;
        for(size_t i=0;i<frames*2;++i)peak=std::max(peak,std::fabs(stereo[i]));
        float seen=micPeak.load();
        while(peak>seen&&!micPeak.compare_exchange_weak(seen,peak)){}
        micFrames.fetch_add(frames);
        if(sampling){std::lock_guard lock(sampleMutex);if(sampling)append(micTrack,stereo,frames,2,kSampleRate);}
    }
    // 全屏录制: the game sound arrives through the system loopback, not the
    // player's renderer. The meter is the same one, so the panel reads the
    // path a take would use.
    void systemPcm(const float* stereo,size_t frames,int64_t) override {
        if(!stereo||!frames)return;
        float peak=0;
        for(size_t i=0;i<frames*2;++i)peak=std::max(peak,std::fabs(stereo[i]));
        float seen=gamePeak.load();
        while(peak>seen&&!gamePeak.compare_exchange_weak(seen,peak)){}
        gameFrames.fetch_add(frames);
        if(sampling){std::lock_guard lock(sampleMutex);if(sampling)append(gameTrack,stereo,frames,2,kSampleRate);}
    }
};

std::mutex gate;
std::shared_ptr<Probe> probe;

double toDb(float peak){ return peak>0.0f?20.0*std::log10(double(peak)):-120.0; }

// Stereo 48 kHz, linear interpolation: enough for a listening test.
std::vector<float> toStereo48(const Track& track){
    if(track.samples.empty()||!track.rate||!track.channels)return {};
    const size_t frames=track.samples.size()/track.channels;
    const double ratio=double(kSampleRate)/double(track.rate);
    const size_t outFrames=size_t(double(frames)*ratio);
    std::vector<float> out(outFrames*2,0.0f);
    for(size_t i=0;i<outFrames;++i){
        const double position=double(i)/ratio;
        const size_t first=std::min(frames-1,size_t(position));
        const size_t second=std::min(frames-1,first+1);
        const float blend=float(position-double(first));
        for(size_t channel=0;channel<2;++channel){
            const size_t source=track.channels==1?0:std::min(channel,size_t(track.channels-1));
            const float a=track.samples[first*track.channels+source];
            const float b=track.samples[second*track.channels+source];
            out[i*2+channel]=a+(b-a)*blend;
        }
    }
    return out;
}

void writeLe(std::ofstream& file,uint32_t value,int bytes){
    for(int i=0;i<bytes;++i)file.put(char((value>>(8*i))&0xFF));
}

bool writeWav(const std::wstring& path,const std::vector<float>& stereo){
    std::ofstream file(std::filesystem::path(path),std::ios::binary);
    if(!file)return false;
    const uint32_t frames=uint32_t(stereo.size()/2);
    const uint32_t dataBytes=frames*2*2;
    file.write("RIFF",4);writeLe(file,36+dataBytes,4);file.write("WAVE",4);
    file.write("fmt ",4);writeLe(file,16,4);writeLe(file,1,2);writeLe(file,2,2);
    writeLe(file,kSampleRate,4);writeLe(file,kSampleRate*4,4);writeLe(file,4,2);writeLe(file,16,2);
    file.write("data",4);writeLe(file,dataBytes,4);
    for(size_t i=0;i<stereo.size();++i){
        const float clamped=std::clamp(stereo[i],-1.0f,1.0f);
        const int16_t sample=int16_t(std::lround(clamped*32767.0f));
        file.put(char(sample&0xFF));file.put(char((sample>>8)&0xFF));
    }
    return bool(file);
}
}

bool startAudioProbe(const std::wstring& micEndpoint,bool systemAudio){
    auto fresh=std::make_shared<Probe>();
    {
        std::lock_guard lock(gate);
        if(probe)return false;
        probe=fresh;
    }
    if(systemAudio)engine::attachSystemAudio(fresh);
    else sink::addRecordingAudioSink(fresh);
    engine::attachMicrophone(fresh,micEndpoint);
    return true;
}

void stopAudioProbe(){
    std::shared_ptr<Probe> gone;
    {std::lock_guard lock(gate);gone=std::move(probe);}
    if(!gone)return;
    engine::detachMicrophone(gone.get());
    engine::detachSystemAudio(gone.get());
    sink::removeRecordingAudioSink(gone.get());
}

bool audioProbeRunning(){
    std::lock_guard lock(gate);
    return probe!=nullptr;
}

AudioProbeSnapshot takeAudioProbeSnapshot(){    std::shared_ptr<Probe> current;
    {std::lock_guard lock(gate);current=probe;}
    AudioProbeSnapshot snapshot;
    if(!current)return snapshot;
    snapshot.running=true;
    snapshot.gamePeakDb=toDb(current->gamePeak.exchange(0.0f));
    snapshot.micPeakDb=toDb(current->micPeak.exchange(0.0f));
    snapshot.gameFrames=current->gameFrames.load();
    snapshot.micFrames=current->micFrames.load();
    const auto mic=engine::microphoneStatus();
    snapshot.micOpen=mic.open;
    snapshot.micDevice=mic.device;
    snapshot.micError=mic.error;
    snapshot.micRate=mic.rate;
    snapshot.micChannels=mic.channels;
    return snapshot;
}

void startSampleCapture(){
    std::shared_ptr<Probe> current;
    {std::lock_guard lock(gate);current=probe;}
    if(!current)return;
    std::lock_guard lock(current->sampleMutex);
    current->gameTrack=Track{};
    current->micTrack=Track{};
    current->sampling=true;
}

bool sampleCapturing(){
    std::shared_ptr<Probe> current;
    {std::lock_guard lock(gate);current=probe;}
    if(!current)return false;
    std::lock_guard lock(current->sampleMutex);
    return current->sampling;
}

bool finishSampleCapture(const std::wstring& path){
    std::shared_ptr<Probe> current;
    {std::lock_guard lock(gate);current=probe;}
    if(!current)return false;
    Track game,mic;
    {
        std::lock_guard lock(current->sampleMutex);
        current->sampling=false;
        game=std::move(current->gameTrack);
        mic=std::move(current->micTrack);
    }
    const auto gameStereo=toStereo48(game);
    const auto micStereo=toStereo48(mic);
    // Keep the two lanes apart next to the mix: a silent lane names the
    // problem (no game sound vs. no voice) instead of hiding it in a mix.
    const auto sidecar=[](const std::wstring& base,const wchar_t* suffix,const std::vector<float>& data){
        if(data.empty())return;
        std::filesystem::path path(base);
        path.replace_filename(path.stem().wstring()+suffix+path.extension().wstring());
        writeWav(path.wstring(),data);
    };
    sidecar(path,L"-game",gameStereo);
    sidecar(path,L"-mic",micStereo);
    const size_t frames=std::max(gameStereo.size(),micStereo.size())/2;
    if(!frames)return false;
    std::vector<float> mixed(frames*2,0.0f);
    for(size_t i=0;i<mixed.size();++i){
        const float rendered=i<gameStereo.size()?gameStereo[i]:0.0f;
        const float voice=i<micStereo.size()?micStereo[i]:0.0f;
        mixed[i]=rendered+voice;
    }
    return writeWav(path,mixed);
}

} // namespace veyra::ui