// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/Ps5MouseCapture.h"

#include <algorithm>
#include <cctype>

#include "veyra/Log.h"

namespace veyra::ui {
namespace {
std::string keyName(unsigned vk, unsigned makeCode, unsigned flags) {
    const bool extended = (flags & RI_KEY_E0) != 0;
    // Modifiers often arrive with VKey 0/0xFF; derive them from the scancode.
    if (vk == 0 || vk == 0xFF) {
        switch (makeCode) {
        case 0x1D: vk = extended ? VK_RCONTROL : VK_LCONTROL; break;
        case 0x38: vk = extended ? VK_RMENU : VK_LMENU; break;
        case 0x2A: vk = VK_LSHIFT; break;
        case 0x36: vk = VK_RSHIFT; break;
        case 0x5B: vk = VK_LWIN; break;
        case 0x5C: vk = VK_RWIN; break;
        default: return {};
        }
    }
    if (vk >= 'A' && vk <= 'Z') return std::string(1, char(std::tolower(int(vk))));
    if (vk >= '0' && vk <= '9') return std::string(1, char(vk));
    switch (vk) {
    case VK_SPACE: return "space";
    case VK_TAB: return "tab";
    case VK_ESCAPE: return "esc";
    case VK_RETURN: return "enter";
    case VK_BACK: return "backspace";
    case VK_CAPITAL: return "caps_lock";
    case VK_SHIFT: return makeCode == 0x36 ? "shift_r" : "shift";
    case VK_LSHIFT: return "shift";
    case VK_RSHIFT: return "shift_r";
    case VK_CONTROL: return extended ? "ctrl_r" : "ctrl_l";
    case VK_LCONTROL: return "ctrl_l";
    case VK_RCONTROL: return "ctrl_r";
    case VK_MENU: return extended ? "alt_r" : "alt_l";
    case VK_LMENU: return "alt_l";
    case VK_RMENU: return "alt_r";
    case VK_LWIN: return "win";
    case VK_RWIN: return "win_r";
    case VK_UP: return "up";
    case VK_DOWN: return "down";
    case VK_LEFT: return "left";
    case VK_RIGHT: return "right";
    case VK_INSERT: return "insert";
    case VK_DELETE: return "delete";
    case VK_HOME: return "home";
    case VK_END: return "end";
    case VK_PRIOR: return "pageup";
    case VK_NEXT: return "pagedown";
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return "f" + std::to_string(vk - VK_F1 + 1);
    return "vk" + std::to_string(vk);
}
} // namespace

bool Ps5MouseCapture::capture(bool on) {
    if (on == captured_.load()) return true;
    if (on) {
        if (!window_ || GetForegroundWindow() != window_) return false;
        RAWINPUTDEVICE devices[2] = {
            {0x01, 0x02, 0, window_}, // generic desktop / mouse
            {0x01, 0x06, 0, window_}, // generic desktop / keyboard
        };
        if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
            veyra::log::warn("remoteplay-input", "RegisterRawInputDevices failed; 键鼠捕获 not started");
            return false;
        }
        dx.store(0); dy.store(0); wheel.store(0); buttons.store(0);
        captured_.store(true);
        clipToWindow();
        SetCursor(nullptr);
        veyra::log::info("remoteplay-input", "mouse aim captured");
    } else {
        captured_.store(false);
        RAWINPUTDEVICE devices[2] = {
            {0x01, 0x02, RIDEV_REMOVE, nullptr},
            {0x01, 0x06, RIDEV_REMOVE, nullptr},
        };
        RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        dx.store(0); dy.store(0); wheel.store(0); buttons.store(0);
        veyra::log::info("remoteplay-input", "mouse aim released");
    }
    return true;
}

void Ps5MouseCapture::clipToWindow() {
    RECT client{};
    if (!window_ || !GetClientRect(window_, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window_, &topLeft);
    ClientToScreen(window_, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool Ps5MouseCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (!captured_.load() || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;

    switch (msg->message) {
    case WM_INPUT: {
        alignas(8) BYTE buffer[128];
        UINT size = sizeof(buffer);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, buffer, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1)) {
            const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
            if (raw->header.dwType == RIM_TYPEMOUSE) {
                const RAWMOUSE& mouse = raw->data.mouse;
                if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                    if (mouse.lLastX) dx.fetch_add(mouse.lLastX);
                    if (mouse.lLastY) dy.fetch_add(mouse.lLastY);
                    if ((mouse.lLastX || mouse.lLastY) && onMouseDelta) onMouseDelta(mouse.lLastX, mouse.lLastY);
                }
                const USHORT flags = mouse.usButtonFlags;
                unsigned held = buttons.load();
                const auto edge = [&](unsigned down, unsigned up, const char* name, unsigned bit) {
                    if (flags & down) { held |= bit; if (onMouseButton) onMouseButton(name, true); }
                    if (flags & up) { held &= ~bit; if (onMouseButton) onMouseButton(name, false); }
                };
                edge(RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_LEFT_BUTTON_UP, "left", 1u);
                edge(RI_MOUSE_RIGHT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_UP, "right", 2u);
                edge(RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, "middle", 4u);
                edge(RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_4_UP, "x1", 8u);
                edge(RI_MOUSE_BUTTON_5_DOWN, RI_MOUSE_BUTTON_5_UP, "x2", 16u);
                buttons.store(held);
                if (flags & RI_MOUSE_WHEEL) wheel.fetch_add(int(short(mouse.usButtonData)));
            } else if (raw->header.dwType == RIM_TYPEKEYBOARD && onKey) {
                const RAWKEYBOARD& keyboard = raw->data.keyboard;
                const auto name = keyName(keyboard.VKey, keyboard.MakeCode, keyboard.Flags);
                if (!name.empty()) onKey(name, (keyboard.Flags & RI_KEY_BREAK) == 0);
            }
        }
        if (result) *result = 0;
        return true;
    }
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE:
    case WM_MOVE:
        clipToWindow();
        return false;
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) capture(false);
        return false;
    case WM_KILLFOCUS:
        capture(false);
        return false;
    case WM_KEYDOWN:
        if (msg->wParam == VK_END) {
            capture(false);
            if (onReleased) onReleased();
            if (result) *result = 0;
            return true;
        }
        if (result) *result = 0;
        return true; // keys belong to the mapping while captured
    // Buttons and keys belong to the mapping, never to the interface underneath.
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: case WM_MBUTTONDBLCLK:
    case WM_MOUSEWHEEL: case WM_MOUSEHWHEEL:
        if (result) *result = 0;
        return true;
    default:
        break;
    }
    return false;
}

} // namespace veyra::ui
