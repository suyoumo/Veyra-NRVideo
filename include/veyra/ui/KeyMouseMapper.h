// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 键鼠映射 (PS5 手柄控制): keyboard + mouse -> controller state, merged on top of
// the physical pad. The processing follows XY-CtoPS5 / JoyShockMapper practice:
// a 10 ms mouse window, two-stage sensitivity, response curves, deadzone and
// anti-deadzone, smoothing, flick stick, and per-mode key maps (GTA6 preset:
// default / driving). Pure state machine: no Win32, no network, safe to test.
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace veyra::ui {

struct KbmTuning {
    float sensitivity = 1.8f;
    int curveType = 0;            // 0 linear, 1 quadratic, 2 cubic
    float deadzone = 0.0f;
    float antiDeadzone = 0.15f;
    float smoothing = 0.5f;
    float slowSpeed = 600.0f;
    float slowSensitivity = 1.3f;
    float fastSpeed = 2500.0f;
    float fastSensitivity = 1.8f;
    bool flick = true;
    float flickThreshold = 1800.0f;
    float flickDuration = 0.033f;
    bool invertY = false;
    float leftValue = 1.0f;
    float leftRamp = 4.0f;
    float r2Depth = 1.0f;
    float l2Depth = 1.0f;
    bool mouseEnabled = true;
};

class KeyMouseMapper {
public:
    struct Output {
        float leftX = 0, leftY = 0;
        float rightX = 0, rightY = 0;
        std::uint32_t buttons = 0;   // ControllerState::Button bits
        std::uint8_t l2 = 0, r2 = 0;
        bool mouseActive = false;    // the mouse currently owns the right stick
    };

    KeyMouseMapper();
    void setTuning(const KbmTuning& t);
    KbmTuning tuning() const;

    int mode() const;
    int modeCount() const;
    void setMode(int index);
    void toggleMode();

    void onKey(const std::string& name, bool down);
    void onMouseButton(const std::string& name, bool down);
    void onMouseDelta(int dx, int dy);

    // dt seconds since the previous tick (clamped to 2..50 ms inside).
    void tick(double dt, Output& out);
    void releaseAll();

    // Preset building block; public so the GTA6 tables can be constructed freely.
    struct Action {
        enum Kind { Button, Chord, Stick };
        Kind kind = Button;
        std::uint32_t bit = 0;             // Button
        int trigger = 0;                   // 1 L2, 2 R2
        std::vector<std::uint32_t> bits;   // Chord
        std::vector<float> values;         // Stick magnitudes
        std::size_t cycle = 0;
    };

private:
    struct Mode {
        std::map<std::string, Action> keys;
        std::map<std::string, Action> mouse;
        std::map<int, std::set<std::string>> stickKeys; // 0 up, 1 down, 2 left, 3 right
    };

    void applyMode(int index);
    void pressAction(Action& action);
    static float curve(float value, const KbmTuning& t);
    static float antiDeadzone(float value, float deadzone, float anti);

    mutable std::mutex mutex_;
    KbmTuning tuning_;
    std::vector<Mode> modes_;
    int mode_ = 0;

    std::set<std::string> keys_;
    std::set<std::string> mouse_;
    std::uint32_t sequenceBits_ = 0;

    int accumDx_ = 0, accumDy_ = 0;
    double accumTime_ = 0.0;
    float rightX_ = 0, rightY_ = 0;
    float leftX_ = 0, leftY_ = 0;
    float leftValue_ = 1.0f;
    double flickRemaining_ = 0.0;
    float flickDir_ = 0.0f;
};

} // namespace veyra::ui
