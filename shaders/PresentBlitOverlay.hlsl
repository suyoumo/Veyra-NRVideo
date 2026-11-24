// SPDX-License-Identifier: GPL-3.0-only
// PresentBlit with the video overlay (engine/VideoOverlay.h): the variant
// VideoPresenter presents with. It declares 24 root constants and a second
// texture (t1); the plain PresentBlit keeps the 16-constant, one-texture
// contract that tests and tools build their pipelines around.
#define VEYRA_PRESENT_OVERLAY 1
#include "PresentBlit.hlsl"
