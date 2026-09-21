#ifndef RAY_H
#define RAY_H

#include "vec.h"

typedef struct { vec3 o, d; float tmax; } ray;

typedef struct {
    vec3 albedo;
    vec3 normal;
    float roughness;
    int hit;
    float t;
    vec3 point;
} hit_record;

typedef struct {
    vec3 center;
    float radius;
    vec3 albedo;
    float roughness;
    int emissive;
    vec3 emission;
} sphere;

typedef struct {
    vec3 point;
    vec3 normal;
    vec3 albedo;
    float roughness;
    int emissive;
    vec3 emission;
} plane;

#define MAX_SPHERES 64
#define MAX_PLANES 64

typedef struct {
    sphere spheres[MAX_SPHERES];
    int nspheres;
    plane planes[MAX_PLANES];
    int nplanes;
    vec3 ambient;
    vec3 bg_color;
} scene;

static inline ray ray_new(vec3 o, vec3 d) {
    return (ray){o, vnorm(d), 1e30f};
}

static inline int intersect_sphere(ray r, sphere s, hit_record *rec) {
    vec3 oc = vsub(r.o, s.center);
    float b = vdot(oc, r.d);
    float c = vdot(oc, oc) - s.radius * s.radius;
    float disc = b * b - c;
    if (disc < 0) return 0;
    float t = -b - sqrtf(disc);
    if (t < 0.001f || t > r.tmax) return 0;
    rec->t = t;
    rec->point = vadd(r.o, vmul(r.d, t));
    rec->normal = vnorm(vsub(rec->point, s.center));
    rec->albedo = s.albedo;
    rec->roughness = s.roughness;
    rec->hit = 1;
    rec->emissive = s.emissive;
    return 1;
}

static inline int intersect_plane(ray r, plane p, hit_record *rec) {
    float denom = vdot(p.normal, r.d);
    if (fabsf(denom) < 1e-6f) return 0;
    float t = vdot(vsub(p.point, r.o), p.normal) / denom;
    if (t < 0.001f || t > r.tmax) return 0;
    rec->t = t;
    rec->point = vadd(r.o, vmul(r.d, t));
    rec->normal = p.normal;
    rec->albedo = p.albedo;
    rec->roughness = p.roughness;
    rec->hit = 1;
    rec->emissive = p.emissive;
    return 1;
}

static inline int intersect_scene(ray r, const scene *s, hit_record *rec) {
    rec->hit = 0;
    rec->t = r.tmax;
    for (int i = 0; i < s->nspheres; i++) {
        hit_record tmp = {0};
        tmp.t = rec->t;
        if (intersect_sphere(r, s->spheres[i], &tmp) && tmp.t < rec->t) {
            *rec = tmp;
        }
    }
    for (int i = 0; i < s->nplanes; i++) {
        hit_record tmp = {0};
        tmp.t = rec->t;
        if (intersect_plane(r, s->planes[i], &tmp) && tmp.t < rec->t) {
            *rec = tmp;
        }
    }
    return rec->hit;
}

#endif
