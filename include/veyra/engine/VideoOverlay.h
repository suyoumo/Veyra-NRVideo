// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <vector>

namespace veyra::engine {
// An image the present blit draws over the picture (shaders/PresentBlit.hlsl):
// straight alpha, BGRA8, top-down, row stride width * 4.
//
// It exists for the frame-rate HUD under 视频直出. The video window is scanned
// out by the display only while it is a whole rectangle; a hole cut for the HUD
// (the usual way QML shows something over the picture) or a window laid over it
// makes DWM composite every frame. Measured on a 4K60 capture: present to
// scanout 9.7 ms composed, about 1.5 ms flipped. Drawing the HUD into the
// swap chain keeps it on screen and the window whole.
struct VideoOverlay {
    std::vector<uint8_t> bgra;
    unsigned width = 0, height = 0;
    int x = 0, y = 0;  // top-left, in output (swap chain) pixels
    bool show = false;
};
}
