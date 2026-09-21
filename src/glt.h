#ifndef GLT_H
#define GLT_H

#include "vec.h"
#include "ray.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GLT_MAX_GAUSSIANS 65536
#define GLT_TILE_SIZE 8
#define GLT_CULL_THRESHOLD -14.0f

typedef struct {
    vec3 mean_pos;      /* 3D position on surface */
    vec3 mean_dir;      /* 3D direction */
    vec3 mean_normal;   /* 3D surface normal */
    vec3 mean_albedo;   /* 3D albedo */
    float mean_roughness; /* 1D roughness */
    vec3 color;         /* radiance coefficient */
    vec3 scale_pos;     /* position scale (log) */
    vec3 scale_dir;     /* direction scale (log) */
    float scale_norm;   /* normal scale */
    float scale_rough;  /* roughness scale */
    float importance;   /* pruning metric */
    int alive;
} glt_gaussian;

typedef struct {
    glt_gaussian gaussians[GLT_MAX_GAUSSIANS];
    int count;
    int max_count;
    float learning_rate;
} glt_model;

static inline float gaussian_eval_1d(float x, float mean, float scale) {
    float d = (x - mean) * expf(-scale);
    return d * d;
}

static inline float gaussian_eval_3d(vec3 x, vec3 mean, vec3 scale) {
    vec3 d = vsub(x, mean);
    d = vmulv(d, v3(expf(-scale.x), expf(-scale.y), expf(-scale.z)));
    return vdot(d, d);
}

static inline float glt_eval(glt_model *m, vec3 pos, vec3 dir, vec3 norm, vec3 albedo, float roughness) {
    float L = 0;
    for (int i = 0; i < m->count; i++) {
        glt_gaussian *g = &m->gaussians[i];
        if (!g->alive) continue;
        float exp_pos = gaussian_eval_3d(pos, g->mean_pos, g->scale_pos);
        float exp_dir = gaussian_eval_3d(dir, g->mean_dir, g->scale_dir);
        float exp_norm = gaussian_eval_3d(norm, g->mean_normal, v3(g->scale_norm, g->scale_norm, g->scale_norm));
        float exp_alb = gaussian_eval_3d(albedo, g->mean_albedo, v3(1, 1, 1));
        float exp_rou = gaussian_eval_1d(roughness, g->mean_roughness, g->scale_rough);
        float exponent = -(exp_pos + exp_dir + exp_norm + exp_alb + exp_rou);
        if (exponent < GLT_CULL_THRESHOLD) continue;
        float w = expf(exponent);
        L += w * (g->color.x + g->color.y + g->color.z) / 3.0f;
    }
    return L;
}

static inline void glt_init(glt_model *m) {
    memset(m, 0, sizeof(glt_model));
    m->count = 0;
    m->max_count = GLT_MAX_GAUSSIANS;
    m->learning_rate = 0.01f;
}

static inline void glt_spawn(glt_model *m, vec3 pos, vec3 dir, vec3 norm, vec3 albedo, float roughness, vec3 color) {
    if (m->count >= m->max_count) return;
    glt_gaussian *g = &m->gaussians[m->count++];
    g->mean_pos = pos;
    g->mean_dir = dir;
    g->mean_normal = norm;
    g->mean_albedo = albedo;
    g->mean_roughness = roughness;
    g->color = color;
    g->scale_pos = v3(-2, -2, -2);
    g->scale_dir = v3(-2, -2, -2);
    g->scale_norm = -2;
    g->scale_rough = -2;
    g->importance = 0;
    g->alive = 1;
}

static inline void glt_prune(glt_model *m, float threshold) {
    for (int i = 0; i < m->count; i++) {
        if (m->gaussians[i].alive && m->gaussians[i].importance < threshold) {
            m->gaussians[i].alive = 0;
        }
    }
}

static inline int glt_alive_count(glt_model *m) {
    int n = 0;
    for (int i = 0; i < m->count; i++)
        if (m->gaussians[i].alive) n++;
    return n;
}

#endif
