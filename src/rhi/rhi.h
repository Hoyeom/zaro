// Render hardware interface (D-002). Engine code talks to the GPU only through this header;
// rhi_wgpu.cpp is the first backend. Console backends implement the same functions privately.
#pragma once

#include "platform/platform.h"

#include <stdint.h>

namespace zaro::rhi {

struct Device;
struct Buffer;
struct Shader;
struct BindLayout;
struct BindGroup;
struct ComputePipeline;
struct RenderPipeline;
struct Texture;
struct Sampler;

enum BufferUsage : uint32_t {
    kBufUniform = 1u << 0,
    kBufStorage = 1u << 1,
    kBufIndirect = 1u << 2,
    kBufCopyDst = 1u << 3,
};

enum class BindingType : uint8_t { kUniform, kStorageRead, kStorageReadWrite, kTexture, kSampler };

// kR8 is one linear channel (masks, glyph coverage); kRGBA8Srgb holds colour images.
enum class TextureFormat : uint8_t { kR8, kRGBA8Srgb };

enum Stage : uint32_t { kStageVertex = 1u << 0, kStageFragment = 1u << 1, kStageCompute = 1u << 2 };

struct BindingDesc {
    uint32_t binding;
    BindingType type;
    uint32_t stages;
};

// Exactly one of buffer, texture and sampler is set, matching the layout's BindingType.
struct BindEntry {
    uint32_t binding;
    Buffer* buffer;
    Texture* texture = nullptr;
    Sampler* sampler = nullptr;
};

enum class Backend : uint8_t { kDefault, kVulkan, kD3D12, kMetal };

struct DeviceDesc {
    const platform::NativeWindow* window = nullptr;  // nullptr renders offscreen (headless)
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;
    Backend backend = Backend::kDefault;
};

struct ComputePipelineDesc {
    const char* label;
    Shader* shader;
    const char* entry;
    BindLayout* layout;
};

struct RenderPipelineDesc {
    const char* label;
    Shader* shader;
    const char* vertex_entry;
    const char* fragment_entry;
    BindLayout* layout;
    bool cull_back = true;
    bool depth = true;        // false: no depth test or write (overlays such as UI)
    bool alpha_blend = false; // premultiplied alpha: src + dst * (1 - src.a)
};

constexpr uint32_t kMaxTimedPasses = 8;

struct GpuTimings {
    bool valid = false;
    uint32_t pass_count = 0;
    const char* names[kMaxTimedPasses] = {};
    double ms[kMaxTimedPasses] = {};
    double total_ms = 0.0;
};

// Device. Errors are logged with a ZE id; device_failed() turns true on any GPU error.
Device* create_device(const DeviceDesc& desc);
void destroy_device(Device* device);
bool device_failed(const Device* device);
const char* adapter_name(const Device* device);
const char* backend_name(const Device* device);
bool has_gpu_timing(const Device* device);
void resize(Device* device, uint32_t width, uint32_t height);
void target_size(const Device* device, uint32_t* width, uint32_t* height);

// Resources.
Buffer* create_buffer(Device* device, uint64_t size, uint32_t usage, const char* label);
void destroy_buffer(Buffer* buffer);
void write_buffer(Device* device, Buffer* buffer, uint64_t offset, const void* data, uint64_t size);

// 2D textures, sampled in shaders. write_texture copies a w x h region at (x, y); `data` points at
// the region's first texel and rows are `bytes_per_row` apart.
Texture* create_texture(Device* device, uint32_t width, uint32_t height, TextureFormat format, const char* label);
void destroy_texture(Texture* texture);
void write_texture(Device* device, Texture* texture, uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                   const void* data, uint32_t bytes_per_row);
// Clamp-to-edge sampler; linear or nearest filtering.
Sampler* create_sampler(Device* device, bool linear);
void destroy_sampler(Sampler* sampler);

// Loads a WGSL file produced by the Slang build step. nullptr if missing.
Shader* load_shader(Device* device, const char* path);
void destroy_shader(Shader* shader);

BindLayout* create_bind_layout(Device* device, const BindingDesc* bindings, uint32_t count);
void destroy_bind_layout(BindLayout* layout);
BindGroup* create_bind_group(Device* device, BindLayout* layout, const BindEntry* entries, uint32_t count);
void destroy_bind_group(BindGroup* group);

ComputePipeline* create_compute_pipeline(Device* device, const ComputePipelineDesc& desc);
void destroy_compute_pipeline(ComputePipeline* pipeline);
RenderPipeline* create_render_pipeline(Device* device, const RenderPipelineDesc& desc);
void destroy_render_pipeline(RenderPipeline* pipeline);

// Frame recording. Every pass is GPU-timed when the adapter supports timestamps.
// Pass names must be string literals (they are kept until the timings are read back).
bool begin_frame(Device* device);
void compute_begin(Device* device, const char* name);
void compute_dispatch(Device* device, ComputePipeline* pipeline, BindGroup* group, uint32_t groups_x);
void compute_end(Device* device);
// clear_rgba nullptr keeps the colour target's contents (a pass drawn over an earlier one).
void render_begin(Device* device, const char* name, const float clear_rgba[4]);
void draw(Device* device, RenderPipeline* pipeline, BindGroup* group, uint32_t vertices, uint32_t instances);
void draw_indirect(Device* device, RenderPipeline* pipeline, BindGroup* group, Buffer* args);
void render_end(Device* device);
void end_frame(Device* device);
// Blocks until every submitted frame is finished.
void wait_idle(Device* device);

// Timings of the most recent frame whose GPU work has completed.
GpuTimings last_gpu_timings(const Device* device);
uint32_t last_draw_calls(const Device* device);

// Headless only: copies the last rendered frame as tightly packed RGBA8 (sRGB).
// `out` must hold width * height * 4 bytes.
bool capture_rgba8(Device* device, uint8_t* out);

}  // namespace zaro::rhi
