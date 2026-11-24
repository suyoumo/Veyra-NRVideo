#pragma once
// SystemAudioLoopback: what the machine is playing, for 全屏录制 - a game that
// Veyra does not play itself still has to reach the recording. One shared-mode
// WASAPI loopback stream on the default playback endpoint, on its own thread,
// opened while at least one listener is attached. Packets are stamped with the
// recording clock when they reached the endpoint, and delivered as 48 kHz
// interleaved float stereo - the format the rendered-sound tap uses - so the
// recorder's lanes treat both identically. Silence (nothing playing) simply
// produces no packets; the lane fills the gap.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace veyra::engine {

class SystemAudioSink {
public:
    virtual ~SystemAudioSink()=default;
    // Loopback thread: copy and return, never block.
    virtual void systemPcm(const float* stereo,size_t frames,int64_t capturedUs)=0;
};

struct SystemAudioStatus {
    bool open=false;             // capturing now
    std::wstring device;         // friendly name of the endpoint in use (or last tried)
    std::wstring error;          // why the last attempt failed; empty while it works
    uint32_t rate=0,channels=0;  // the endpoint's own format
    uint64_t packets=0,failures=0,opens=0;
};

// Any thread, never waits for the device: the capture thread opens the default
// playback endpoint while a sink is attached.
void attachSystemAudio(std::shared_ptr<SystemAudioSink> sink);
void detachSystemAudio(const SystemAudioSink* sink);
SystemAudioStatus systemAudioStatus();

} // namespace veyra::engine
