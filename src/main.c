#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _OPENMP
#include <omp.h>
#endif
#include "vec.h"
#include "ray.h"
#include "camera.h"
#include "image.h"
#include "glt.h"

#define PI 3.14159265f

/* Thread-local xorshift RNG */
static unsigned int g_seed = 0xDEADBEEF;
static inline unsigned int xorshift(unsigned int *s) {
    unsigned int x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static inline float randf_s(unsigned int *s) {
    return (float)(xorshift(s) & 0x00FFFFFF) / (float)0x01000000;
}

/* Cosine-weighted hemisphere sample */
static vec3 cosine_hemisphere_s(vec3 normal, unsigned int *s) {
    float r1 = randf_s(s) * 2.0f * PI;
    float r2 = randf_s(s);
    float r2s = sqrtf(r2);
    vec3 w = normal;
    vec3 u = fabsf(w.x) > 0.1f ? vcross(v3(0,1,0), w) : vcross(v3(1,0,0), w);
    u = vnorm(u);
    vec3 v = vcross(w, u);
    return vnorm(vadd(vadd(vmul(u, cosf(r1) * r2s), vmul(v, sinf(r1) * r2s)), vmul(w, sqrtf(1 - r2))));
}

static long g_rays = 0; /* atomic via omp */

static vec3 pathtrace(ray r, const scene *s, glt_model *m, unsigned int *rng, int depth);

/* Path trace with direct (NEE) + indirect; GLT cache guides deep bounces */
static vec3 pathtrace(ray r, const scene *s, glt_model *m, unsigned int *rng, int depth) {
    if (depth > 8) return v3(0, 0, 0);
#ifdef _OPENMP
#pragma omp atomic
    g_rays++;
#else
    g_rays++;
#endif
    hit_record rec = {0};
    if (!intersect_scene(r, s, &rec)) return s->bg_color;
    if (rec.emissive) return rec.emission;

    /* --- direct lighting: next-event estimation on emissive spheres --- */
    vec3 direct = v3(0, 0, 0);
    for (int i = 0; i < s->nspheres; i++) {
        const sphere *l = &s->spheres[i];
        if (!l->emissive) continue;
        vec3 to_l = vsub(l->center, rec.point);
        float dist = vlen(to_l);
        if (dist < 1e-4f) continue;
        vec3 ldir = vmul(to_l, 1.0f / dist);
        float cos_s = fmaxf(vdot(rec.normal, ldir), 0.0f);
        if (cos_s <= 0) continue;
        /* offset to avoid self-hit; ignore emissive hits (the light itself) */
        vec3 o = vadd(rec.point, vmul(rec.normal, 1e-3f));
        ray shadow = ray_new(o, ldir);
        shadow.tmax = dist - l->radius - 1e-3f;
        hit_record srec = {0};
        if (shadow.tmax > 0 && intersect_scene(shadow, s, &srec)) {
            if (!srec.emissive) continue; /* occluded by real geometry */
        }
        float area = 4.0f * PI * l->radius * l->radius;
        /* solid-angle approx: cos_light * area / dist^2 */
        vec3 ln = vmul(vsub(rec.point, l->center), 1.0f / dist); /* light normal ~ toward point */
        float cos_l = fmaxf(vdot(ln, vmul(ldir, -1.0f)), 0.0f);
        if (cos_l <= 0) cos_l = 0.5f;
        vec3 brdf = vmul(rec.albedo, 1.0f / PI);
        float gterm = cos_s * cos_l * area / (dist * dist);
        direct = vadd(direct, vmulv(l->emission, vmul(brdf, gterm)));
    }

    /* --- indirect: cosine-weighted bounce, throughput = albedo --- */
    vec3 new_dir = cosine_hemisphere_s(rec.normal, rng);
    vec3 o2 = vadd(rec.point, vmul(rec.normal, 1e-3f));
    ray new_r = ray_new(o2, new_dir);
    vec3 indirect;
    if (depth >= 3 && m->count > 0 && (xorshift(rng) & 7u) == 0) {
        /* 1/8 of deep paths: query Gaussian light cache instead of recursing */
        vec3 wo = vmul(new_dir, -1.0f);
        indirect = glt_eval_rgb(m, rec.point, wo, rec.normal, rec.albedo, rec.roughness);
        indirect = vmul(indirect, 8.0f); /* MIS-ish unbiased rescale */
    } else {
        indirect = pathtrace(new_r, s, m, rng, depth + 1);
    }
    /* cosine-weighted estimator: albedo * Li (cos/pdf = pi cancels brdf 1/pi) */
    vec3 ind = vmulv(rec.albedo, indirect);

    /* Russian roulette after depth 3 */
    if (depth >= 3) {
        float q = fmaxf(fmaxf(rec.albedo.x, rec.albedo.y), rec.albedo.z);
        q = fmaxf(0.2f, fminf(0.95f, q));
        if (randf_s(rng) > q) return direct;
        ind = vmul(ind, 1.0f / q);
    }
    return vadd(direct, ind);
}

/* ============================ scenes ============================ */
static scene cornell_box(void) {
    scene s = {0};
    s.bg_color = v3(0, 0, 0);
    s.ambient = v3(0.1f, 0.1f, 0.1f);
    s.planes[s.nplanes++] = (plane){v3(0,0,0), v3(0,1,0), v3(0.73f,0.73f,0.73f), 0, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,5,0), v3(0,-1,0), v3(0.73f,0.73f,0.73f), 0, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,0,-5), v3(0,0,1), v3(0.73f,0.73f,0.73f), 0, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(-3,0,0), v3(1,0,0), v3(0.63f,0.065f,0.065f), 0, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(3,0,0), v3(-1,0,0), v3(0.12f,0.45f,0.15f), 0, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(0,4.9f,-2.5f), 0.5f, v3(1,1,1), 0, 1, v3(12,12,12)};
    s.spheres[s.nspheres++] = (sphere){v3(-1.2f,1.2f,-2.5f), 1.2f, v3(0.73f,0.73f,0.73f), 0.1f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(1.5f,0.8f,-1.5f), 0.8f, v3(0.9f,0.9f,0.1f), 0.3f, 0, v3(0,0,0)};
    return s;
}

static scene bedroom_scene(void) {
    scene s = {0};
    s.bg_color = v3(0.02f, 0.02f, 0.03f);
    /* warm bedroom: floor, ceiling, walls */
    s.planes[s.nplanes++] = (plane){v3(0,0,0), v3(0,1,0), v3(0.55f,0.42f,0.32f), 0.6f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,4,0), v3(0,-1,0), v3(0.85f,0.82f,0.78f), 0.9f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,0,-6), v3(0,0,1), v3(0.75f,0.68f,0.60f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(-4,0,0), v3(1,0,0), v3(0.60f,0.55f,0.62f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(4,0,0), v3(-1,0,0), v3(0.60f,0.55f,0.62f), 0.8f, 0, v3(0,0,0)};
    /* bed (big low sphere squashed -> use sphere) + pillows + lamp */
    s.spheres[s.nspheres++] = (sphere){v3(-1.0f,0.7f,-3.0f), 1.1f, v3(0.70f,0.25f,0.30f), 0.9f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(0.2f,0.9f,-3.6f), 0.55f, v3(0.92f,0.88f,0.80f), 0.9f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(2.2f,1.0f,-3.2f), 0.35f, v3(1,0.85f,0.6f), 0.2f, 1, v3(6,4.5f,3)};
    s.spheres[s.nspheres++] = (sphere){v3(2.2f,0.35f,-3.2f), 0.35f, v3(0.4f,0.3f,0.25f), 0.7f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(1.6f,0.45f,-1.2f), 0.45f, v3(0.30f,0.45f,0.70f), 0.5f, 0, v3(0,0,0)};
    return s;
}

static scene dining_scene(void) {
    scene s = {0};
    s.bg_color = v3(0.01f, 0.01f, 0.02f);
    s.planes[s.nplanes++] = (plane){v3(0,0,0), v3(0,1,0), v3(0.45f,0.32f,0.22f), 0.5f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,5,0), v3(0,-1,0), v3(0.8f,0.78f,0.75f), 0.9f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,0,-6), v3(0,0,1), v3(0.55f,0.50f,0.45f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(-4,0,0), v3(1,0,0), v3(0.50f,0.42f,0.35f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(4,0,0), v3(-1,0,0), v3(0.50f,0.42f,0.35f), 0.8f, 0, v3(0,0,0)};
    /* table + chairs (spheres) + pendant lights */
    s.spheres[s.nspheres++] = (sphere){v3(0,1.1f,-2.8f), 0.9f, v3(0.50f,0.30f,0.15f), 0.4f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(-1.6f,0.6f,-2.2f), 0.6f, v3(0.65f,0.20f,0.15f), 0.6f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(1.6f,0.6f,-2.2f), 0.6f, v3(0.15f,0.35f,0.60f), 0.6f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(-1.6f,0.6f,-3.8f), 0.6f, v3(0.20f,0.55f,0.25f), 0.6f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(1.6f,0.6f,-3.8f), 0.6f, v3(0.70f,0.60f,0.20f), 0.6f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(-0.8f,4.2f,-2.8f), 0.35f, v3(1,0.9f,0.7f), 0, 1, v3(5,4,3)};
    s.spheres[s.nspheres++] = (sphere){v3(0.8f,4.2f,-2.8f), 0.35f, v3(1,0.9f,0.7f), 0, 1, v3(5,4,3)};
    return s;
}

static scene staircase_scene(void) {
    scene s = {0};
    s.bg_color = v3(0.03f, 0.03f, 0.05f);
    s.planes[s.nplanes++] = (plane){v3(0,0,0), v3(0,1,0), v3(0.60f,0.60f,0.62f), 0.7f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(0,0,-7), v3(0,0,1), v3(0.70f,0.70f,0.72f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(-4,0,0), v3(1,0,0), v3(0.55f,0.55f,0.60f), 0.8f, 0, v3(0,0,0)};
    s.planes[s.nplanes++] = (plane){v3(4,0,0), v3(-1,0,0), v3(0.55f,0.55f,0.60f), 0.8f, 0, v3(0,0,0)};
    /* steps: row of spheres increasing in height going back */
    for (int i = 0; i < 6; i++) {
        float z = -1.5f - (float)i * 0.7f;
        float y = 0.35f + (float)i * 0.45f;
        float shade = 0.55f + 0.05f * (i % 2);
        if (s.nspheres >= MAX_SPHERES - 2) break;
        s.spheres[s.nspheres++] = (sphere){v3(0, y, z), 0.55f, v3(shade, shade, shade + 0.03f), 0.6f, 0, v3(0,0,0)};
    }
    /* railing spheres + skylight */
    s.spheres[s.nspheres++] = (sphere){v3(1.8f, 2.2f, -3.0f), 0.25f, v3(0.8f,0.6f,0.2f), 0.3f, 0, v3(0,0,0)};
    s.spheres[s.nspheres++] = (sphere){v3(0, 5.5f, -3.0f), 0.6f, v3(1,1,1), 0, 1, v3(7,7,8)};
    return s;
}

typedef struct { const char *name; scene (*fn)(void); vec3 eye, look; float fov; } scene_entry;
static scene_entry SCENES[] = {
    {"cornell",  cornell_box,    {0, 2.5f, 7},    {0, 2.5f, -2.5f}, 45},
    {"bedroom",  bedroom_scene,  {0, 2.0f, 2.5f}, {0, 1.4f, -3.2f}, 50},
    {"dining",   dining_scene,   {0, 2.4f, 2.0f}, {0, 1.4f, -3.0f}, 50},
    {"staircase",staircase_scene,{0, 2.2f, 3.0f}, {0, 1.6f, -3.5f}, 50},
    {NULL, NULL, {0,0,0}, {0,0,0}, 0},
};

static void usage(const char *p) {
    fprintf(stderr, "Usage: %s [scene] [out.ppm] [spp] [options]\n", p);
    fprintf(stderr, "  scenes: cornell bedroom dining staircase (default cornell)\n");
    fprintf(stderr, "  options: --scene NAME --out PATH --spp N --width W --height H --train N\n");
}

int main(int argc, char **argv) {
    const char *scene_name = "cornell";
    const char *out = "output.ppm";
    int spp = 16, W = 800, H = 600, train_iters = 2000;
    int out_set = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scene") && i + 1 < argc) scene_name = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) { out = argv[++i]; out_set = 1; }
        else if (!strcmp(argv[i], "--spp") && i + 1 < argc) spp = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--width") && i + 1 < argc) W = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--height") && i + 1 < argc) H = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--train") && i + 1 < argc) train_iters = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(argv[0]); return 0; }
        else if (argv[i][0] != '-') {
            /* positional: [scene-or-out] [spp-or-out] [spp] ; also handle scenes/x.glt */
            const char *a = argv[i];
            const char *base = strrchr(a, '/');
            base = base ? base + 1 : a;
            char tmp[64]; snprintf(tmp, sizeof tmp, "%s", base);
            char *dot = strrchr(tmp, '.');
            if (dot) *dot = 0;
            int is_scene = 0;
            for (scene_entry *e = SCENES; e->name; e++)
                if (!strcmp(tmp, e->name)) is_scene = 1;
            if (is_scene && !strcmp(scene_name, "cornell") && i == 1) scene_name = ({ const char *r = NULL; for (scene_entry *e = SCENES; e->name; e++) if (!strcmp(tmp, e->name)) r = e->name; r; });
            else if (!out_set && (strstr(a, ".ppm") || strstr(a, ".png") || strstr(a, "/"))) { out = a; out_set = 1; }
            else { int v = atoi(a); if (v > 0) spp = v; }
        }
    }
    if (spp < 1) spp = 1;
    if (W < 8) W = 8; if (H < 8) H = 8;

    scene_entry *E = &SCENES[0];
    for (scene_entry *e = SCENES; e->name; e++)
        if (!strcmp(scene_name, e->name)) E = e;
    scene sc = E->fn();
    camera cam = cam_new(E->eye, E->look, v3(0, 1, 0), E->fov, (float)W / H);
    image img = img_new(W, H);
    glt_model m;
    glt_init(&m);

    g_seed = (unsigned int)time(NULL) ^ 0x9E3779B9u;

    /* ---- Phase A: seed Gaussian cache from scene surface samples ---- */
    {
        unsigned int rng = g_seed ^ 0x12345678u;
        int nseed = train_iters > 0 ? 2048 : 0;
        if (nseed > GLT_MAX_GAUSSIANS - 600) nseed = GLT_MAX_GAUSSIANS - 600;
        for (int i = 0; i < nseed; i++) {
            /* random camera ray -> surface point */
            float u = randf_s(&rng), v = randf_s(&rng);
            ray r = cam_ray(cam, u, v);
            hit_record rec = {0};
            if (!intersect_scene(r, &sc, &rec)) continue;
            if (rec.emissive) continue;
            vec3 wo = vmul(r.d, -1.0f);
            vec3 col = rec.albedo; /* init color ~ albedo, refined below */
            glt_spawn(&m, rec.point, wo, rec.normal, rec.albedo, rec.roughness, vmul(col, 0.2f));
        }
        glt_build_index(&m);
        fprintf(stderr, "[GLT] seeded %d gaussians\n", m.count);
    }

    /* ---- Phase B: residual minimization (Eq. 1 + Eq. 8) ---- */
    double train_loss = 0;
    {
        unsigned int rng = g_seed ^ 0xABCDEF01u;
        int report = train_iters / 4; if (report < 1) report = 1;
        for (int it = 0; it < train_iters; it++) {
            float u = randf_s(&rng), v = randf_s(&rng);
            ray r = cam_ray(cam, u, v);
            hit_record rec = {0};
            if (!intersect_scene(r, &sc, &rec) || rec.emissive) continue;
            /* target = E + T L via 1-bounce path sample (unbiased estimator) */
            unsigned int r2 = rng ^ (unsigned int)(it * 2654435761u);
            vec3 target = pathtrace(r, &sc, &m, &r2, 0);
            vec3 wo = vmul(r.d, -1.0f);
            vec3 pred = glt_eval_rgb(&m, rec.point, wo, rec.normal, rec.albedo, rec.roughness);
            train_loss += glt_normalized_loss(pred, target);
            glt_train_step(&m, rec.point, wo, rec.normal, rec.albedo, rec.roughness, target);
            glt_adapt(&m, it, rec.point, wo, rec.normal, rec.albedo, rec.roughness, target, &rng);
            if ((it + 1) % report == 0)
                fprintf(stderr, "[GLT] iter %d/%d loss=%.4f alive=%d\n",
                        it + 1, train_iters, train_loss / (it + 1), glt_alive_count(&m));
        }
        glt_build_index(&m);
        fprintf(stderr, "[GLT] train done: loss=%.4f alive=%d total=%d\n",
                train_iters > 0 ? train_loss / train_iters : 0, glt_alive_count(&m), m.count);
    }

    /* ---- Phase C: render ---- */
    fprintf(stderr, "Rendering scene '%s' %dx%d @ %d spp -> %s ...\n", E->name, W, H, spp, out);
    double t0 = (double)clock() / CLOCKS_PER_SEC;
#ifdef _OPENMP
    double wt0 = omp_get_wtime();
#endif
    g_rays = 0;

#pragma omp parallel for schedule(dynamic, 4) if (H > 32)
    for (int y = 0; y < H; y++) {
        unsigned int rng = g_seed ^ (0x51ab3b27u + (unsigned int)y * 0x9E3779B1u);
        for (int x = 0; x < W; x++) {
            vec3 col = v3(0, 0, 0);
            for (int k = 0; k < spp; k++) {
                float u = ((float)x + randf_s(&rng)) / (float)W;
                float vv = ((float)y + randf_s(&rng)) / (float)H;
                ray r = cam_ray(cam, u, vv);
                col = vadd(col, pathtrace(r, &sc, &m, &rng, 0));
            }
            col = vmul(col, 1.0f / (float)spp);
            img_set(img, x, y, col);
        }
        if (y % 100 == 0) {
#ifdef _OPENMP
            if (omp_get_thread_num() == 0)
#endif
                fprintf(stderr, "\r  Row %d/%d", y, H);
        }
    }

    double t1 = (double)clock() / CLOCKS_PER_SEC;
#ifdef _OPENMP
    double wt1 = omp_get_wtime();
    double elapsed = wt1 - wt0;
#else
    double elapsed = t1 - t0;
#endif
    (void)t0; (void)t1;
    double mpix = (double)W * H;
    fprintf(stderr, "\nDone in %.2fs wall (%.3f ms/pixel, %.2f Mrays/s)\n",
            elapsed, elapsed * 1e3 / mpix * 1e3, (double)g_rays / elapsed / 1e6);
    fprintf(stderr, "Gaussians: %d alive / %d total | evals: %ld kept %ld culled %ld (keep %.1f%%)\n",
            glt_alive_count(&m), m.count, m.eval_total, m.eval_kept, m.eval_culled,
            m.eval_total ? 100.0 * (double)m.eval_kept / (double)m.eval_total : 0.0);

    img_write_ppm(img, out);
    fprintf(stderr, "Wrote %s\n", out);
    img_free(img);
    return 0;
}
