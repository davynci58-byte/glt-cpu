#ifndef CAMERA_H
#define CAMERA_H

#include "vec.h"
#include "ray.h"

typedef struct {
    vec3 origin;
    vec3 lower_left;
    vec3 horizontal;
    vec3 vertical;
} camera;

static inline camera cam_new(vec3 lookfrom, vec3 lookat, vec3 vup, float vfov, float aspect) {
    vec3 u, v, w;
    float theta = vfov * 3.14159265f / 180.0f;
    float half_height = tanf(theta / 2.0f);
    float half_width = aspect * half_height;
    w = vnorm(vsub(lookfrom, lookat));
    u = vnorm(vcross(vup, w));
    v = vcross(w, u);
    return (camera){
        lookfrom,
        vsub(vsub(vadd(lookfrom, vmul(u, -half_width)), vmul(v, -half_height)), w),
        vmul(u, 2.0f * half_width),
        vmul(v, 2.0f * half_height)
    };
}

static inline ray cam_ray(camera c, float s, float t) {
    vec3 dir = vadd(vadd(c.lower_left, vmul(c.horizontal, s)), vmul(c.vertical, t));
    dir = vsub(dir, c.origin);
    return ray_new(c.origin, dir);
}

#endif
