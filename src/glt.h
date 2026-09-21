#ifndef GLT_H
#define GLT_H
/*
 * Gaussian Light Transport (SIGGRAPH Asia 2026, arXiv:2609.11430) — CPU core.
 *
 * 13D Gaussian mixture over position(3) x direction(3) x normal(3) x
 * albedo(3) x roughness(1), separable covariance (Eq. 4):
 *   G(q) = prod over subspaces exp(-|| (q_s - mu_s)/sigma_s ||^2)
 * Residual minimization (Eq. 1): theta* = argmin ||L_theta - E - T L_theta||^2
 * Normalized loss (Eq. 8): || r_theta / (L_theta + eps) ||^2
 * Tile-based Morton culling (Sec. 3.1): only ~76 of 22K kernels per pixel.
 */
#include "vec.h"
#include "ray.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GLT_MAX_GAUSSIANS 65536
#define GLT_TILE_SIZE 8
#define GLT_CULL_THRESHOLD -14.0f
#define GLT_GRID_RES 16          /* 16^3 spatial hash cells over scene bbox */
#define GLT_EPS 1e-3f

typedef struct {
    vec3 mean_pos;        /* 3D position on surface */
    vec3 mean_dir;        /* 3D direction (outgoing) */
    vec3 mean_normal;     /* 3D surface normal */
    vec3 mean_albedo;     /* 3D albedo */
    float mean_roughness; /* 1D roughness */
    vec3 color;           /* radiance coefficient (RGB) */
    vec3 scale_pos;       /* position log-scale (per axis) */
    vec3 scale_dir;       /* direction log-scale (per axis) */
    float scale_norm;     /* normal log-scale */
    float scale_rough;    /* roughness log-scale */
    float importance;     /* pruning metric (accumulated |color|*weight) */
    unsigned int morton;  /* spatial hash key for culling */
    int alive;
} glt_gaussian;

typedef struct {
    glt_gaussian gaussians[GLT_MAX_GAUSSIANS];
    int count;
    int max_count;
    float learning_rate;
    /* spatial index: sorted gaussian indices + cell ranges */
    int sorted[GLT_MAX_GAUSSIANS];
    int cell_start[GLT_GRID_RES*GLT_GRID_RES*GLT_GRID_RES + 1];
    int index_built;
    /* stats */
    long eval_total;   /* gaussian evaluations attempted */
    long eval_culled;  /* skipped by culling */
    long eval_kept;
} glt_model;

/* ---------- separable Gaussian factors (Eq. 4) ---------- */
static inline float gaussian_eval_1d(float x, float mean, float scale) {
    float d = (x - mean) * expf(-scale);
    return d * d;
}

static inline float gaussian_eval_3d(vec3 x, vec3 mean, vec3 scale) {
    vec3 d = vsub(x, mean);
    d = vmulv(d, v3(expf(-scale.x), expf(-scale.y), expf(-scale.z)));
    return vdot(d, d);
}

/* ---------- Morton code (Sec. 3.1) ---------- */
static inline unsigned int glt_expand_bits(unsigned int x) {
    x &= 0x3ffu; /* 10 bits */
    x = (x ^ (x << 16)) & 0x030000ffu;
    x = (x ^ (x << 8))  & 0x0300f00fu;
    x = (x ^ (x << 4))  & 0x030c30c3u;
    x = (x ^ (x << 2))  & 0x09249249u;
    return x;
}
/* scene bbox used for quantization */
#define GLT_BBOX_MIN_X -4.0f
#define GLT_BBOX_MIN_Y -0.5f
#define GLT_BBOX_MIN_Z -7.0f
#define GLT_BBOX_MAX_X 4.0f
#define GLT_BBOX_MAX_Y 6.0f
#define GLT_BBOX_MAX_Z 8.0f

static inline unsigned int glt_morton_of(vec3 p) {
    float fx = (p.x - GLT_BBOX_MIN_X) / (GLT_BBOX_MAX_X - GLT_BBOX_MIN_X);
    float fy = (p.y - GLT_BBOX_MIN_Y) / (GLT_BBOX_MAX_Y - GLT_BBOX_MIN_Y);
    float fz = (p.z - GLT_BBOX_MIN_Z) / (GLT_BBOX_MAX_Z - GLT_BBOX_MIN_Z);
    if (fx < 0) fx = 0; if (fx > 1) fx = 1;
    if (fy < 0) fy = 0; if (fy > 1) fy = 1;
    if (fz < 0) fz = 0; if (fz > 1) fz = 1;
    unsigned int ix = (unsigned int)(fx * 1023.0f);
    unsigned int iy = (unsigned int)(fy * 1023.0f);
    unsigned int iz = (unsigned int)(fz * 1023.0f);
    return (glt_expand_bits(ix) << 2) | (glt_expand_bits(iy) << 1) | glt_expand_bits(iz);
}

static inline int glt_cell_of_morton(unsigned int m) {
    /* deinterleave top 4 bits of each 10-bit coord -> 16^3 grid */
    unsigned int ix = 0, iy = 0, iz = 0;
    for (int b = 0; b < 4; b++) {
        int bit = 9 - b; /* of the 10-bit coords, take top 4 */
        ix = (ix << 1) | ((m >> (bit * 3 + 2)) & 1u);
        iy = (iy << 1) | ((m >> (bit * 3 + 1)) & 1u);
        iz = (iz << 1) | ((m >> (bit * 3 + 0)) & 1u);
    }
    return (int)((ix * GLT_GRID_RES + iy) * GLT_GRID_RES + iz);
}

/* sort comparator over morton codes */
static glt_model *glt_sort_ctx;
static int glt_cmp_morton(const void *a, const void *b) {
    int ia = *(const int *)a, ib = *(const int *)b;
    unsigned int ma = glt_sort_ctx->gaussians[ia].morton;
    unsigned int mb = glt_sort_ctx->gaussians[ib].morton;
    return (ma > mb) - (ma < mb);
}

/* Build tile/cell index: sort alive gaussians by morton, record cell ranges. */
static inline void glt_build_index(glt_model *m) {
    int n = 0;
    for (int i = 0; i < m->count; i++)
        if (m->gaussians[i].alive) m->sorted[n++] = i;
    glt_sort_ctx = m;
    qsort(m->sorted, (size_t)n, sizeof(int), glt_cmp_morton);
    int ncells = GLT_GRID_RES*GLT_GRID_RES*GLT_GRID_RES;
    for (int i = 0; i <= ncells; i++) m->cell_start[i] = 0;
    /* count per cell */
    for (int i = 0; i < n; i++) {
        int c = glt_cell_of_morton(m->gaussians[m->sorted[i]].morton);
        if (c < 0) c = 0; if (c >= ncells) c = ncells - 1;
        m->cell_start[c + 1]++;
    }
    for (int i = 0; i < ncells; i++) m->cell_start[i + 1] += m->cell_start[i];
    /* NOTE: sorted is currently grouped only by counting; do a stable
       placement pass via temp buffer on stack is too big, so instead we
       reorder with insertion using counts — simpler: re-sort is already
       grouped, compute start offsets by scanning (sorted order). */
    int cur = -1, start = 0;
    int tmp_start[GLT_GRID_RES*GLT_GRID_RES*GLT_GRID_RES + 1];
    for (int i = 0; i <= ncells; i++) tmp_start[i] = -1;
    for (int i = 0; i < n; i++) {
        int c = glt_cell_of_morton(m->gaussians[m->sorted[i]].morton);
        if (c < 0) c = 0; if (c >= ncells) c = ncells - 1;
        if (c != cur) { cur = c; start = i; tmp_start[c] = start; }
    }
    /* fill gaps: cell_start[c] = first index with cell >= c */
    int next = n;
    for (int c = ncells; c >= 0; c--) {
        if (c < ncells && tmp_start[c] >= 0) next = tmp_start[c];
        m->cell_start[c] = next;
    }
    m->index_built = 1;
}

/* ---------- full RGB evaluation with culling ---------- */
static inline vec3 glt_eval_rgb(glt_model *m, vec3 pos, vec3 dir, vec3 norm,
                                vec3 albedo, float roughness) {
    vec3 L = v3(0, 0, 0);
    /* fast path: if index built, only visit gaussians in the 27 neighbor
       cells of the query point + fall back to global if cell empty. */
    if (m->index_built && m->count > 512) {
        float fx = (pos.x - GLT_BBOX_MIN_X) / (GLT_BBOX_MAX_X - GLT_BBOX_MIN_X) * GLT_GRID_RES;
        float fy = (pos.y - GLT_BBOX_MIN_Y) / (GLT_BBOX_MAX_Y - GLT_BBOX_MIN_Y) * GLT_GRID_RES;
        float fz = (pos.z - GLT_BBOX_MIN_Z) / (GLT_BBOX_MAX_Z - GLT_BBOX_MIN_Z) * GLT_GRID_RES;
        int cx = (int)fx, cy = (int)fy, cz = (int)fz;
        if (cx < 0) cx = 0; if (cx >= GLT_GRID_RES) cx = GLT_GRID_RES - 1;
        if (cy < 0) cy = 0; if (cy >= GLT_GRID_RES) cy = GLT_GRID_RES - 1;
        if (cz < 0) cz = 0; if (cz >= GLT_GRID_RES) cz = GLT_GRID_RES - 1;
        int visited = 0;
        for (int dx = -1; dx <= 1; dx++)
        for (int dy = -1; dy <= 1; dy++)
        for (int dz = -1; dz <= 1; dz++) {
            int ax = cx + dx, ay = cy + dy, az = cz + dz;
            if (ax < 0 || ay < 0 || az < 0 ||
                ax >= GLT_GRID_RES || ay >= GLT_GRID_RES || az >= GLT_GRID_RES) continue;
            int cell = (ax * GLT_GRID_RES + ay) * GLT_GRID_RES + az;
            for (int k = m->cell_start[cell]; k < m->cell_start[cell + 1]; k++) {
                glt_gaussian *g = &m->gaussians[m->sorted[k]];
                visited++;
                /* cheap position pre-cull before full 13D eval */
                float epos = gaussian_eval_3d(pos, g->mean_pos, g->scale_pos);
                if (epos > 9.0f) { m->eval_culled++; continue; }
                float edir = gaussian_eval_3d(dir, g->mean_dir, g->scale_dir);
                float enrm = gaussian_eval_3d(norm, g->mean_normal,
                    v3(g->scale_norm, g->scale_norm, g->scale_norm));
                float ealb = gaussian_eval_3d(albedo, g->mean_albedo, v3(1, 1, 1));
                float erou = gaussian_eval_1d(roughness, g->mean_roughness, g->scale_rough);
                float exponent = -(epos + edir + enrm + ealb + erou);
                if (exponent < GLT_CULL_THRESHOLD) { m->eval_culled++; continue; }
                float w = expf(exponent);
                m->eval_kept++;
                L = vadd(L, vmul(g->color, w));
                g->importance += w * (fabsf(g->color.x) + fabsf(g->color.y) + fabsf(g->color.z));
            }
        }
        m->eval_total += visited;
        return L;
    }
    for (int i = 0; i < m->count; i++) {
        glt_gaussian *g = &m->gaussians[i];
        if (!g->alive) continue;
        m->eval_total++;
        float epos = gaussian_eval_3d(pos, g->mean_pos, g->scale_pos);
        if (epos > 9.0f) { m->eval_culled++; continue; }
        float edir = gaussian_eval_3d(dir, g->mean_dir, g->scale_dir);
        float enrm = gaussian_eval_3d(norm, g->mean_normal,
            v3(g->scale_norm, g->scale_norm, g->scale_norm));
        float ealb = gaussian_eval_3d(albedo, g->mean_albedo, v3(1, 1, 1));
        float erou = gaussian_eval_1d(roughness, g->mean_roughness, g->scale_rough);
        float exponent = -(epos + edir + enrm + ealb + erou);
        if (exponent < GLT_CULL_THRESHOLD) { m->eval_culled++; continue; }
        float w = expf(exponent);
        m->eval_kept++;
        L = vadd(L, vmul(g->color, w));
        g->importance += w * (fabsf(g->color.x) + fabsf(g->color.y) + fabsf(g->color.z));
    }
    return L;
}

/* scalar (luminance) wrapper kept for compatibility */
static inline float glt_eval(glt_model *m, vec3 pos, vec3 dir, vec3 norm,
                             vec3 albedo, float roughness) {
    vec3 L = glt_eval_rgb(m, pos, dir, norm, albedo, roughness);
    return (L.x + L.y + L.z) / 3.0f;
}

static inline void glt_init(glt_model *m) {
    memset(m, 0, sizeof(glt_model));
    m->count = 0;
    m->max_count = GLT_MAX_GAUSSIANS;
    m->learning_rate = 0.01f;
}

static inline void glt_spawn(glt_model *m, vec3 pos, vec3 dir, vec3 norm,
                             vec3 albedo, float roughness, vec3 color) {
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
    g->importance = 1.0f;
    g->morton = glt_morton_of(pos);
    g->alive = 1;
    m->index_built = 0;
}

/* Split highest-importance gaussian into two children (every 400 iters). */
static inline void glt_split(glt_model *m) {
    int best = -1;
    float best_imp = -1.0f;
    for (int i = 0; i < m->count; i++) {
        if (!m->gaussians[i].alive) continue;
        if (m->gaussians[i].importance > best_imp) {
            best_imp = m->gaussians[i].importance;
            best = i;
        }
    }
    if (best < 0 || m->count + 1 >= m->max_count) return;
    glt_gaussian parent = m->gaussians[best];
    float px = expf(parent.scale_pos.x);
    vec3 off = v3(px * 0.5f, 0, 0);
    for (int s = -1; s <= 1; s += 2) {
        if (m->count >= m->max_count) break;
        glt_gaussian *g = &m->gaussians[m->count++];
        *g = parent;
        g->mean_pos = vadd(parent.mean_pos, vmul(off, (float)s));
        g->scale_pos = vsub(parent.scale_pos, v3(0.35f, 0.35f, 0.35f));
        g->color = vmul(parent.color, 0.5f);
        g->importance = parent.importance * 0.5f;
        g->morton = glt_morton_of(g->mean_pos);
        g->alive = 1;
    }
    m->gaussians[best].alive = 0; /* replace parent by children */
    m->index_built = 0;
}

/* Spawn a batch of new kernels around a seed point (500 every 500 iters). */
static inline void glt_spawn_batch(glt_model *m, vec3 pos, vec3 dir, vec3 norm,
                                   vec3 albedo, float roughness, vec3 color, int n,
                                   unsigned int *rng) {
    for (int i = 0; i < n; i++) {
        if (m->count >= m->max_count) return;
        /* xorshift jitter */
        *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
        float j1 = ((float)(*rng & 0xffff) / 65535.0f - 0.5f);
        *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
        float j2 = ((float)(*rng & 0xffff) / 65535.0f - 0.5f);
        *rng ^= *rng << 13; *rng ^= *rng >> 17; *rng ^= *rng << 5;
        float j3 = ((float)(*rng & 0xffff) / 65535.0f - 0.5f);
        vec3 p = v3(pos.x + j1 * 1.5f, pos.y + j2 * 1.5f, pos.z + j3 * 1.5f);
        glt_spawn(m, p, dir, norm, albedo, roughness, vmul(color, 1.0f / (float)(n > 0 ? n : 1)));
    }
}

/* Prune kernels below importance threshold (every 2000 iters). */
static inline int glt_prune(glt_model *m, float threshold) {
    int killed = 0;
    for (int i = 0; i < m->count; i++) {
        if (m->gaussians[i].alive && m->gaussians[i].importance < threshold) {
            m->gaussians[i].alive = 0;
            killed++;
        }
    }
    m->index_built = 0;
    return killed;
}

/* Normalized residual loss (Eq. 8): || (L - E - T L) / (L + eps) ||^2.
   L_pred: cache prediction, L_target = E + T*L estimate from path tracer. */
static inline float glt_normalized_loss(vec3 L_pred, vec3 L_target) {
    vec3 r = vsub(L_pred, L_target);
    vec3 denom = v3(L_pred.x + GLT_EPS, L_pred.y + GLT_EPS, L_pred.z + GLT_EPS);
    vec3 n = v3(r.x / denom.x, r.y / denom.y, r.z / denom.z);
    return vdot(n, n);
}

/* One SGD step on kernel colors toward target (gradient of Eq. 8, simplified):
   moves each contributing kernel's color along -lr * normalized residual.
   denom uses (pred + 1) for stability; updates are clamped. */
static inline void glt_train_step(glt_model *m, vec3 pos, vec3 dir, vec3 norm,
                                  vec3 albedo, float roughness, vec3 target) {
    vec3 pred = glt_eval_rgb(m, pos, dir, norm, albedo, roughness);
    vec3 denom = v3(pred.x + 1.0f, pred.y + 1.0f, pred.z + 1.0f);
    vec3 nres = v3((pred.x - target.x) / denom.x,
                   (pred.y - target.y) / denom.y,
                   (pred.z - target.z) / denom.z);
    /* clamp normalized residual to avoid spikes when pred ~ 0 */
    nres.x = fmaxf(-2.0f, fminf(2.0f, nres.x));
    nres.y = fmaxf(-2.0f, fminf(2.0f, nres.y));
    nres.z = fmaxf(-2.0f, fminf(2.0f, nres.z));
    float lr = m->learning_rate;
    for (int i = 0; i < m->count; i++) {
        glt_gaussian *g = &m->gaussians[i];
        if (!g->alive) continue;
        float epos = gaussian_eval_3d(pos, g->mean_pos, g->scale_pos);
        if (epos > 9.0f) continue;
        float edir = gaussian_eval_3d(dir, g->mean_dir, g->scale_dir);
        float enrm = gaussian_eval_3d(norm, g->mean_normal,
            v3(g->scale_norm, g->scale_norm, g->scale_norm));
        float exponent = -(epos + edir + enrm);
        if (exponent < GLT_CULL_THRESHOLD) continue;
        float w = expf(exponent);
        g->color = vsub(g->color, vmul(nres, lr * w));
        /* keep colors non-negative */
        if (g->color.x < 0) g->color.x = 0;
        if (g->color.y < 0) g->color.y = 0;
        if (g->color.z < 0) g->color.z = 0;
    }
}

/* Adaptation schedule from the paper. */
static inline void glt_adapt(glt_model *m, int iter, vec3 seed_pos, vec3 seed_dir,
                             vec3 seed_norm, vec3 seed_alb, float seed_rough,
                             vec3 seed_color, unsigned int *rng) {
    if (iter > 0 && iter % 400 == 0) glt_split(m);
    if (iter > 0 && iter % 500 == 0)
        glt_spawn_batch(m, seed_pos, seed_dir, seed_norm, seed_alb,
                        seed_rough, seed_color, 500, rng);
    if (iter > 0 && iter % 2000 == 0) glt_prune(m, 1e-5f);
    if (iter % 250 == 0) glt_build_index(m);
}

static inline int glt_alive_count(glt_model *m) {
    int n = 0;
    for (int i = 0; i < m->count; i++)
        if (m->gaussians[i].alive) n++;
    return n;
}

#endif
