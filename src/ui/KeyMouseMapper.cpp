// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/KeyMouseMapper.h"

#include <algorithm>
#include <cmath>

namespace veyra::ui {
namespace {
constexpr std::uint32_t kCross = 1u << 0, kCircle = 1u << 1, kSquare = 1u << 2, kTriangle = 1u << 3;
constexpr std::uint32_t kLeft = 1u << 4, kRight = 1u << 5, kUp = 1u << 6, kDown = 1u << 7;
constexpr std::uint32_t kL1 = 1u << 8, kR1 = 1u << 9, kL3 = 1u << 10, kR3 = 1u << 11;
constexpr std::uint32_t kOptions = 1u << 12, kShare = 1u << 13, kTouchpad = 1u << 14, kPs = 1u << 15;

// GTA6 预设（来自 XY-CtoPS5 的 presets/gta6.json）：默认/驾驶两套。
struct ButtonName { const char* name; std::uint32_t bit; int trigger; };
constexpr ButtonName kNames[] = {
    {"CROSS", kCross, 0}, {"CIRCLE", kCircle, 0}, {"SQUARE", kSquare, 0}, {"TRIANGLE", kTriangle, 0},
    {"UP", kUp, 0}, {"DOWN", kDown, 0}, {"LEFT", kLeft, 0}, {"RIGHT", kRight, 0},
    {"L1", kL1, 0}, {"R1", kR1, 0}, {"L2", 0, 1}, {"R2", 0, 2},
    {"L3", kL3, 0}, {"R3", kR3, 0}, {"OPTIONS", kOptions, 0}, {"SHARE", kShare, 0},
    {"PS", kPs, 0}, {"TOUCHPAD", kTouchpad, 0}};

std::uint32_t bitFor(const std::string& name) {
    for (const auto& n : kNames)
        if (name == n.name) return n.bit;
    return 0;
}
int triggerFor(const std::string& name) {
    for (const auto& n : kNames)
        if (name == n.name) return n.trigger;
    return 0;
}

KeyMouseMapper::Action makeButton(const char* name) {
    KeyMouseMapper::Action a;
    a.kind = KeyMouseMapper::Action::Button;
    a.bit = bitFor(name);
    a.trigger = triggerFor(name);
    return a;
}
KeyMouseMapper::Action makeChord(std::initializer_list<const char*> names) {
    KeyMouseMapper::Action a;
    a.kind = KeyMouseMapper::Action::Chord;
    for (const char* n : names) a.bits.push_back(bitFor(n));
    return a;
}
KeyMouseMapper::Action makeStick(std::initializer_list<float> values) {
    KeyMouseMapper::Action a;
    a.kind = KeyMouseMapper::Action::Stick;
    for (float v : values) a.values.push_back(std::clamp(v, 0.05f, 1.0f));
    return a;
}
float moveToward(float current, float target, float step) {
    if (current < target) return std::min(current + step, target);
    return std::max(current - step, target);
}
float applyCurve(float value, int type) {
    if (value == 0.0f) return 0.0f;
    if (type == 1) return value * std::abs(value);
    if (type == 2) return value * value * value;
    return value;
}
float applyAntiDeadzone(float value, float deadzone, float anti) {
    if (value == 0.0f || anti <= 0.0f) return value;
    const float sign = value > 0 ? 1.0f : -1.0f;
    const float span = std::max(1e-6f, 1.0f - deadzone);
    const float normalized = std::clamp((std::abs(value) - deadzone) / span, 0.0f, 1.0f);
    return sign * (anti + normalized * (1.0f - anti));
}
std::string canonical(const std::string& name, const std::map<std::string, KeyMouseMapper::Action>& actions) {
    if (actions.count(name)) return name;
    // Hook names for the right-hand modifiers fall back to the left names used by presets.
    static const std::pair<const char*, const char*> aliases[] = {
        {"ctrl_r", "ctrl_l"}, {"alt_r", "alt_l"}, {"shift_r", "shift"}};
    for (const auto& [from, to] : aliases)
        if (name == from && actions.count(to)) return to;
    return name;
}
} // namespace

KeyMouseMapper::KeyMouseMapper() {
    Mode base;
    base.stickKeys = {{0, {"w"}}, {1, {"s"}}, {2, {"a"}}, {3, {"d"}}};
    base.keys = {
        {"space", makeButton("CROSS")}, {"r", makeButton("SQUARE")}, {"q", makeButton("R1")},
        {"c", makeButton("CIRCLE")}, {"v", makeButton("DOWN")}, {"tab", makeButton("L1")},
        {"esc", makeButton("OPTIONS")}, {"alt_l", makeStick({0.3f, 1.0f})},
        {"ctrl_l", makeButton("RIGHT")}, {"shift", makeButton("L3")}, {"b", makeButton("LEFT")},
        {"f", makeButton("TRIANGLE")}, {"up", makeButton("UP")}, {"down", makeButton("DOWN")},
        {"left", makeButton("LEFT")}, {"right", makeButton("RIGHT")}, {"m", makeButton("TOUCHPAD")},
        {"caps_lock", makeChord({"L3", "R3"})}};
    base.mouse = {{"left", makeButton("R2")}, {"right", makeButton("L2")},
                  {"middle", makeButton("R3")}, {"x1", makeButton("UP")}};

    Mode driving = base;
    driving.keys["w"] = makeButton("R2");
    driving.keys["s"] = makeButton("L2");
    driving.mouse["left"] = makeButton("R1");
    driving.mouse.erase("right");

    modes_ = {base, driving};
    leftValue_ = tuning_.leftValue;
}

void KeyMouseMapper::setTuning(const KbmTuning& t) {
    std::lock_guard lock(mutex_);
    tuning_ = t;
    tuning_.sensitivity = std::clamp(tuning_.sensitivity, 0.1f, 10.0f);
    tuning_.deadzone = std::clamp(tuning_.deadzone, 0.0f, 0.5f);
    tuning_.antiDeadzone = std::clamp(tuning_.antiDeadzone, 0.0f, 0.9f);
    tuning_.smoothing = std::clamp(tuning_.smoothing, 0.0f, 0.95f);
    tuning_.slowSpeed = std::max(1.0f, tuning_.slowSpeed);
    tuning_.fastSpeed = std::max(tuning_.slowSpeed + 1.0f, tuning_.fastSpeed);
    tuning_.leftValue = std::clamp(tuning_.leftValue, 0.05f, 1.0f);
    tuning_.leftRamp = std::clamp(tuning_.leftRamp, 0.0f, 50.0f);
    tuning_.r2Depth = std::clamp(tuning_.r2Depth, 0.0f, 1.0f);
    tuning_.l2Depth = std::clamp(tuning_.l2Depth, 0.0f, 1.0f);
}
KbmTuning KeyMouseMapper::tuning() const {
    std::lock_guard lock(mutex_);
    return tuning_;
}
int KeyMouseMapper::mode() const {
    std::lock_guard lock(mutex_);
    return mode_;
}
int KeyMouseMapper::modeCount() const { return int(modes_.size()); }

void KeyMouseMapper::applyMode(int index) {
    mode_ = index;
    keys_.clear();
    mouse_.clear();
    sequenceBits_ = 0;
    accumDx_ = accumDy_ = 0;
    accumTime_ = 0;
    rightX_ = rightY_ = leftX_ = leftY_ = 0;
}
void KeyMouseMapper::setMode(int index) {
    std::lock_guard lock(mutex_);
    if (index < 0 || index >= int(modes_.size()) || index == mode_) return;
    applyMode(index);
}
void KeyMouseMapper::toggleMode() {
    std::lock_guard lock(mutex_);
    applyMode((mode_ + 1) % int(modes_.size()));
}

void KeyMouseMapper::pressAction(Action& action) {
    if (action.kind == Action::Stick && !action.values.empty()) {
        leftValue_ = action.values[action.cycle % action.values.size()];
        action.cycle = (action.cycle + 1) % action.values.size();
    }
}

void KeyMouseMapper::onKey(const std::string& name, bool down) {
    std::lock_guard lock(mutex_);
    const auto key = canonical(name, modes_[mode_].keys);
    if (down) {
        if (keys_.count(key)) return; // keyboard auto-repeat
        keys_.insert(key);
        const auto it = modes_[mode_].keys.find(key);
        if (it != modes_[mode_].keys.end()) pressAction(it->second);
    } else {
        keys_.erase(key);
    }
}

void KeyMouseMapper::onMouseButton(const std::string& name, bool down) {
    std::lock_guard lock(mutex_);
    if (down && name == "x2") { // GTA6: mouse side button 2 switches the mode
        applyMode((mode_ + 1) % int(modes_.size()));
        return;
    }
    if (down) {
        if (mouse_.count(name)) return;
        mouse_.insert(name);
        const auto it = modes_[mode_].mouse.find(name);
        if (it != modes_[mode_].mouse.end()) pressAction(it->second);
    } else {
        mouse_.erase(name);
    }
}

void KeyMouseMapper::onMouseDelta(int dx, int dy) {
    std::lock_guard lock(mutex_);
    accumDx_ += dx;
    accumDy_ += dy;
}

void KeyMouseMapper::tick(double dtSeconds, Output& out) {
    const double dt = std::clamp(dtSeconds, 0.002, 0.05);
    std::lock_guard lock(mutex_);
    out = {};

    const Mode& mode = modes_[mode_];
    // --- 左摇杆：键盘方向键，对角归一化，斜坡过渡 ---
    const auto pressed = [&](int direction) {
        const auto it = mode.stickKeys.find(direction);
        if (it == mode.stickKeys.end()) return false;
        for (const auto& key : it->second)
            if (keys_.count(key)) return true;
        return false;
    };
    float x = float(pressed(3)) - float(pressed(2));
    float y = float(pressed(1)) - float(pressed(0));
    if (x != 0.0f && y != 0.0f) { x /= std::sqrt(2.0f); y /= std::sqrt(2.0f); }
    const float targetX = x * leftValue_;
    const float targetY = y * leftValue_;
    if (tuning_.leftRamp <= 0.0f) {
        leftX_ = targetX;
        leftY_ = targetY;
    } else {
        const float step = tuning_.leftRamp * float(dt);
        leftX_ = moveToward(leftX_, targetX, step);
        leftY_ = moveToward(leftY_, targetY, step);
    }
    out.leftX = leftX_;
    out.leftY = leftY_;

    // --- 右摇杆：鼠标速度 -> 摇杆，固定 10ms 窗口 ---
    if (!tuning_.mouseEnabled) {
        accumDx_ = accumDy_ = 0;
        accumTime_ = 0;
        rightX_ = rightY_ = 0;
    } else {
        accumTime_ += dt;
        if (accumTime_ >= 0.010) {
            const double span = accumTime_;
            const float speedX = float(accumDx_ / span);
            const float speedY = float(accumDy_ / span);
            accumDx_ = accumDy_ = 0;
            accumTime_ = 0;
            if (tuning_.flick && std::abs(speedX) >= tuning_.flickThreshold) {
                flickDir_ = speedX > 0 ? 1.0f : -1.0f;
                flickRemaining_ = tuning_.flickDuration;
            }
            if (flickRemaining_ > 0.0) {
                flickRemaining_ -= dt;
                if (flickRemaining_ > 0.0) {
                    rightX_ = flickDir_;
                    rightY_ = 0;
                    out.rightX = rightX_;
                    out.rightY = rightY_;
                    out.mouseActive = true;
                }
            }
            if (!(flickRemaining_ > 0.0)) {
                // 两段灵敏度：慢速精细、快速转身，中间线性插值。
                const float speed = std::hypot(speedX, speedY);
                float segment = tuning_.slowSensitivity;
                if (speed >= tuning_.fastSpeed) segment = tuning_.fastSensitivity;
                else if (speed > tuning_.slowSpeed)
                    segment = tuning_.slowSensitivity + (tuning_.fastSensitivity - tuning_.slowSensitivity)
                              * (speed - tuning_.slowSpeed) / (tuning_.fastSpeed - tuning_.slowSpeed);
                const float gain = (tuning_.sensitivity * segment) / 2500.0f;
                float tx = std::clamp(speedX * gain, -1.0f, 1.0f);
                float ty = std::clamp(speedY * gain, -1.0f, 1.0f);
                if (tuning_.invertY) ty = -ty;
                tx = applyCurve(tx, tuning_.curveType);
                ty = applyCurve(ty, tuning_.curveType);
                if (std::abs(tx) < tuning_.deadzone) tx = 0;
                if (std::abs(ty) < tuning_.deadzone) ty = 0;
                tx = applyAntiDeadzone(tx, tuning_.deadzone, tuning_.antiDeadzone);
                ty = applyAntiDeadzone(ty, tuning_.deadzone, tuning_.antiDeadzone);
                if (tuning_.smoothing > 0.0f) {
                    const float alpha = 1.0f - tuning_.smoothing;
                    rightX_ += (tx - rightX_) * alpha;
                    rightY_ += (ty - rightY_) * alpha;
                } else {
                    rightX_ = tx;
                    rightY_ = ty;
                }
            }
        }
        out.rightX = rightX_;
        out.rightY = rightY_;
        out.mouseActive = rightX_ != 0.0f || rightY_ != 0.0f;
    }

    // --- 按键：当前按下集合 -> 语义按钮位 / 扳机深度 ---
    std::uint32_t bits = sequenceBits_;
    int l2 = 0, r2 = 0;
    const auto collect = [&](const Action& action) {
        if (action.kind == Action::Button) {
            bits |= action.bit;
            if (action.trigger == 1) l2 = 1;
            if (action.trigger == 2) r2 = 1;
        } else if (action.kind == Action::Chord) {
            for (auto bit : action.bits) bits |= bit;
        }
    };
    for (const auto& key : keys_) {
        const auto it = mode.keys.find(key);
        if (it != mode.keys.end()) collect(it->second);
    }
    for (const auto& button : mouse_) {
        const auto it = mode.mouse.find(button);
        if (it != mode.mouse.end()) collect(it->second);
    }
    out.buttons = bits;
    if (l2) out.l2 = std::uint8_t(std::lround(tuning_.l2Depth * 255.0f));
    if (r2) out.r2 = std::uint8_t(std::lround(tuning_.r2Depth * 255.0f));
}

void KeyMouseMapper::releaseAll() {
    std::lock_guard lock(mutex_);
    keys_.clear();
    mouse_.clear();
    sequenceBits_ = 0;
    accumDx_ = accumDy_ = 0;
    accumTime_ = 0;
    rightX_ = rightY_ = leftX_ = leftY_ = 0;
    flickRemaining_ = 0;
}

} // namespace veyra::ui
