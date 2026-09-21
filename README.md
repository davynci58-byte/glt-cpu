# Gaussian Light Transport — CPU Ray Tracer

A super-fast CPU ray tracer inspired by [Gaussian Light Transport](https://arxiv.org/abs/2609.11430) (SIGGRAPH Asia 2026).

Represents the solution to the rendering equation as a mixture of Gaussian kernels over positions, directions, normals, and material properties. Optimizes by minimizing the residual of the rendering equation. Achieves real-time global illumination on CPU through efficient tile-based culling and separable covariance evaluation.

## Build

```bash
make
```

## Run

```bash
./glt scenes/cornell.glt
```

Outputs a PPM image to `output.ppm`.

## Architecture

- `src/vec.h` — SIMD-friendly 3D vector math
- `src/ray.h` — ray-primitive intersection
- `src/scene.h` — scene definition (spheres, planes, meshes)
- `src/glt.h` — Gaussian Light Transport core (13D Gaussian mixture, culling, evaluation)
- `src/camera.h` — pinhole camera model
- `src/image.h` — PPM image output
- `src/main.c` — entry point, rendering loop

## Key Techniques (from the paper)

- 13D Gaussians: position (3), direction (3), normal (3), albedo (3), roughness (1)
- Rendering equation residual minimization: θ* = argmin ||L_θ - E - T·L_θ||²
- Tile-based Morton code culling for fast evaluation
- Separable covariance (product of lower-dimensional Gaussians)
- Splitting, spawning, and pruning for adaptive density

## License

MIT
