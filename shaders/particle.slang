// Shared by shaders/particles_sim.slang (compute) and shaders/particles_draw.slang (render) of
// render/particle_renderer.cpp: the per-frame constants and one particle in the ring buffer.
module particle;

public struct Sim {
    public float4x4 view_proj;  // column-major, clip = view_proj * world
    public float4 forward;      // the camera's forward vector (xyz), seconds since the last frame (w)
    public uint head;           // ring slot of the frame's first new particle
    public uint emit_total;     // new particles this frame
    public uint capacity;
    public uint burst_count;
};

public struct Particle {
    public float4 position_age;   // world xyz, seconds lived
    public float4 velocity_life;  // metres per second, seconds to live
    public float4 color;
    public float4 size_drag;      // width, drag, shape (0 spark, 1 square, 2 circle, 3 triangle), radians per second
};
