# Gaussian Light Transport — CPU Ray Tracer

A super-fast CPU ray tracer inspired by [Gaussian Light Transport](https://arxiv.org/abs/2609.11430) (SIGGRAPH Asia 2026).

Represents the solution to the rendering equation as a mixture of Gaussian kernels over positions, directions, normals, and material properties. Optimizes by minimizing the residual of the rendering equation. Achieves real-time global illumination on CPU through efficient tile-based culling and separable covariance evaluation.

## Build

```bash
make
```

Requires `gcc` with OpenMP (`-fopenmp`, included in the Makefile).

## Run

```bash
./glt --scene cornell --out screenshots/cornell.ppm --spp 16 --train 1500
./glt scenes/cornell.glt        # scene descriptor shortcut (basename = scene name)
./glt output.ppm 64             # legacy positional style (cornell, 64 spp)
```

Options: `--scene cornell|bedroom|dining|staircase`, `--out PATH`, `--spp N`,
`--width W`, `--height H`, `--train N` (GLT residual-training iterations).

Output is PPM (P6, gamma 2.0 via sqrt). Convert to PNG if available:

```bash
convert screenshots/cornell.ppm screenshots/cornell.png
```

## Architecture

- `src/vec.h` — SIMD-friendly 3D vector math
- `src/ray.h` — ray-primitive intersection (spheres, planes)
- `src/glt.h` — Gaussian Light Transport core: 13D Gaussian mixture (Eq. 4),
  Morton-code tile culling (Sec. 3.1), residual minimization (Eq. 1),
  normalized loss (Eq. 8), split/spawn/prune adaptation
- `src/camera.h` — pinhole camera model
- `src/image.h` — PPM image output
- `src/main.c` — path tracer (NEE + cosine-weighted bounces, Russian roulette),
  GLT cache seeding/training, OpenMP parallel renderer
- `scenes/*.glt` — scene descriptors | `screenshots/` — 800×600 renders

## Key Techniques (from the paper)

- 13D Gaussians: position (3), direction (3), normal (3), albedo (3), roughness (1)
- Rendering equation residual minimization: θ\* = argmin ||L_θ − E − T·L_θ||²
- Tile-based Morton code culling for fast evaluation
- Separable covariance (product of lower-dimensional Gaussians)
- Splitting (every 400 iters), spawning (500 every 500 iters), pruning (every 2000 iters)
- Normalized loss (Eq. 8) with stabilized `(pred + 1)` denominator

## Results (800×600, 16 spp, 1500 train iters, 1 CPU core)

| Scene | Avg RGB | Render time | Mrays/s | Gaussians alive | Cache keep rate |
|---|---|---|---|---|---|
| cornell | 98 / 89 / 75 | ~36 s | ~1.56 | ~3050 | ~0.1% |
| bedroom | 126 / 96 / 74 | ~41 s | ~1.36 | ~3050 | ~0.1% |
| dining | 207 / 185 / 161 | ~38 s | ~1.37 | ~2980 | ~0.0% |
| staircase | 134 / 134 / 150 | ~34 s | ~1.58 | ~2960 | ~0.0% |

The Morton grid index (16³ cells, 27-cell neighborhood) culls >99% of kernels
per query — matching the paper's ~76-of-22K evaluation ratio regime.

![Cornell box](screenshots/cornell.png)
![Bedroom](screenshots/bedroom.png)
![Dining room](screenshots/dining.png)
![Staircase](screenshots/staircase.png)

## License

MIT
