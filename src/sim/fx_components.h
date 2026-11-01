// Visual effect components (P6, D-029): bloom over an HDR frame. They are plain data like every
// component; the app hands them to render. Nothing here feeds the simulation or its state hash
// (D-005).
#pragma once

#include "core/reflect.h"

#include <stdint.h>

namespace zaro::sim {

enum HdrFormat : uint32_t { kHdrAuto, kHdrRgba16f, kHdrRg11b10f };

// With a Bloom component anywhere in the scene, the 3D view renders into an HDR frame: colours
// brighter than 1 glow, then the frame is tonemapped. The first one in the scene is used.
struct Bloom {
    bool enabled = true;     // false keeps the HDR frame and tonemap but draws no glow
    float threshold = 1.0f;  // linear brightness where glow starts
    float knee = 0.5f;
    float intensity = 0.8f;
    float radius = 0.7f;
    uint32_t levels = 5;
    float exposure = 1.0f;
    uint32_t format = kHdrAuto;
};

extern const reflect::ComponentDesc kBloomDesc;

void register_fx_components();

}  // namespace zaro::sim
