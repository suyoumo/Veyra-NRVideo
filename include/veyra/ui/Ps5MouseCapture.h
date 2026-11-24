// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// 鼠标瞄准 (PS5 手柄控制): raw mouse for the Remote Play control tunnel. While
// captured the cursor is hidden and clipped to the main window, and relative
// movement/buttons are published as atomics that the forwarding thread turns
// into the right stick (aim) and L2/R2 (aim/fire). The PS5 controller stays the
// primary input; only the mouse is added on top.
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <atomic>
#include <functional>
#include <string>
#include <windows.h>

namespace veyra::ui {

class Ps5MouseCapture final : public QAbstractNativeEventFilter {
public:
    ~Ps5MouseCapture() override { capture(false); }
    void setWindow(HWND topLevel) { window_ = topLevel; }
    // Returns false when it cannot start (no window, or it is not in the foreground).
    bool capture(bool on);
    bool captured() const { return captured_.load(); }

    // Consumed by the forwarding thread.
    std::atomic<int> dx{0}, dy{0}, wheel{0};
    std::atomic<unsigned> buttons{0}; // 1 left, 2 right, 4 middle
    // 键鼠映射: raw keyboard and every mouse button edge (GUI thread).
    std::function<void(const std::string&, bool)> onKey;
    std::function<void(const std::string&, bool)> onMouseButton;
    std::function<void(int, int)> onMouseDelta;
    // End pressed: the capture is already released when this runs (GUI thread).
    std::function<void()> onReleased;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void clipToWindow();
    HWND window_ = nullptr;
    std::atomic<bool> captured_{false};
};

} // namespace veyra::ui
