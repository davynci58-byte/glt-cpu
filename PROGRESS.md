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

## 2026-09-21 — Polish: warning-free build, docs, gitignore
- Fixed all `-Wmisleading-indentation` warnings (`make` now silent);
  split one-line `if` chains onto separate lines in `glt.h`/`main.c`.
- README: emoji title, pseudocode blocks for Gaussian eval, Morton
  culling, and residual-minimization loop (presentation-rules compliant).
- Added file-purpose headers (`vec/ray/camera/image/main`) + doc comments
  on intersect, sampling, and pathtrace functions.
- `.gitignore`: build artifacts, local test renders, `logs/` (loop noise),
  editor/OS files. Untracked `logs/glt.log`.
- Verified: `make clean && make` silent, 200×150/4spp cornell test renders
  avg ~80/255 (not black), 1.6 Mrays/s, keep rate 0.9%; 4× 800×600 PNGs valid.

## 2026-09-21 — Polish: LICENSE, README honesty, repo hygiene
- Added MIT `LICENSE` (README already claimed MIT but no file existed).
- README: results header now shows the exact `--train 1500` flag
  (code default is 2000), license links to `LICENSE`, added honest note
  that <10ms/frame at 800×600/16spp is out of reach for a CPU path
  tracer (~40–60M rays/render) — Mrays/s + keep rate are the real metrics.
- Untracked `loop.sh` agent scaffolding from the public repo
  (`git rm --cached`, added to `.gitignore`; kept on local disk).
- Verified: `make clean && make` warning-free, 200×150/4spp cornell test
  avg ~91/82/68 (not black), 2.1 Mrays/s, keep 0.4%; 4× 800×600 PNGs valid.

## 2026-09-21 — Phase 4: hot-loop optimization (SIMD + culled training)
- `glt.h`: precomputed inv-sigma per kernel (refreshed on spawn/split) —
  eliminates 7 `expf` calls per gaussian per query; single shared
  `glt_kernel_weight` helper used by eval AND training (also fixes train
  step omitting the albedo/roughness terms).
- Explicit SSE2 fast path for the 3D squared-distance factor
  (`_mm_sub/mul` + 3-lane horizontal sum, scalar fallback otherwise).
- `glt_train_step` now takes the caller's `pred` (no double eval) and
  visits only the 27-cell Morton neighborhood when the index is built
  instead of scanning all 65K slots: 1500-iter training takes <0.2 s.
- Removed dead counting pass in `glt_build_index` (single scan fill).
- Fixed stack overflow from the enlarged model (~9.5 MB vs 8 MB stack):
  `glt_model` is now `static` in `main.c`.
- Measured (800×600, 16 spp, 1500 train): 22–27 s/render, 1.9–2.5 Mrays/s
  (~1.6× vs scalar baseline), keep rate 0.0–0.1%, screenshots re-rendered
  with identical brightness (cornell 98/89/75, dining 207/185/161, …).

## 2026-09-21 — Release hygiene: untrack PPM masters
- `screenshots/*.ppm` were tracked in git (~5.6 MB duplicating the PNGs):
  `git rm --cached` + `screenshots/*.ppm` in `.gitignore`; PNGs stay
  tracked, PPMs stay on local disk for `convert` regeneration.
- Verified: `make clean && make` warning-free, 200×150/4spp cornell test
  avg ~81/255 (not black), 2.0 Mrays/s, keep 0.2%.
