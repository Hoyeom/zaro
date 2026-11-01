// WebGPU backend (wgpu-native v29). The only file that includes webgpu.h.
#include "rhi/rhi.h"

#include "core/log.h"
#include "core/profile.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace zaro::rhi {

struct Buffer {
    WGPUBuffer buffer = nullptr;
    uint64_t size = 0;
};
struct Shader {
    WGPUShaderModule module = nullptr;
};
struct BindLayout {
    WGPUBindGroupLayout layout = nullptr;
    WGPUPipelineLayout pipeline_layout = nullptr;
};
struct BindGroup {
    WGPUBindGroup group = nullptr;
};
struct ComputePipeline {
    WGPUComputePipeline pipeline = nullptr;
};
struct RenderPipeline {
    WGPURenderPipeline pipeline = nullptr;
};

namespace {

// Two frames in flight: frame N waits for frame N-2 before reusing its slot.
constexpr uint32_t kSlots = 2;
constexpr uint32_t kQueriesPerSlot = kMaxTimedPasses * 2;
// Query resolve destinations must be 256-byte aligned (QUERY_RESOLVE_BUFFER_ALIGNMENT).
constexpr uint64_t kResolveStride = 256;
static_assert(kQueriesPerSlot * 8 <= kResolveStride, "slot timestamps must fit one stride");

struct FrameSlot {
    WGPUBuffer readback = nullptr;  // resolved timestamps, MapRead
    WGPUSubmissionIndex submission = 0;
    bool submitted = false;
    bool map_requested = false;
    bool map_done = false;
    bool map_ok = false;
    uint32_t pass_count = 0;
    const char* names[kMaxTimedPasses] = {};
};

WGPUStringView sv(const char* s) { return WGPUStringView{s, WGPU_STRLEN}; }

}  // namespace

struct Device {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUSurface surface = nullptr;
    WGPUTextureFormat color_format = WGPUTextureFormat_RGBA8UnormSrgb;
    WGPUPresentMode present_mode = WGPUPresentMode_Fifo;
    WGPUCompositeAlphaMode alpha_mode = WGPUCompositeAlphaMode_Auto;
    uint32_t width = 0;
    uint32_t height = 0;

    WGPUTexture offscreen = nullptr;  // headless color target
    WGPUTextureView offscreen_view = nullptr;
    WGPUTexture depth = nullptr;
    WGPUTextureView depth_view = nullptr;

    // Current frame.
    WGPUTexture frame_texture = nullptr;  // surface texture (windowed)
    WGPUTextureView frame_view = nullptr;
    WGPUCommandEncoder encoder = nullptr;
    WGPUComputePassEncoder compute = nullptr;
    WGPURenderPassEncoder render = nullptr;
    uint32_t draw_calls = 0;
    uint32_t prev_draw_calls = 0;

    bool timing = false;
    float timestamp_period = 1.0f;
    WGPUQuerySet queries = nullptr;
    WGPUBuffer resolve = nullptr;
    FrameSlot slots[kSlots];
    uint64_t frame_index = 0;
    GpuTimings last_timings;

    bool failed = false;
    char adapter_name[256] = {};
    const char* backend = "unknown";
};

namespace {

void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void* user, void*) {
    Device* d = (Device*)user;
    d->failed = true;
    log_error(kErrGpuValidation, "gpu %s error: %.*s",
              type == WGPUErrorType_Validation ? "validation" : "internal",
              (int)message.length, message.data ? message.data : "");
}

void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void* user, void*) {
    if (status == WGPURequestAdapterStatus_Success) {
        *(WGPUAdapter*)user = adapter;
    } else {
        log_error(kErrGpuAdapter, "no GPU adapter: %.*s", (int)message.length, message.data ? message.data : "");
    }
}

void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void* user, void*) {
    if (status == WGPURequestDeviceStatus_Success) {
        *(WGPUDevice*)user = device;
    } else {
        log_error(kErrGpuAdapter, "device request failed: %.*s", (int)message.length, message.data ? message.data : "");
    }
}

void on_slot_mapped(WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
    FrameSlot* s = (FrameSlot*)user;
    s->map_done = true;
    s->map_ok = status == WGPUMapAsyncStatus_Success;
}

const char* backend_string(WGPUBackendType t) {
    switch (t) {
        case WGPUBackendType_Vulkan: return "vulkan";
        case WGPUBackendType_D3D12: return "d3d12";
        case WGPUBackendType_Metal: return "metal";
        case WGPUBackendType_D3D11: return "d3d11";
        case WGPUBackendType_OpenGL: return "opengl";
        default: return "unknown";
    }
}

void release_targets(Device* d) {
    if (d->depth_view) wgpuTextureViewRelease(d->depth_view);
    if (d->depth) wgpuTextureRelease(d->depth);
    if (d->offscreen_view) wgpuTextureViewRelease(d->offscreen_view);
    if (d->offscreen) wgpuTextureRelease(d->offscreen);
    d->depth_view = nullptr;
    d->depth = nullptr;
    d->offscreen_view = nullptr;
    d->offscreen = nullptr;
}

void create_targets(Device* d) {
    release_targets(d);
    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
    td.size = {d->width, d->height, 1};
    td.label = sv("depth");
    td.format = WGPUTextureFormat_Depth32Float;
    td.usage = WGPUTextureUsage_RenderAttachment;
    d->depth = wgpuDeviceCreateTexture(d->device, &td);
    d->depth_view = wgpuTextureCreateView(d->depth, nullptr);
    if (d->surface) {
        WGPUSurfaceConfiguration sc = WGPU_SURFACE_CONFIGURATION_INIT;
        sc.device = d->device;
        sc.format = d->color_format;
        sc.usage = WGPUTextureUsage_RenderAttachment;
        sc.width = d->width;
        sc.height = d->height;
        sc.presentMode = d->present_mode;
        sc.alphaMode = d->alpha_mode;
        wgpuSurfaceConfigure(d->surface, &sc);
    } else {
        td.label = sv("offscreen color");
        td.format = d->color_format;
        td.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        d->offscreen = wgpuDeviceCreateTexture(d->device, &td);
        d->offscreen_view = wgpuTextureCreateView(d->offscreen, nullptr);
    }
}

WGPUSurface create_surface(WGPUInstance instance, const platform::NativeWindow& w) {
    WGPUSurfaceDescriptor sd = WGPU_SURFACE_DESCRIPTOR_INIT;
    WGPUSurfaceSourceWindowsHWND win32 = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
    WGPUSurfaceSourceMetalLayer metal = WGPU_SURFACE_SOURCE_METAL_LAYER_INIT;
    WGPUSurfaceSourceXlibWindow x11 = WGPU_SURFACE_SOURCE_XLIB_WINDOW_INIT;
    WGPUSurfaceSourceWaylandSurface wl = WGPU_SURFACE_SOURCE_WAYLAND_SURFACE_INIT;
    switch (w.kind) {
        case platform::NativeWindow::kWin32:
            win32.hinstance = w.display;
            win32.hwnd = w.handle;
            sd.nextInChain = &win32.chain;
            break;
        case platform::NativeWindow::kMetalLayer:
            metal.layer = w.handle;
            sd.nextInChain = &metal.chain;
            break;
        case platform::NativeWindow::kX11:
            x11.display = w.display;
            x11.window = w.x11_window;
            sd.nextInChain = &x11.chain;
            break;
        case platform::NativeWindow::kWayland:
            wl.display = w.display;
            wl.surface = w.handle;
            sd.nextInChain = &wl.chain;
            break;
        case platform::NativeWindow::kNone:
            return nullptr;
    }
    return wgpuInstanceCreateSurface(instance, &sd);
}

void poll(Device* d, bool wait, const WGPUSubmissionIndex* index) {
    wgpuDevicePoll(d->device, wait ? 1u : 0u, index);
    wgpuInstanceProcessEvents(d->instance);
}

// Waits for the slot's previous frame and harvests its GPU timings.
void retire_slot(Device* d, FrameSlot& s) {
    if (!s.submitted) return;
    poll(d, true, &s.submission);
    s.submitted = false;
    if (!s.map_requested) return;
    while (!s.map_done) poll(d, true, nullptr);
    if (s.map_ok) {
        const uint64_t* ts = (const uint64_t*)wgpuBufferGetConstMappedRange(s.readback, 0, kQueriesPerSlot * 8);
        GpuTimings t;
        t.valid = true;
        t.pass_count = s.pass_count;
        for (uint32_t i = 0; i < s.pass_count; ++i) {
            const uint64_t begin = ts[i * 2], end = ts[i * 2 + 1];
            t.names[i] = s.names[i];
            t.ms[i] = end > begin ? (double)(end - begin) * d->timestamp_period / 1.0e6 : 0.0;
            t.total_ms += t.ms[i];
        }
        d->last_timings = t;
        wgpuBufferUnmap(s.readback);
    }
    s.map_requested = false;
    s.map_done = false;
}

WGPUPassTimestampWrites timestamp_writes(Device* d, FrameSlot& s, const char* name) {
    WGPUPassTimestampWrites tw = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    const uint32_t pass = s.pass_count;
    if (pass < kMaxTimedPasses) {
        const uint32_t base = (uint32_t)(d->frame_index % kSlots) * kQueriesPerSlot + pass * 2;
        tw.querySet = d->queries;
        tw.beginningOfPassWriteIndex = base;
        tw.endOfPassWriteIndex = base + 1;
        s.names[pass] = name;
        s.pass_count++;
    }
    return tw;
}

}  // namespace

Device* create_device(const DeviceDesc& desc) {
    Device* d = new Device();
    d->width = desc.width;
    d->height = desc.height;
    d->instance = wgpuCreateInstance(nullptr);
    if (desc.window) d->surface = create_surface(d->instance, *desc.window);

    WGPURequestAdapterOptions ao = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    ao.powerPreference = WGPUPowerPreference_HighPerformance;
    ao.compatibleSurface = d->surface;
    switch (desc.backend) {
        case Backend::kVulkan: ao.backendType = WGPUBackendType_Vulkan; break;
        case Backend::kD3D12: ao.backendType = WGPUBackendType_D3D12; break;
        case Backend::kMetal: ao.backendType = WGPUBackendType_Metal; break;
        case Backend::kDefault: break;
    }
    WGPURequestAdapterCallbackInfo aci = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    aci.mode = WGPUCallbackMode_AllowProcessEvents;
    aci.callback = on_adapter;
    aci.userdata1 = &d->adapter;
    wgpuInstanceRequestAdapter(d->instance, &ao, aci);
    wgpuInstanceProcessEvents(d->instance);
    if (!d->adapter) {
        destroy_device(d);
        return nullptr;
    }

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    wgpuAdapterGetInfo(d->adapter, &info);
    snprintf(d->adapter_name, sizeof(d->adapter_name), "%.*s", (int)info.device.length,
             info.device.data ? info.device.data : "");
    d->backend = backend_string(info.backendType);
    wgpuAdapterInfoFreeMembers(info);

    d->timing = wgpuAdapterHasFeature(d->adapter, WGPUFeatureName_TimestampQuery);
    WGPUFeatureName features[1] = {WGPUFeatureName_TimestampQuery};
    WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
    dd.label = sv("zaro device");
    dd.requiredFeatureCount = d->timing ? 1 : 0;
    dd.requiredFeatures = features;
    dd.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
    dd.uncapturedErrorCallbackInfo.userdata1 = d;
    WGPURequestDeviceCallbackInfo dci = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    dci.mode = WGPUCallbackMode_AllowProcessEvents;
    dci.callback = on_device;
    dci.userdata1 = &d->device;
    wgpuAdapterRequestDevice(d->adapter, &dd, dci);
    wgpuInstanceProcessEvents(d->instance);
    if (!d->device) {
        destroy_device(d);
        return nullptr;
    }
    d->queue = wgpuDeviceGetQueue(d->device);

    if (d->surface) {
        WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
        wgpuSurfaceGetCapabilities(d->surface, d->adapter, &caps);
        d->color_format = caps.formatCount ? caps.formats[0] : WGPUTextureFormat_BGRA8UnormSrgb;
        for (size_t i = 0; i < caps.formatCount; ++i) {
            if (caps.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb ||
                caps.formats[i] == WGPUTextureFormat_RGBA8UnormSrgb) {
                d->color_format = caps.formats[i];
                break;
            }
        }
        d->present_mode = WGPUPresentMode_Fifo;
        if (!desc.vsync) {
            for (size_t i = 0; i < caps.presentModeCount; ++i) {
                if (caps.presentModes[i] == WGPUPresentMode_Immediate) d->present_mode = WGPUPresentMode_Immediate;
            }
        }
        if (caps.alphaModeCount) d->alpha_mode = caps.alphaModes[0];
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }
    create_targets(d);

    if (d->timing) {
        d->timestamp_period = wgpuQueueGetTimestampPeriod(d->queue);
        WGPUQuerySetDescriptor qd = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        qd.label = sv("pass timestamps");
        qd.type = WGPUQueryType_Timestamp;
        qd.count = kQueriesPerSlot * kSlots;
        d->queries = wgpuDeviceCreateQuerySet(d->device, &qd);
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.label = sv("timestamp resolve");
        bd.size = kResolveStride * kSlots;
        bd.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        d->resolve = wgpuDeviceCreateBuffer(d->device, &bd);
        bd.label = sv("timestamp readback");
        bd.size = (uint64_t)kQueriesPerSlot * 8;
        bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        for (FrameSlot& s : d->slots) s.readback = wgpuDeviceCreateBuffer(d->device, &bd);
    }
    return d;
}

void destroy_device(Device* d) {
    if (!d) return;
    if (d->device) wait_idle(d);
    for (FrameSlot& s : d->slots) {
        if (s.readback) wgpuBufferRelease(s.readback);
    }
    if (d->resolve) wgpuBufferRelease(d->resolve);
    if (d->queries) wgpuQuerySetRelease(d->queries);
    release_targets(d);
    if (d->surface) wgpuSurfaceRelease(d->surface);
    if (d->queue) wgpuQueueRelease(d->queue);
    if (d->device) wgpuDeviceRelease(d->device);
    if (d->adapter) wgpuAdapterRelease(d->adapter);
    if (d->instance) wgpuInstanceRelease(d->instance);
    delete d;
}

bool device_failed(const Device* d) { return d->failed; }
const char* adapter_name(const Device* d) { return d->adapter_name; }
const char* backend_name(const Device* d) { return d->backend; }
bool has_gpu_timing(const Device* d) { return d->timing; }

void resize(Device* d, uint32_t width, uint32_t height) {
    if (width == 0 || height == 0 || (width == d->width && height == d->height)) return;
    wait_idle(d);
    d->width = width;
    d->height = height;
    create_targets(d);
}

void target_size(const Device* d, uint32_t* width, uint32_t* height) {
    *width = d->width;
    *height = d->height;
}

Buffer* create_buffer(Device* d, uint64_t size, uint32_t usage, const char* label) {
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = sv(label);
    bd.size = (size + 3) & ~3ull;
    bd.usage = WGPUBufferUsage_CopyDst;
    if (usage & kBufUniform) bd.usage |= WGPUBufferUsage_Uniform;
    if (usage & kBufStorage) bd.usage |= WGPUBufferUsage_Storage;
    if (usage & kBufIndirect) bd.usage |= WGPUBufferUsage_Indirect;
    Buffer* b = new Buffer();
    b->buffer = wgpuDeviceCreateBuffer(d->device, &bd);
    b->size = bd.size;
    return b;
}

void destroy_buffer(Buffer* b) {
    if (!b) return;
    wgpuBufferRelease(b->buffer);
    delete b;
}

void write_buffer(Device* d, Buffer* b, uint64_t offset, const void* data, uint64_t size) {
    wgpuQueueWriteBuffer(d->queue, b->buffer, offset, data, (size_t)size);
}

Shader* load_shader(Device* d, const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        log_error(kErrShaderLoad, "%s: cannot open shader (was the Slang build step run?)", path);
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* code = (char*)malloc((size_t)size);
    const size_t read = fread(code, 1, (size_t)size, f);
    fclose(f);
    WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgsl.code = WGPUStringView{code, read};
    WGPUShaderModuleDescriptor md = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    md.nextInChain = &wgsl.chain;
    md.label = sv(path);
    Shader* s = new Shader();
    s->module = wgpuDeviceCreateShaderModule(d->device, &md);
    free(code);
    return s;
}

void destroy_shader(Shader* s) {
    if (!s) return;
    wgpuShaderModuleRelease(s->module);
    delete s;
}

BindLayout* create_bind_layout(Device* d, const BindingDesc* bindings, uint32_t count) {
    WGPUBindGroupLayoutEntry entries[16];
    for (uint32_t i = 0; i < count && i < 16; ++i) {
        WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        e.binding = bindings[i].binding;
        if (bindings[i].stages & kStageVertex) e.visibility |= WGPUShaderStage_Vertex;
        if (bindings[i].stages & kStageFragment) e.visibility |= WGPUShaderStage_Fragment;
        if (bindings[i].stages & kStageCompute) e.visibility |= WGPUShaderStage_Compute;
        switch (bindings[i].type) {
            case BindingType::kUniform: e.buffer.type = WGPUBufferBindingType_Uniform; break;
            case BindingType::kStorageRead: e.buffer.type = WGPUBufferBindingType_ReadOnlyStorage; break;
            case BindingType::kStorageReadWrite: e.buffer.type = WGPUBufferBindingType_Storage; break;
        }
        entries[i] = e;
    }
    WGPUBindGroupLayoutDescriptor ld = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    ld.entryCount = count;
    ld.entries = entries;
    BindLayout* l = new BindLayout();
    l->layout = wgpuDeviceCreateBindGroupLayout(d->device, &ld);
    WGPUPipelineLayoutDescriptor pd = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pd.bindGroupLayoutCount = 1;
    pd.bindGroupLayouts = &l->layout;
    l->pipeline_layout = wgpuDeviceCreatePipelineLayout(d->device, &pd);
    return l;
}

void destroy_bind_layout(BindLayout* l) {
    if (!l) return;
    wgpuPipelineLayoutRelease(l->pipeline_layout);
    wgpuBindGroupLayoutRelease(l->layout);
    delete l;
}

BindGroup* create_bind_group(Device* d, BindLayout* layout, const BindEntry* entries, uint32_t count) {
    WGPUBindGroupEntry e[16];
    for (uint32_t i = 0; i < count && i < 16; ++i) {
        WGPUBindGroupEntry x = WGPU_BIND_GROUP_ENTRY_INIT;
        x.binding = entries[i].binding;
        x.buffer = entries[i].buffer->buffer;
        x.size = entries[i].buffer->size;
        e[i] = x;
    }
    WGPUBindGroupDescriptor gd = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    gd.layout = layout->layout;
    gd.entryCount = count;
    gd.entries = e;
    BindGroup* g = new BindGroup();
    g->group = wgpuDeviceCreateBindGroup(d->device, &gd);
    return g;
}

void destroy_bind_group(BindGroup* g) {
    if (!g) return;
    wgpuBindGroupRelease(g->group);
    delete g;
}

ComputePipeline* create_compute_pipeline(Device* d, const ComputePipelineDesc& desc) {
    WGPUComputePipelineDescriptor cd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cd.label = sv(desc.label);
    cd.layout = desc.layout->pipeline_layout;
    cd.compute.module = desc.shader->module;
    cd.compute.entryPoint = sv(desc.entry);
    ComputePipeline* p = new ComputePipeline();
    p->pipeline = wgpuDeviceCreateComputePipeline(d->device, &cd);
    return p;
}

void destroy_compute_pipeline(ComputePipeline* p) {
    if (!p) return;
    wgpuComputePipelineRelease(p->pipeline);
    delete p;
}

RenderPipeline* create_render_pipeline(Device* d, const RenderPipelineDesc& desc) {
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = d->color_format;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fs = WGPU_FRAGMENT_STATE_INIT;
    fs.module = desc.shader->module;
    fs.entryPoint = sv(desc.fragment_entry);
    fs.targetCount = 1;
    fs.targets = &target;
    WGPUDepthStencilState ds = WGPU_DEPTH_STENCIL_STATE_INIT;
    ds.format = WGPUTextureFormat_Depth32Float;
    ds.depthWriteEnabled = WGPUOptionalBool_True;
    ds.depthCompare = WGPUCompareFunction_Less;
    WGPURenderPipelineDescriptor rd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rd.label = sv(desc.label);
    rd.layout = desc.layout->pipeline_layout;
    rd.vertex.module = desc.shader->module;
    rd.vertex.entryPoint = sv(desc.vertex_entry);
    rd.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    rd.primitive.frontFace = WGPUFrontFace_CCW;
    rd.primitive.cullMode = desc.cull_back ? WGPUCullMode_Back : WGPUCullMode_None;
    rd.depthStencil = &ds;
    rd.fragment = &fs;
    RenderPipeline* p = new RenderPipeline();
    p->pipeline = wgpuDeviceCreateRenderPipeline(d->device, &rd);
    return p;
}

void destroy_render_pipeline(RenderPipeline* p) {
    if (!p) return;
    wgpuRenderPipelineRelease(p->pipeline);
    delete p;
}

bool begin_frame(Device* d) {
    ZARO_ZONE("rhi.begin_frame");
    FrameSlot& slot = d->slots[d->frame_index % kSlots];
    retire_slot(d, slot);
    slot.pass_count = 0;
    if (d->surface) {
        WGPUSurfaceTexture st = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(d->surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            if (st.texture) wgpuTextureRelease(st.texture);
            create_targets(d);  // outdated or lost: reconfigure and skip this frame
            return false;
        }
        d->frame_texture = st.texture;
        d->frame_view = wgpuTextureCreateView(st.texture, nullptr);
    } else {
        d->frame_view = d->offscreen_view;
    }
    d->encoder = wgpuDeviceCreateCommandEncoder(d->device, nullptr);
    d->prev_draw_calls = d->draw_calls;
    d->draw_calls = 0;
    return true;
}

void compute_begin(Device* d, const char* name) {
    FrameSlot& slot = d->slots[d->frame_index % kSlots];
    WGPUComputePassDescriptor cd = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    cd.label = sv(name);
    WGPUPassTimestampWrites tw;
    if (d->timing) {
        tw = timestamp_writes(d, slot, name);
        if (tw.querySet) cd.timestampWrites = &tw;
    }
    d->compute = wgpuCommandEncoderBeginComputePass(d->encoder, &cd);
}

void compute_dispatch(Device* d, ComputePipeline* p, BindGroup* g, uint32_t groups_x) {
    wgpuComputePassEncoderSetPipeline(d->compute, p->pipeline);
    wgpuComputePassEncoderSetBindGroup(d->compute, 0, g->group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(d->compute, groups_x, 1, 1);
}

void compute_end(Device* d) {
    wgpuComputePassEncoderEnd(d->compute);
    wgpuComputePassEncoderRelease(d->compute);
    d->compute = nullptr;
}

void render_begin(Device* d, const char* name, const float clear[4]) {
    FrameSlot& slot = d->slots[d->frame_index % kSlots];
    WGPURenderPassColorAttachment ca = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    ca.view = d->frame_view;
    ca.loadOp = WGPULoadOp_Clear;
    ca.storeOp = WGPUStoreOp_Store;
    ca.clearValue = {clear[0], clear[1], clear[2], clear[3]};
    WGPURenderPassDepthStencilAttachment da = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    da.view = d->depth_view;
    da.depthLoadOp = WGPULoadOp_Clear;
    da.depthStoreOp = WGPUStoreOp_Discard;
    da.depthClearValue = 1.0f;
    WGPURenderPassDescriptor rd = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rd.label = sv(name);
    rd.colorAttachmentCount = 1;
    rd.colorAttachments = &ca;
    rd.depthStencilAttachment = &da;
    WGPUPassTimestampWrites tw;
    if (d->timing) {
        tw = timestamp_writes(d, slot, name);
        if (tw.querySet) rd.timestampWrites = &tw;
    }
    d->render = wgpuCommandEncoderBeginRenderPass(d->encoder, &rd);
}

void draw(Device* d, RenderPipeline* p, BindGroup* g, uint32_t vertices, uint32_t instances) {
    wgpuRenderPassEncoderSetPipeline(d->render, p->pipeline);
    wgpuRenderPassEncoderSetBindGroup(d->render, 0, g->group, 0, nullptr);
    wgpuRenderPassEncoderDraw(d->render, vertices, instances, 0, 0);
    d->draw_calls++;
}

void draw_indirect(Device* d, RenderPipeline* p, BindGroup* g, Buffer* args) {
    wgpuRenderPassEncoderSetPipeline(d->render, p->pipeline);
    wgpuRenderPassEncoderSetBindGroup(d->render, 0, g->group, 0, nullptr);
    wgpuRenderPassEncoderDrawIndirect(d->render, args->buffer, 0);
    d->draw_calls++;
}

void render_end(Device* d) {
    wgpuRenderPassEncoderEnd(d->render);
    wgpuRenderPassEncoderRelease(d->render);
    d->render = nullptr;
}

void end_frame(Device* d) {
    ZARO_ZONE("rhi.end_frame");
    FrameSlot& slot = d->slots[d->frame_index % kSlots];
    if (d->timing && slot.pass_count) {
        const uint32_t index = (uint32_t)(d->frame_index % kSlots);
        const uint32_t base = index * kQueriesPerSlot;
        wgpuCommandEncoderResolveQuerySet(d->encoder, d->queries, base, slot.pass_count * 2, d->resolve,
                                          index * kResolveStride);
        wgpuCommandEncoderCopyBufferToBuffer(d->encoder, d->resolve, index * kResolveStride, slot.readback, 0,
                                             (uint64_t)kQueriesPerSlot * 8);
    }
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(d->encoder, nullptr);
    wgpuCommandEncoderRelease(d->encoder);
    d->encoder = nullptr;
    slot.submission = wgpuQueueSubmitForIndex(d->queue, 1, &cb);
    slot.submitted = true;
    wgpuCommandBufferRelease(cb);
    if (d->surface) {
        wgpuSurfacePresent(d->surface);
        wgpuTextureViewRelease(d->frame_view);
        wgpuTextureRelease(d->frame_texture);
        d->frame_texture = nullptr;
    }
    d->frame_view = nullptr;
    if (d->timing && slot.pass_count) {
        WGPUBufferMapCallbackInfo mi = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        mi.mode = WGPUCallbackMode_AllowProcessEvents;
        mi.callback = on_slot_mapped;
        mi.userdata1 = &slot;
        slot.map_requested = true;
        slot.map_done = false;
        wgpuBufferMapAsync(slot.readback, WGPUMapMode_Read, 0, (size_t)kQueriesPerSlot * 8, mi);
    }
    d->frame_index++;
    poll(d, false, nullptr);
}

void wait_idle(Device* d) {
    for (FrameSlot& s : d->slots) retire_slot(d, s);
    poll(d, true, nullptr);
}

GpuTimings last_gpu_timings(const Device* d) { return d->last_timings; }
uint32_t last_draw_calls(const Device* d) { return d->encoder ? d->prev_draw_calls : d->draw_calls; }

namespace {
struct MapWait {
    bool done = false;
    bool ok = false;
};
void on_capture_mapped(WGPUMapAsyncStatus status, WGPUStringView, void* user, void*) {
    MapWait* w = (MapWait*)user;
    w->done = true;
    w->ok = status == WGPUMapAsyncStatus_Success;
}
}  // namespace

bool capture_rgba8(Device* d, uint8_t* out) {
    if (!d->offscreen) return false;
    const uint32_t row = d->width * 4;
    const uint32_t padded = (row + 255u) & ~255u;
    WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
    bd.label = sv("capture");
    bd.size = (uint64_t)padded * d->height;
    bd.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer buf = wgpuDeviceCreateBuffer(d->device, &bd);
    WGPUCommandEncoder enc = wgpuDeviceCreateCommandEncoder(d->device, nullptr);
    WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    src.texture = d->offscreen;
    WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    dst.buffer = buf;
    dst.layout.bytesPerRow = padded;
    dst.layout.rowsPerImage = d->height;
    WGPUExtent3D size = {d->width, d->height, 1};
    wgpuCommandEncoderCopyTextureToBuffer(enc, &src, &dst, &size);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(enc, nullptr);
    wgpuQueueSubmit(d->queue, 1, &cb);
    wgpuCommandBufferRelease(cb);
    wgpuCommandEncoderRelease(enc);

    MapWait wait;
    WGPUBufferMapCallbackInfo mi = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mi.mode = WGPUCallbackMode_AllowProcessEvents;
    mi.callback = on_capture_mapped;
    mi.userdata1 = &wait;
    wgpuBufferMapAsync(buf, WGPUMapMode_Read, 0, (size_t)bd.size, mi);
    while (!wait.done) poll(d, true, nullptr);
    if (wait.ok) {
        const uint8_t* p = (const uint8_t*)wgpuBufferGetConstMappedRange(buf, 0, (size_t)bd.size);
        const bool bgra = d->color_format == WGPUTextureFormat_BGRA8UnormSrgb;
        for (uint32_t y = 0; y < d->height; ++y) {
            memcpy(out + (size_t)y * row, p + (size_t)y * padded, row);
            if (bgra) {
                uint8_t* r = out + (size_t)y * row;
                for (uint32_t x = 0; x < d->width; ++x) {
                    const uint8_t t = r[x * 4];
                    r[x * 4] = r[x * 4 + 2];
                    r[x * 4 + 2] = t;
                }
            }
        }
        wgpuBufferUnmap(buf);
    } else {
        log_error(kErrGpuReadback, "frame capture readback failed");
    }
    wgpuBufferRelease(buf);
    return wait.ok;
}

}  // namespace zaro::rhi
