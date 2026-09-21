#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "vec.h"
#include "ray.h"
#include "camera.h"
#include "image.h"
#include "glt.h"

/* Simple hash for pseudo-random */
static unsigned int seed;
static float randf(void) {
    seed = seed * 1664525u + 1013904223u;
    return (float)(seed & 0x00FFFFFF) / (float)0x01000000;
}

/* Cosine-weighted hemisphere sample */
static vec3 cosine_hemisphere(vec3 normal) {
    float r1 = randf() * 2.0f * 3.14159265f;
    float r2 = randf();
    float r2s = sqrtf(r2);
    vec3 w = normal;
    vec3 u = fabsf(w.x) > 0.1f ? vcross(v3(0,1,0), w) : vcross(v3(1,0,0), w);
    u = vnorm(u);
    vec3 v = vcross(w, u);
    return vnorm(vadd(vadd(vmul(u, cosf(r1) * r2s), vmul(v, sinf(r1) * r2s)), vmul(w, sqrtf(1 - r2))));
}

/* Path trace a single sample */
static vec3 pathtrace(ray r, const scene *s, glt_model *m, int depth) {
    if (depth > 8) return v3(0, 0, 0);
    hit_record rec;
    if (!intersect_scene(r, s, &rec)) return s->bg_color;
    if (rec.emissive) return rec.albedo;
    vec3 emitted = v3(0, 0, 0);
    /* Direct light */
    for (int i = 0; i < s->nspheres; i++) {
        sphere *l = &s->spheres[i];
        if (!l->emissive) continue;
        vec3 to_l = vsub(l->center, rec.point);
        float dist = vlen(to_l);
        vec3 ldir = vmul(to_l, 1.0f / dist);
        float cos_theta = fmaxf(vdot(rec.normal, ldir), 0.0f);
        if (cos_theta <= 0) continue;
        ray shadow = ray_new(rec.point, ldir);
        shadow.tmax = dist - 0.01f;
        hit_record srec;
        if (intersect_scene(shadow, s, &srec) && srec.t < dist - 0.01f) continue;
        float pdf = 1.0f / (4.0f * 3.14159265f * l->radius * l->radius);
        float brdf = rec.albedo.x / 3.14159265f;
        emitted = vadd(emitted, vmul(l->emission, cos_theta * brdf / (pdf * dist * dist)));
    }
    /* Indirect light */
    vec3 new_dir = cosine_hemisphere(rec.normal);
    ray new_r = ray_new(rec.point, new_dir);
    vec3 indirect = pathtrace(new_r, s, m, depth + 1);
    vec3 brdf_color = vmul(rec.albedo, 1.0f / 3.14159265f);
    return vadd(emitted, vmulv(brdf_color, indirect));
}

/* Initialize a Cornell box scene */
static scene cornell_box(void) {
    scene s = {0};
    s.ambient = v3(0.1f, 0.1f, 0.1f);
    s.bg_color = v3(0, 0, 0);
    /* Floor */
    s.planes[s.nplanes++] = (plane){v3(0, 0, 0), v3(0, 1, 0), v3(0.73f, 0.73f, 0.73f), 0, 0, v3(0,0,0)};
    /* Ceiling */
    s.planes[s.nplanes++] = (plane){v3(0, 5, 0), v3(0, -1, 0), v3(0.73f, 0.73f, 0.73f), 0, 0, v3(0,0,0)};
    /* Back wall */
    s.planes[s.nplanes++] = (plane){v3(0, 0, -5), v3(0, 0, 1), v3(0.73f, 0.73f, 0.73f), 0, 0, v3(0,0,0)};
    /* Left wall (red) */
    s.planes[s.nplanes++] = (plane){v3(-3, 0, 0), v3(1, 0, 0), v3(0.63f, 0.065f, 0.065f), 0, 0, v3(0,0,0)};
    /* Right wall (green) */
    s.planes[s.nplanes++] = (plane){v3(3, 0, 0), v3(-1, 0, 0), v3(0.12f, 0.45f, 0.15f), 0, 0, v3(0,0,0)};
    /* Light (ceiling area light approximated as small emissive sphere) */
    s.spheres[s.nspheres++] = (sphere){v3(0, 4.9f, -2.5f), 0.5f, v3(1,1,1), 0, 1, v3(8, 8, 8)};
    /* Tall box */
    s.spheres[s.nspheres++] = (sphere){v3(-1.2f, 1.2f, -2.5f), 1.2f, v3(0.73f, 0.73f, 0.73f), 0.1f, 0, v3(0,0,0)};
    /* Small sphere */
    s.spheres[s.nspheres++] = (sphere){v3(1.5f, 0.8f, -1.5f), 0.8f, v3(0.9f, 0.9f, 0.1f), 0.3f, 0, v3(0,0,0)};
    return s;
}

int main(int argc, char **argv) {
    seed = (unsigned int)time(NULL) ^ 0xDEADBEEF;
    int W = 800, H = 600;
    int spp = 64;
    const char *out = "output.ppm";
    if (argc > 1) out = argv[1];
    if (argc > 2) spp = atoi(argv[2]);
    if (spp < 1) spp = 1;

    scene s = cornell_box();
    camera c = cam_new(v3(0, 2.5f, 7), v3(0, 2.5f, -2.5f), v3(0, 1, 0), 45, (float)W / H);
    image img = img_new(W, H);
    glt_model m;
    glt_init(&m);

    fprintf(stderr, "Rendering %dx%d @ %d spp...\n", W, H, spp);
    clock_t start = clock();

    for (int y = 0; y < H; y++) {
        if (y % 50 == 0) fprintf(stderr, "\r  Row %d/%d", y, H);
        for (int x = 0; x < W; x++) {
            vec3 col = v3(0, 0, 0);
            for (int s = 0; s < spp; s++) {
                float u = (x + randf()) / W;
                float v = (y + randf()) / H;
                ray r = cam_ray(c, u, v);
                col = vadd(col, pathtrace(r, &s, &m, 0));
            }
            col = vmul(col, 1.0f / spp);
            img_set(img, x, y, col);
        }
    }

    clock_t end = clock();
    float elapsed = (float)(end - start) / CLOCKS_PER_SEC;
    fprintf(stderr, "\nDone in %.2fs (%.1f ms/pixel)\n", elapsed, elapsed * 1e6f / (W * H));
    fprintf(stderr, "Gaussians: %d alive / %d total\n", glt_alive_count(&m), m.count);

    img_write_ppm(img, out);
    fprintf(stderr, "Wrote %s\n", out);
    img_free(img);
    return 0;
}
