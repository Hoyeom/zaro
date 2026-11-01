// Per-frame constants shared by every crowd shader. Mirrors render::FrameConstants in
// src/render/crowd_renderer.cpp; keep the two in the same order and size (208 bytes).
module frame;

public struct Frame {
    public float4x4 view_proj;  // column-major, clip = view_proj * world
    public float4 planes[6];    // frustum planes, inside when dot(plane.xyz, p) + plane.w >= 0
    public float4 player;       // x, z, time seconds, arena size
    public float4 camera;       // world position
    public uint enemy_count;
    public uint obstacle_count;
    public uint2 pad;
};
