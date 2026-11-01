// Minimal vector/matrix math. Matrices are column-major (m[col*4 + row]), clip = M * v,
// which is the layout the Slang shaders expect for float4x4 uniforms.
#pragma once

#include <math.h>

namespace zaro {

struct Vec3 {
    float x, y, z;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline Vec3 normalize(Vec3 a) {
    const float len = sqrtf(dot(a, a));
    return len > 0.0f ? a * (1.0f / len) : a;
}

struct Mat4 {
    float m[16];
};

inline Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int c = 0; c < 4; ++c) {
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    }
    return r;
}

// Right-handed view matrix.
inline Mat4 look_at(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f = normalize(target - eye);
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    return {{s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
             -dot(s, eye), -dot(u, eye), dot(f, eye), 1}};
}

// Right-handed perspective with WebGPU clip depth [0, 1].
inline Mat4 perspective(float fov_y_radians, float aspect, float z_near, float z_far) {
    const float f = 1.0f / tanf(fov_y_radians * 0.5f);
    const float r = z_far / (z_near - z_far);
    return {{f / aspect, 0, 0, 0, 0, f, 0, 0, 0, 0, r, -1, 0, 0, r * z_near, 0}};
}

// Frustum planes (a, b, c, d) with inward normals, unnormalized: inside when dot >= 0.
// Order: left, right, bottom, top, near, far.
inline void frustum_planes(const Mat4& vp, float out[6][4]) {
    auto row = [&](int r, int c) { return vp.m[c * 4 + r]; };
    for (int c = 0; c < 4; ++c) {
        out[0][c] = row(3, c) + row(0, c);
        out[1][c] = row(3, c) - row(0, c);
        out[2][c] = row(3, c) + row(1, c);
        out[3][c] = row(3, c) - row(1, c);
        out[4][c] = row(2, c);
        out[5][c] = row(3, c) - row(2, c);
    }
    for (int p = 0; p < 6; ++p) {
        const float len = sqrtf(out[p][0] * out[p][0] + out[p][1] * out[p][1] + out[p][2] * out[p][2]);
        for (int c = 0; c < 4; ++c) out[p][c] /= len;
    }
}

}  // namespace zaro
