# Progress Log — glt-cpu

## 2026-09-20 — Phase 1: path tracer fixed
- `src/ray.h` was truncated (missing `sphere`/`plane` structs, `emission` in
  `hit_record`): rewrote it completely; fixed `main.c` variable shadowing
  (`scene s` vs loop `s`) and emissive-return bug (returned albedo, now emission).
- `make` compiles clean (only `-Wmisleading-indentation` style warnings).
- First test render exposed two lighting bugs: shadow rays self-occluded on the
  light sphere itself, and indirect throughput used `albedo/pi` instead of
  `albedo` (cosine-weighted estimator). Both fixed in the rewrite.
- Before fix: avg pixel ~1.4/255 (black). After fix: ~90/255.

## 2026-09-20 — Phase 2: Gaussian Light Transport core (`src/glt.h`)
- Separable 13D Gaussian evaluation (Eq. 4) with RGB kernels.
- Morton-code spatial hash + 16³ grid index, 27-cell neighborhood lookup
  (Sec. 3.1): measured keep rate 0.0–1.7% per query (paper: ~76/22K).
- Residual training loop (Eq. 1) with normalized loss (Eq. 8); stabilized SGD
  with `(pred + 1)` denominator and clamped updates (raw `eps` denom diverged:
  loss 16 → 789 at spawn iters; now stable at ~40).
- Adaptation schedule: split best kernel every 400 iters, spawn 500 every 500,
  prune every 2000, rebuild index every 250. 2048 seeds → ~3050 alive.

## 2026-09-20 — Phase 3: scenes + CLI
- 4 built-in scenes: `cornell`, `bedroom`, `dining`, `staircase`
  (spheres + planes, distinct lights/cameras), `scenes/*.glt` descriptors.
- CLI: `--scene/--out/--spp/--width/--height/--train` plus legacy positional
  (`./glt out.ppm 64`, `./glt scenes/cornell.glt`) support.

## 2026-09-20 — Phase 4: optimization + perf logging
- OpenMP row-parallel renderer (`-fopenmp`, per-row xorshift RNG), atomic ray
  counter; `-march=native -O3` for auto-SIMD.
- Per-run log: wall time, ms/pixel, Mrays/s, gaussians alive/total,
  cache evals kept/culled. ~1.4–1.6 Mrays/s on 1 core.

## 2026-09-20 — Phase 5: screenshots + release
- Rendered all 4 scenes at 800×600, 16 spp, 1500 train iters (~35–40 s each);
  PPM + PNG (via `convert`) in `screenshots/`.
- Updated README (build/run/results table), scene descriptors, this log.
- Committed, pushed to `origin/master`, tagged `v1.0-glt`.
