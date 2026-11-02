// Per-frame constants shared by every crowd shader. Mirrors render::FrameConstants in
// src/render/crowd_renderer.cpp; keep the two in the same order and size (352 bytes).
module frame;

public struct Frame {
    public float4x4 view_proj;  // column-major, clip = view_proj * world
    public float4 planes[6];    // frustum planes, inside when dot(plane.xyz, p) + plane.w >= 0
    public float4 player;       // x, z, time seconds, arena size
    public float4 camera;       // world position (xyz), metres from it where fog starts (w)
    public uint enemy_count;
    public uint obstacle_count;
    public uint2 pad;
    public float4 sprite;  // enemy sprites: metres per pixel, fps, frame count, cull radius (0: cubes)
    public float4 right;   // the camera's right vector (xyz), for sprites facing it
    public float4 shape;        // enemy shapes: kind, sides, size, thickness
    public float4 shape_color;  // linear rgb times intensity, opacity
    public float4 shape_glow;   // glow, glow strength, additive (1/0), hue jitter
    public float4 ground;       // arena colours: ground rgb, w 1 when set (0: grey checkerboard)
    public float4 grid;         // grid rgb, spacing
    public float4 obstacle;     // obstacle rgb
    public float4 sky;          // background and fog rgb
};
