// GPU frustum culling: compacts visible enemy indices and writes the instance count of the
// indirect draw (args[1]). args[0] (vertex count) is set by the CPU when the buffer is reset.
import frame;

[[vk::binding(0, 0)]] ConstantBuffer<Frame> frame;
[[vk::binding(1, 0)]] StructuredBuffer<float4> instances;  // x, z, heading x, heading z
[[vk::binding(2, 0)]] RWStructuredBuffer<uint> visible;
[[vk::binding(3, 0)]] RWStructuredBuffer<Atomic<uint>> args;

static const float kCubeRadius = 1.0;

[shader("compute")]
[numthreads(64, 1, 1)]
void cull(uint3 id: SV_DispatchThreadID) {
    const uint i = id.x;
    if (i >= frame.enemy_count) return;
    const float4 inst = instances[i];
    const float3 center = float3(inst.x, 0.5, inst.y);
    const float radius = frame.sprite.w > 0.0 ? frame.sprite.w : kCubeRadius;
    for (int p = 0; p < 6; ++p) {
        if (dot(frame.planes[p].xyz, center) + frame.planes[p].w < -radius) return;
    }
    const uint slot = args[1].add(1);
    visible[slot] = i;
}
