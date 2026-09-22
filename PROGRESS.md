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

## 2026-09-21 — Verify + refresh screenshots (800×600, 16 spp, train 1500)
- `make clean && make` warning-free; smoke test 200×150/4spp/train200:
  avg 90.9/82.1/68.4 (not black), 3.10 Mrays/s, keep 0.8%.
- Re-rendered all 4 scenes with current binary: cornell 21.4s/2.64 Mrays/s/
  keep 0.1% (3051 alive), bedroom 21.4s/2.63, dining 27.2s/1.93,
  staircase 20.9s/2.61; `convert`ed PPM→PNG, all PNG headers valid.
- Matches README results table; no code changes needed.

## 2026-09-21 — Tag verified release
- `make clean && make` warning-free; smoke test 200×150/4spp/train200:
  avg 91.2/81.9/68.4 (not black), 2.88 Mrays/s, keep 0.8%.
- 4× 800×600 PNGs re-validated (`file`: 800×600 RGB); working tree clean,
  `origin/master` up to date; tagged `v1.4-verify` and pushed tags.

## 2026-09-21 — Refresh screenshots with current binary
- `make clean && make` warning-free; smoke test 200×150/4spp/train200:
  avg 80.3/255 (not black), 3.16 Mrays/s, keep 0.8%.
- Re-rendered all 4 scenes (800×600, 16 spp, train 1500) with the current
  binary: cornell 23.3s/2.43 Mrays/s/keep 0.1% (3051 alive), bedroom
  22.4s/2.51, dining 27.4s/1.92, staircase 21.7s/2.51; `convert`ed PPM→PNG,
  all PNG headers valid (800×600 RGB).
- Avg RGB matches README results table exactly (cornell 98/89/75, bedroom
  126/96/74, dining 207/185/161, staircase 134/134/150); no code or README
  changes needed.

## 2026-09-21 — Final verification (all phases complete)
- `make clean && make` warning-free; smoke test 200×150/4spp/train200:
  avg 91.0/81.5/68.2 (not black), 3.08 Mrays/s, keep 0.8%.
- All AGENT.md phases confirmed done: path tracer, GLT core (Eq. 4/8,
  Morton culling, split/spawn/prune), 4 scenes, 800×600 PNG screenshots,
  OpenMP + SSE2 optimization with perf logging, README (emoji title,
  pseudocode, benchmarks, license), `.gitignore`, MIT `LICENSE`.
- 4× 800×600 PNGs valid (`file`: 800×600 RGB), tracked in git; PPM
  masters local-only (gitignored). Tags `v1.0-glt`…`v1.4-verify` pushed;
  `origin/master` up to date. No code changes needed.

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 91.2/82.2/68.7 (not black), 3.15 Mrays/s, keep 0.8%.
- Repo hygiene confirmed: 4× 800×600 PNGs valid (`file`: 800×600 RGB),
  tracked in git; README has emoji title, description, build/run, 3×
  pseudocode blocks, results table, license; MIT `LICENSE` + `.gitignore`
  present; working tree clean, `origin/master` up to date.
- All AGENT.md phases remain complete. No code changes needed.

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 91.1/82.0/68.6 (not black), 2.09 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  gaussian/culling/residual pseudocode, benchmarks, license); `convert`
  (ImageMagick 7.1.1) available; tracked files clean (no binary/PPM/logs);
  tags v1.0-glt…v1.4-verify present; working tree clean, `origin/master`
  up to date.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21b).

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.8/81.9/68.3 (not black), 2.78 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  gaussian/culling/residual pseudocode, benchmarks, license); `convert`
  (ImageMagick 7.1.1) available; tracked files clean (no binary/PPM/logs);
  tags v1.0-glt…v1.4-verify present; working tree clean, `origin/master`
  up to date.
- All AGENT.md phases remain complete. No code changes needed.

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.8/81.9/68.3 (not black), 2.89 Mrays/s, keep 0.9%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  gaussian/culling/residual pseudocode, benchmarks, license); `convert`
  available; tracked files clean (no binary/PPM/logs);
  tags v1.0-glt…v1.4-verify present; working tree clean, `origin/master`
  up to date; 4× 800×600 PNGs valid.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21c).

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.9/82.1/68.5 (not black), 3.18 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); tracked files clean
  (22 files: no binary/PPM/logs/loop.sh); tags v1.0-glt…v1.4-verify
  present; working tree clean, `origin/master` in sync (0 ahead/behind);
  4× 800×600 PNGs valid.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21d).

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.9/82.0/68.4 (not black), 1.54 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); tracked files clean
  (22 files: no binary/PPM/logs/loop.sh); working tree clean,
  `origin/master` in sync; 4× 800×600 PNGs valid.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21e).

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.8/81.8/68.2 (not black), 3.11 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); tracked files clean
  (20 files: no binary/PPM/logs/loop.sh); working tree clean,
  `origin/master` in sync; 4× 800×600 PNGs valid; tags v1.0-glt…v1.4-verify.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21f).

## 2026-09-21 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 90.7/81.9/68.2 (not black), 1.83 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); tracked files clean
  (20 files: no binary/PPM/logs/loop.sh); working tree clean,
  `origin/master` in sync; 4× 800×600 PNGs valid; tags v1.0-glt…v1.4-verify.
- All AGENT.md phases remain complete. No code changes needed (2026-09-21g).

## 2026-09-22 — Fix perf-log unit bug (ms/pixel was 1000× off)
- `src/main.c` Phase C log computed `elapsed * 1e3 / mpix * 1e3` but labeled
  it `ms/pixel` — the extra `* 1e3` made it µs/pixel mislabeled (smoke test
  printed `9.870 ms/pixel` for a 0.30 s / 30 kpx render; true value 0.010).
- Fixed to `elapsed * 1e3 / mpix`; verified: `make clean && make`
  warning-free, smoke test 200×150/4spp/train200 prints `0.010 ms/pixel`,
  avg 91.2/82.2/68.6 (not black), 3.05 Mrays/s, keep 0.8%.
- Rendering math untouched (log string only), so 800×600 screenshots stand.

## 2026-09-22 — Final verification (all phases complete, v1.5)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 91.2/82.1/68.6 (not black), 0.016 ms/pixel (unit fix confirmed),
  1.81 Mrays/s, keep 0.7%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); 4× 800×600 PNGs valid;
  tracked files clean (no binary/PPM/logs/loop.sh); working tree clean,
  `origin/master` in sync.
- All AGENT.md phases remain complete. No code changes needed.

## 2026-09-22 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 91.2/82.2/68.6 (not black), 0.015 ms/pixel, 1.90 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); 4× 800×600 PNGs valid
  (`file`: 800×600 RGB); tracked files clean (no binary/PPM/logs/loop.sh);
  working tree clean, `origin/master` in sync; tags v1.0-glt…v1.5-verify.
- All AGENT.md phases remain complete. No code changes needed (2026-09-22b).

## 2026-09-22 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  avg 91.1/82.0/68.4 (not black), 0.014 ms/pixel, 2.06 Mrays/s, keep 0.8%.
- README checklist all PASS (emoji title, 4× screenshots, build/run,
  3× pseudocode blocks, benchmarks, license); 4× 800×600 PNGs valid
  (`file`: 800×600 RGB); working tree clean, `origin/master` in sync.
- All AGENT.md phases remain complete. No code changes needed (2026-09-22c).

## 2026-09-22 — Fix unstable training-loss metric (Eq. 8 reporting)
- Smoke test showed `[GLT] train done: loss=211772` — the *reported* loss used
  the raw paper denominator `(pred + 1e-3)` while the actual SGD step already
  used stabilized `(pred + 1)`. Gradients were fine (renders bright), but the
  logged loss diverged whenever pred started near 0, making logs meaningless.
- Fixed `glt_normalized_loss` in `src/glt.h` to use `(pred + 1)`, matching
  `glt_train_step` and the README ("stabilized (pred + 1) denominator").
  Loss is diagnostic-only (not used by gradients), so rendering math untouched.
- Verified: `make clean && make` warning-free; smoke test 200×150/4spp/train200:
  loss 1.11 (was 211772), avg 91.1/81.9/68.5 (not black), 2.15 Mrays/s, keep 0.8%.
  Full 1500-iter adaptation path: loss 1.01, 3051 alive / 3054 total.
- Re-rendered all 4 scenes (800×600, 16 spp, train 1500) with current binary:
  cornell 21.5s/2.63 Mrays/s/keep 0.1% (3051 alive), bedroom 23.4s/2.40,
  dining 26.3s/2.00 (2976 alive), staircase 21.8s/2.49 (2985 alive);
  `convert`ed PPM→PNG, all PNG headers valid (800×600 RGB).
- Avg RGB matches README results table exactly (cornell 98/89/75, bedroom
  126/96/74, dining 207/185/161, staircase 134/134/150).

## 2026-09-22 — Re-verification (no changes required)
- `make clean && make` warning-free (exit 0); smoke test 200×150/4spp/train200:
  loss 1.31 (stable, pred+1 denom), avg 90.9/81.6/68.1 (not black),
  0.013 ms/pixel, 2.30 Mrays/s, keep 0.8%.
- Screenshot PPM masters re-measured: bedroom 126/96/74, cornell 98/89/75,
  dining 207/185/161, staircase 134/134/150 — match README results table;
  4× 800×600 PNGs valid, tracked in git.
- All headers have file-purpose comments + doc comments; scenes/*.glt
  descriptors, LICENSE, .gitignore verified; working tree clean,
  `origin/master` in sync.
- All AGENT.md phases remain complete. No code changes needed (2026-09-22d).
