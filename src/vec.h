#ifndef VEC_H
#define VEC_H
/*
 * vec.h — minimal single-precision 3D vector math.
 *
 * Plain-old-data `vec3` plus inline add/sub/mul/dot/cross/normalize
 * helpers used by the path tracer, camera, and Gaussian cache.
 * All functions are `static inline` so the header is self-contained.
 */

#include <math.h>

typedef struct { float x, y, z; } vec3;

static inline vec3 v3(float x, float y, float z) { return (vec3){x, y, z}; }
static inline vec3 vadd(vec3 a, vec3 b) { return v3(a.x+b.x, a.y+b.y, a.z+b.z); }
static inline vec3 vsub(vec3 a, vec3 b) { return v3(a.x-b.x, a.y-b.y, a.z-b.z); }
static inline vec3 vmul(vec3 a, float t) { return v3(a.x*t, a.y*t, a.z*t); }
static inline vec3 vmulv(vec3 a, vec3 b) { return v3(a.x*b.x, a.y*b.y, a.z*b.z); }
static inline float vdot(vec3 a, vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static inline vec3 vcross(vec3 a, vec3 b) {
    return v3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}
static inline float vlen(vec3 v) { return sqrtf(vdot(v, v)); }
static inline vec3 vnorm(vec3 v) { float l = vlen(v); return l > 0 ? vmul(v, 1.0f/l) : v3(0,0,0); }
static inline vec3 vreflect(vec3 v, vec3 n) { return vsub(v, vmul(n, 2.0f * vdot(v, n))); }
static inline float vmax(vec3 v) { float m = v.x; if (v.y > m) m = v.y; if (v.z > m) m = v.z; return m; }
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline vec3 vclamp(vec3 v, float lo, float hi) {
    return v3(clampf(v.x, lo, hi), clampf(v.y, lo, hi), clampf(v.z, lo, hi));
}

#endif
