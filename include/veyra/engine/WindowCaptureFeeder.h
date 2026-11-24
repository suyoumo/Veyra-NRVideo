#pragma once

// WindowCaptureFeeder: records Veyra's own window - the picture and the
// interface drawn over it - into a LiveRecorder (LiveRecorder.h).
//
// Windows Graphics Capture hands the composited window over as a D3D11 texture:
// the native video child and every QML panel are in it, because the window is
// what the desktop shows. The frame is copied into a shared texture, opened on
// a D3D12 device of its own, and the recorder converts and encodes it exactly
// as it does a presented frame (RgbToNv12 + NVENC).
//
// The capture runs on a thread of its own and never touches the presenter, the
// engine thread or the picture's latency. Sound needs nothing extra: the
// recording audio tap is process-wide, so the file keeps the played sound.
#include <functional>
#include <memory>
#include <string>

namespace veyra::engine {

class LiveRecorder;

// 区域录制: the part of the screen to keep, in monitor pixels. A zero width
// means the whole monitor.
struct CaptureRegion {
    long x=0,y=0,width=0,height=0;
};

class WindowCaptureFeeder final {
public:
    WindowCaptureFeeder();
    ~WindowCaptureFeeder();
    WindowCaptureFeeder(const WindowCaptureFeeder&)=delete;
    WindowCaptureFeeder& operator=(const WindowCaptureFeeder&)=delete;

    // `window` is the top-level HWND whose monitor is recorded (the video is
    // its WS_CHILD). `fps` 0 records every frame the window composes.
    // `region` keeps one rectangle of that monitor (区域录制); zero width
    // records the whole screen. False with a reason when the screen cannot be
    // captured; nothing runs then.
    bool start(void* window,LiveRecorder& recorder,unsigned fps,std::wstring& error,
               const CaptureRegion& region={});
    // Stops and joins; frames already handed over are still encoded.
    void stop();
    bool running() const;
    // Why the capture stopped by itself (window closed, recorder ended), for
    // the interface. Empty while it runs normally.
    std::wstring note() const;

private:
    // ready is called once: empty when the capture is running, the reason when
    // it could not start. It is called before the loop, not when it ends.
    std::wstring run(void* window,LiveRecorder& recorder,unsigned fps,
                     const std::function<void(const std::wstring&)>& ready,
                     const CaptureRegion& region);
    struct Impl;
    std::unique_ptr<Impl> p_;
};

} // namespace veyra::engine
