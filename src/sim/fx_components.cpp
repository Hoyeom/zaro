#include "sim/fx_components.h"

namespace zaro::sim {

namespace {

const char* const kHdrFormats[] = {"auto", "rgba16f", "rg11b10f", nullptr};

const reflect::FieldDesc kBloomFields[] = {
    ZARO_FIELD(Bloom, enabled, kBool, "Draw the glow; false keeps the HDR frame and tonemap without it"),
    ZARO_FIELD_RANGE(Bloom, threshold, kF32, "Linear brightness where glow starts (colour x intensity above it glows)",
                     0.0, 100.0),
    ZARO_FIELD_RANGE(Bloom, knee, kF32, "0: hard cut at the threshold; 1: glow fades in from black", 0.0, 1.0),
    ZARO_FIELD_RANGE(Bloom, intensity, kF32, "Strength of the glow added to the frame", 0.0, 10.0),
    ZARO_FIELD_RANGE(Bloom, radius, kF32, "0..1: how far the glow spreads", 0.0, 1.0),
    ZARO_FIELD_RANGE(Bloom, levels, kU32, "Half-size levels of the glow; fewer is cheaper and tighter", 1, 8),
    ZARO_FIELD_RANGE(Bloom, exposure, kF32, "Multiplies the scene's light before tonemapping", 0.0, 16.0),
    ZARO_FIELD_ENUM(Bloom, format,
                    "HDR frame format: auto picks rg11b10f where the GPU renders it, else rgba16f (D-030)",
                    kHdrFormats),
};

}  // namespace

ZARO_COMPONENT(Bloom, "Renders the 3D view in HDR: colours brighter than the threshold glow (D-030)", kBloomFields);

void register_fx_components() { reflect::register_component(&kBloomDesc); }

}  // namespace zaro::sim
