#pragma once
// AudioProbe: measures, for the self-test panel, whether the sound the player
// renders and the chosen recording endpoint actually carry anything. It listens
// through the same two taps a recording uses (sink/RecordingAudioTap.h and
// engine/MicrophoneCapture.h), so a green meter here means a recording would
// have sound - and a red one names the reason instead of leaving it to a guess.
#include <cstdint>
#include <string>

namespace veyra::ui {

struct AudioProbeSnapshot {
    bool running=false;
    // Peak since the previous snapshot, in dBFS (-120 = silence).
    double gamePeakDb=-120.0;
    double micPeakDb=-120.0;
    uint64_t gameFrames=0;
    uint64_t micFrames=0;
    // The endpoint in use, as the microphone layer reports it.
    bool micOpen=false;
    std::wstring micDevice;
    std::wstring micError;
    uint32_t micRate=0;
    uint32_t micChannels=0;
};

// Starts metering. `micEndpoint` empty means the default recording device, the
// same rule a recording follows. `systemAudio` listens to the system loopback
// instead of the player's tap (全屏录制). False when a probe is already running.
bool startAudioProbe(const std::wstring& micEndpoint,bool systemAudio=false);
void stopAudioProbe();
bool audioProbeRunning();
// Peak-hold: each call returns the loudest sample since the last one, so a
// caller polling at 10 Hz still sees every burst.
AudioProbeSnapshot takeAudioProbeSnapshot();

// 录制试听: capture what a recording would take - the rendered sound mixed with
// the microphone - into one WAV the user can play back at once.
void startSampleCapture();
bool sampleCapturing();
bool finishSampleCapture(const std::wstring& path);

} // namespace veyra::ui
