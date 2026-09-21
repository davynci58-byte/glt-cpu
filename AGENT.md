# Agent: Gaussian Light Transport CPU Ray Tracer

You are an independent agent working on `/root/glt-cpu`. Your mission: implement a super-fast CPU ray tracer that uses Gaussian Light Transport (SIGGRAPH Asia 2026) for real-time global illumination.

## The Paper

Read and understand the paper at https://arxiv.org/abs/2609.11430. Key concepts:

- **13D Gaussian mixture model** over positions (3), directions (3), normals (3), albedo (3), roughness (1)
- **Rendering equation residual minimization**: θ* = argmin ||L_θ - E - T·L_θ||²
- **Tile-based Morton code culling** for fast evaluation (only evaluate ~76 of 22K Gaussians per pixel)
- **Separable covariance**: product of lower-dimensional Gaussians (position, direction, normal, albedo, roughness as separate subspaces)
- **Splitting** (every 400 iter), **spawning** (500 new every 500 iter), **pruning** (every 2000 iter)
- **Normalized loss**: ||r_θ / (L_θ + ε)||² to balance bright/dark regions

## What to Build

A complete CPU ray tracer in C that:

1. **Path tracer** with direct + indirect illumination
2. **Gaussian light cache** that stores the solution to the rendering equation as Gaussian kernels
3. **Tile-based culling** using Morton codes for fast Gaussian evaluation
4. **Multiple scenes**: Cornell box, bedroom, dining room, staircase
5. **Screenshot capture** to PPM (convert to PNG with `convert` if available)
6. **Performance logging** (ms/pixel, Gaussians alive)

## Architecture

Current files:
- `src/vec.h` — 3D vector math
- `src/ray.h` — ray-primitive intersection (spheres, planes)
- `src/camera.h` — pinhole camera
- `src/image.h` — PPM output
- `src/glt.h` — Gaussian model (partially implemented)
- `src/main.c` — rendering loop with path tracer

## Implementation Steps

### Phase 1: Fix and Optimize the Path Tracer
1. Fix any compilation issues
2. Optimize the path tracer (BVH if you add meshes, better sampling)
3. Add more scene types (Cornell box with different objects)
4. Ensure clean PPM output

### Phase 2: Implement Gaussian Light Transport
1. Implement the Gaussian evaluation from the paper (Equation 4)
2. Add tile-based Morton code culling (Section 3.1)
3. Implement the residual minimization loop
4. Add splitting, spawning, and pruning
5. Implement the normalized loss (Equation 8)

### Phase 3: Scenes and Screenshots
1. Create at least 3 different scenes
2. Render each at 800x600 minimum
3. Save screenshots to `screenshots/` directory
4. Measure and log performance

### Phase 4: Optimization
1. Profile and optimize hot loops
2. Add SIMD (SSE2/AVX2) for Gaussian evaluation
3. Add OpenMP parallelism
4. Optimize memory layout for cache efficiency
5. Target: <10ms per frame at 800x600

### Phase 5: Polish and Push
1. Update README with build instructions, screenshots, and results
2. Create a proper `.gitignore`
3. Commit and push to `git@github.com:davynci58-byte/glt-cpu.git` (add remote if needed)
4. Tag the release

## Rules

- Work alone. Do not wait for instructions.
- Commit after each major milestone.
- Push to GitHub when you have screenshots and a working product.
- Log your progress in `PROGRESS.md`.
- If you get stuck, try a different approach. Don't give up.
- Speed is everything. Profile, optimize, iterate.

## Git

The repo already exists at `https://github.com/davynci58-byte/glt-cpu`. Remote `origin` is set up.

```bash
cd /root/glt-cpu
git add -A
git commit -m "description of changes"
git push origin master
```

Push after every significant milestone. The repo is PUBLIC — present it like a real open-source project.

## Presentation Rules

This is a public GitHub project. It must look professional:

1. **README.md** must have:
   - Project title with emoji
   - One-paragraph description of what it does
   - Screenshot(s) embedded with `![name](screenshots/file.png)`
   - Build instructions
   - How to run
   - Key techniques explained (with pseudocode blocks)
   - Performance benchmarks
   - License

2. **Code must be clean**:
   - Consistent formatting (4-space indent)
   - Descriptive variable/function names
   - Header comments explaining each file's purpose
   - Function-level doc comments

3. **Add pseudocode** in README for key algorithms:
   - Gaussian evaluation
   - Tile-based culling
   - Residual minimization loop

4. **Screenshots** in `screenshots/` — at least 3 different scenes

## Output

When done, you should have:
- A working `glt` binary that renders scenes
- At least 3 screenshots in `screenshots/`
- A polished README.md with embedded screenshots
- Clean, well-documented code
- Everything pushed to the public GitHub repo
- The repo URL is https://github.com/davynci58-byte/glt-cpu
