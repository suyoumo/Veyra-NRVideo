#pragma once
#include "Types.h"
#include "TriggerFilter.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
struct SDL_Gamepad;
struct SDL_AudioStream;
namespace veyra::remoteplay {
// SDL calls stay on one owner thread: the Win32 UI owner, or the forwarding
// thread below, independently of GPU processing.
class ControllerInput {
public:
    ControllerInput();
    ~ControllerInput(){stop();}
    bool start(uint32_t preferredDevice=0);
    void stop(); // also turns follow mode off
    // 谁按键就用谁 (PS5 手柄控制): every pad is opened and the one whose button went
    // down last is the one read, so a streaming tool's virtual pad that appears
    // after a local pad still works. Off (default): the first pad found.
    void setFollowLastPressed(bool on);
    // Physical pad button feeding each PS5 button; the index is the
    // ControllerState::Button bit (0 Cross … 15 PS), the value an SDL_GamepadButton
    // or -1 to leave that PS5 button unmapped. Users remap this from 按键映射.
    void setButtonMap(const std::array<std::int8_t,16>& sources);
    static std::array<std::int8_t,16> defaultButtonMap();
    static int buttonSourceFromName(std::string_view name); // -1 unknown/empty
    static const char* buttonSourceName(int source);       // "" when unmapped
    ControllerState poll(bool focused);
    void feedback(ControllerFeedback,bool focused);
    void releaseEffects();
    bool calibrate();
    bool connected()const{return gamepad_!=nullptr;}
    struct Capabilities {bool connected=false,gyro=false,accel=false,touch=false,triggers=false,haptics=false,calibrating=false;std::string name;};
    Capabilities capabilities()const;

    // Forwarding thread: reads the pad every 4 ms and hands each state straight
    // to `sink`, so input does not wait behind the interface thread (whose
    // stalls measured up to a second). SDL is started, pumped and stopped
    // there; start()/stop() from elsewhere first end forwarding, so SDL always
    // has exactly one owner.
    struct Forwarding {
        std::function<bool()> focused;                      // read on the forwarding thread
        std::function<void(ControllerState&,bool)> prepare;  // optional rewrite and diagnosis, same thread
        std::function<void(const ControllerState&)> sink;
        std::function<ControllerFeedback()> feedback;        // optional
    };
    bool startForwarding(Forwarding forwarding);
    void stopForwarding();
    bool forwarding()const{return forwarding_.load();}
    // Read/forward cadence: 250 Hz by default, 500 Hz for lower sampling staleness.
    // The Remote Play feedback channel sends on state change, so this is the ceiling,
    // not a fixed packet rate.
    void setForwardRateHz(int hz);
    // Forwarding thread only: whether the pad being read has a real touchpad.
    bool activeHasTouchpad()const;
    // Polls per second that carried an active pad, and the longest interval
    // between two polls since that figure was last taken.
    double forwardedHz()const{return forwardedHz_.load();}
    long long takeForwardedGapMs(){return forwardedGapMs_.exchange(0);}
private:
    void forwardLoop(std::stop_token stop,Forwarding forwarding);
    void activate(SDL_Gamepad*);
    void deactivate();
    void applyLights();
    bool opened(uint32_t id)const;
    Capabilities currentCapabilities()const;
    SDL_Gamepad* gamepad_=nullptr;
    std::vector<SDL_Gamepad*> others_; // follow mode: open, not read
    std::atomic<bool> follow_{false};
    bool initialized_=false;
    HostTime enumerateAt_=0;uint32_t preferredDevice_=0;
    std::array<int8_t,2> touchIds_{-1,-1};uint8_t nextTouchId_=0;
    bool gyro_=false,accel_=false,effectsActive_=false,audioInitialized_=false,hapticAttempted_=false;
    SDL_AudioStream* hapticStream_=nullptr;
    // Console-set light bar and player indicator: kept while unfocused, applied to the read pad.
    std::array<uint8_t,3> ledColor_{};uint8_t player_=0;bool haveLed_=false,havePlayer_=false,lightsDirty_=false;
    std::array<ControllerState::Touch,2> touchState_{};
    std::deque<std::array<ControllerState::Touch,2>> touchQueue_;
    std::array<float,3> gyroBias_{},biasSum_{},sensorGyro_{},sensorAccel_{0,1,0};
    unsigned calibrationSamples_=0;bool calibrating_=false,haveGyro_=false,haveAccel_=false;
    HostTime calibrationStart_=0;uint64_t sensorStamp_=0;
    TriggerFilter triggerFilter_;

    // Read by the forwarding thread every 4 ms, written from the interface thread
    // when the user edits 按键映射.
    std::mutex mapMutex_;
    std::array<std::int8_t,16> buttonMap_{};

    std::jthread forwardThread_;
    std::atomic<bool> forwarding_{false};
    std::atomic<double> forwardedHz_{0};
    std::atomic<long long> forwardedGapMs_{0};
    std::atomic<int> forwardPeriodUs_{4000};
    // Published by the forwarding thread for capabilities() readers elsewhere.
    mutable std::mutex publishMutex_;
    Capabilities published_;
};
}
