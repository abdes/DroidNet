# EX07D — Baseline register for EX07E and EX07F

Recorded 2026-09-24. This preserves the initial D comparison baseline.
Current accepted measurements are in the [E comparison report](EX07E-shadow-sharing-results.md);
[F acceptance](EX07F-acceptance-report.md) awaits only user-owned editor sign-off.
Renderer implementation: **137b681b2**. Collection tooling: **250917c9e**, with
the desktop-load refinement in **d90e15823**. All runs use existing Ninja Release
trees; no other build tree was used or created for this closeout.

**EX07D: closed (2026-09-24).** Implementation and correctness repairs are
committed in `137b681b2`; the durable baseline register and evidence are committed
in `894a25e57`. The delivered scene/presets, 54 benchmark records, four application
runs and credited historical controls satisfy D's baseline-delivery gate.
The recorded preflight gaps, noisy B05-D and application memory scope constrain
later comparisons; they do not require another blanket baseline campaign.
EX07E subsequently closed with accepted performance, memory and visual evidence.
F credits those results and the qualified build repairs; only the user-owned
editor interaction sign-off keeps overall EX07 open. The original D numbers and
collection limits below remain unchanged.

## Results summary

**Initial D application performance without Tracy was 36.48 FPS for Instancing
and 16.07 FPS for New Sponza.** The accepted final E values are **68.46 FPS and
33.95 FPS**, respectively; see the linked E/F reports for matched comparisons.
The initial D synthetic
1,024-light benchmark measures 10.043 ms deferred and 7.518 ms forward; it is an
offscreen workload, not a prediction of either demo's FPS.

| Workload                     | Rendering / collection      | Mean frame ms | p95 ms | Application FPS |
| ---------------------------- | --------------------------- | ------------: | -----: | --------------: |
| 1,024-light benchmark, 1080p | Deferred, non-Tracy Release |        10.043 | 11.378 |               — |
| 1,024-light benchmark, 1080p | Forward, non-Tracy Release  |         7.518 |  9.176 |               — |
| 4,096-light benchmark, 1080p | Deferred, non-Tracy Release |        22.077 | 25.032 |               — |
| 4,096-light benchmark, 1080p | Forward, non-Tracy Release  |        20.341 | 22.921 |               — |
| Instancing, 2560x1400        | Deferred, non-Tracy Release |        27.416 | 29.000 |           36.48 |
| Instancing, 2560x1400        | Deferred, Tracy Release     |        36.319 | 37.843 |           27.53 |
| New Sponza, 2560x1400        | Deferred, non-Tracy Release |        62.229 | 64.000 |           16.07 |
| New Sponza, 2560x1400        | Deferred, Tracy Release     |        63.492 | 64.284 |           15.75 |

The build trees are **`out/build-ninja` / Release / Tracy OFF** and
**`out/build-tracy-ninja` / Release / Tracy ON**. Native application intervals use
1 ms log timestamps; Tracy application intervals use GPU frame boundaries.
Compare like collection modes; do not turn their difference into a renderer
speedup claim.

**Measured bottlenecks for E:** Sponza's traced deferred lighting costs
46.309 ms, including 45.288 ms across 23 point-light draws; translucency costs
7.726 ms. Instancing's traced deferred lighting costs 32.413 ms, including
26.433 ms across 39 visible point-light draws. The 4,096-light benchmark's grid
costs about 11 ms in either family. These identify where to investigate; they
do not isolate filtering, fetch or BRDF costs inside a draw.

**Capture coverage and quality:** 54 timed benchmark rows cover all 27 presets
in both families. All 74 benchmark images match their complete-list references
exactly, and every measured steady window records zero new buffers/textures.
Both application scenes have traced and untraced captures, frozen settings and
screenshots. CPU/GPU preflight is recorded for 24 benchmark rows and all four
application runs; 30 earlier timing records lack CPU preflight and remain
explicit supporting references. B05-D is noisy and needs a targeted repeat before
a small timing claim. No frames were removed to hide these limitations.

**Historical results remain credited:** accepted older Instancing was
43.84 FPS; older New Sponza was 42.82 FPS with 10 m point-light ranges. Current
Sponza has 4,096 m ranges and a separately frozen view, so those numbers are not
a matched renderer regression comparison. Accepted model-2 MultiView remains
7.804 ms / 128.14 FPS. The detailed sections below record their scope alongside
the new references.

These results, the complete timing tables, memory operating points and
recommendations are recorded **in this Markdown document**. Versioned JSON,
Tracy files, screenshots and byte-preserving replay inputs provide supporting
evidence under `design/vortex/plan/baselines/ex07d-20260924`; deleting the transient
analysis directory does not delete this summary or those committed artifacts.

Hardware: AMD Ryzen 9 9950X (16 cores / 32 logical processors), NVIDIA RTX 3080,
driver 610.62. Windows reports 134,904,303,616 bytes physical memory. Per-run GPU
clock, temperature, utilization and memory samples are retained; the static CPU
inventory is [versioned](baselines/ex07d-20260924/hardware-cpu.json).

## What is captured and where it belongs

| Register                    | Recorded evidence                                                                                                                                             | Role in E/F                                                                     |
| --------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| B01–B27, D/F                | **54 timed, image-qualified benchmark rows**, covering every preset in both families; native CPU/GPU percentiles, memory, churn, float images and provenance. | Controlled recipe comparisons and scaling/quality controls.                     |
| A-INSTANCING-TRACY / NATIVE | Two current-code application runs of the 1,000-mesh Instancing scene, in a frozen saved-camera view.                                                          | Conventional local-shadow and real application submission/shading control.      |
| A-SPONZA-TRACY / NATIVE     | Two current-code runs of New Sponza (the large/uber Sponza asset), using its current 4,096 m local-light ranges.                                              | Real-material, broad light-overlap, shadowed and translucent scene control.     |
| H-*                         | Accepted MultiView/model-2 and earlier Instancing/Sponza results, plus earlier benchmark/diagnostic comparisons.                                              | Credited historical evidence, with explicit content/instrumentation boundaries. |

The [versioned register](baselines/ex07d-20260924/register.json) indexes the
[full benchmark records](baselines/ex07d-20260924/benchmark-baselines.json),
four application records and [historical controls](baselines/ex07d-20260924/historical-controls.json).
These contain actual measurements and hashes, not just pointers to another report.
Application Tracy captures, reference screenshots and settings are also versioned
in that directory. Larger native CSV/GPU JSON/RGBA32F artifacts remain in
`out/analysis/ex07d/closure-137b681b2`, with exact paths, sizes and SHA256 recorded.

[Replay inputs](baselines/ex07d-20260924/replay-inputs.json) preserve the original
settings/layout bytes as base64 with hashes. These are authoritative for exact
restoration; the adjacent readable JSON/INI copies may receive repository formatting.

## Build and measurement separation

| Runs       | Existing build tree / configuration | Tracy | Meaning of frame interval                                                                                                              |
| ---------- | ----------------------------------- | ----- | -------------------------------------------------------------------------------------------------------------------------------------- |
| All B rows | `out/build-ninja`, Release          | OFF   | Native offscreen renderer wall interval; native GPU stage timestamps and bounded CPU scopes enabled. Not presented FPS.                |
| A-*-TRACY  | `out/build-tracy-ninja`, Release    | ON    | Complete GPU frame intervals between successive depth-prepass starts; detailed diagnostic tracing included.                            |
| A-*-NATIVE | `out/build-ninja`, Release          | OFF   | Actual application CPU scene-completion log intervals, 1 ms timestamp resolution, informational logging enabled. Not GPU stage timing. |

Do not mix these modes into a speedup claim. Full per-light Tracy collection
has material overhead. Release alone does not disable Tracy. Compare a candidate
against the same build mode, collection method, recipe, content, camera, quality
and resolution. Nested GPU stages and CPU scopes must not be summed as independent costs.

### Background load and confidence

Ordinary Windows desktop activity is allowed; the machine is not required to be idle.
New timing uses five pre-launch samples: CPU mean <=25%, NVIDIA GPU mean <=50%,
and fewer than two samples above CPU 50% or GPU 70%. Heavy contention delays
launch; missing counters reject timing. Early successful checks used a stricter
single-spike rule and also meet the final policy. Raw observations and thresholds
are retained, including normal Codex/desktop GPU use. No competing build/test/GPU
capture was launched by this task during measurement. The check cannot guarantee
that another application stays inactive for the whole run.

**Coverage limitation:** the first complete 54-row capture preceded the request
to record CPU headroom. **24 rows were repeated with preflight checks**. At the
user's request to release the machine sooner, the blanket repeat stopped and
application capture took priority. The remaining **30 timings retain their
measured values but have no recorded CPU preflight**. They are supporting
references, not silently relabeled load-controlled measurements. The tables mark
this distinction. All four application runs have accepted preflight records.

A small performance claim against an unchecked/noisy row requires a targeted
matched baseline/candidate check when that row is used. This explicit limitation
does not invalidate its image qualification, but limits timing conclusions.
Within-capture block variation is recorded for every row; it is not a confidence
interval across independent runs. Do not call differences within observed noise wins.

**Specific noisy record:** B05-D (33 sparse lights) has a 74.7% range between
60-frame block means. Its slow frames are retained, but it is not a reliable
timing gate for a small regression or a supposed 32/33-light performance cliff.
The earlier same-binary capture is retained alongside it in the JSON. Use a
targeted matched repeat when E evaluates this boundary; no extra run was made
after returning the machine to the user.

## Benchmark recipe and sampling

The [frozen recipe](baselines/ex07d-20260924/LightingWorkloads.json) is revision 2.
The primary is 1,024 real scene lights (512 point / 512 spot), gray XY receiver,
100 lm, 3 m support, source radius zero, 60-degree camera at (0,0,24), manual EV0
and 1920x1080 output. It guarantees 576 visible unshadowed contributors.
Shadow owners are central and stable during motion; raised occluders produce
actual cast shadows. All variants use the same scene builder and production renderer.

Static timing warms 120 frames plus 12 following untimed readbacks, then measures
240 frames; the 1,024 primary measures 480. Moving rows warm one 240-frame cycle
plus 12 and measure two complete cycles (480 frames). Motion is deterministic
per frame. Images sample phases 0/60/120/180, and both views where applicable.
Readbacks and memory inventories are outside timing. Every measured frame,
including slow frames, is retained. CPU/GPU sequence coverage is validated.

Each row compares with the complete-list capacity fallback before timing and
checks its timed image afterward. Budget: 0.5% relative + 2e-5 absolute, finite
radiance, expected size and light publication, P=1, grid completion and requested
shadow maps without quality omission. The fallback still performs spatial
empty-cell rejection; B/C independent physical and boundary references remain
the separate correctness authority. It is not an independent lighting oracle.

### Recorded frame distributions

All entries below are **mean / p50 / p95 / p99 / maximum in milliseconds**.
D = deferred, F = forward. Load column is the number of these two family rows
with recorded CPU/GPU preflight. All 54 rows are image-qualified and timed.

| IDs     | Preset            | D frame ms                                 | F frame ms                                 | Load checks |
| ------- | ----------------- | ------------------------------------------ | ------------------------------------------ | ----------: |
| B01 D/F | sparse-0          | 1.706 / 1.387 / 2.719 / 2.937 / 3.033      | 1.598 / 1.333 / 2.553 / 2.953 / 3.153      |         2/2 |
| B02 D/F | sparse-1          | 1.935 / 1.695 / 2.958 / 3.210 / 3.397      | 1.904 / 1.650 / 2.900 / 3.318 / 3.549      |         2/2 |
| B03 D/F | sparse-31         | 2.374 / 2.153 / 3.587 / 3.919 / 4.230      | 2.079 / 1.951 / 3.122 / 3.475 / 3.546      |         2/2 |
| B04 D/F | sparse-32         | 2.451 / 2.236 / 3.628 / 4.127 / 4.614      | 2.149 / 2.030 / 3.059 / 3.718 / 4.171      |         2/2 |
| B05 D/F | sparse-33         | 5.014 / 4.217 / 9.255 / 13.272 / 18.933    | 2.015 / 1.882 / 2.970 / 3.283 / 3.679      |         2/2 |
| B06 D/F | sparse-64         | 2.664 / 2.376 / 3.814 / 4.141 / 4.341      | 2.299 / 2.192 / 3.145 / 3.435 / 3.576      |         2/2 |
| B07 D/F | sparse-256        | 4.974 / 4.859 / 6.020 / 6.321 / 6.447      | 3.786 / 3.625 / 4.892 / 5.455 / 5.559      |         2/2 |
| B08 D/F | sparse-1024       | 10.043 / 10.235 / 11.378 / 12.127 / 12.448 | 7.518 / 7.535 / 9.176 / 10.266 / 11.627    |         2/2 |
| B09 D/F | sparse-4096       | 22.077 / 21.985 / 25.032 / 25.680 / 29.648 | 20.341 / 20.524 / 22.921 / 24.239 / 25.441 |         2/2 |
| B10 D/F | dense-33          | 2.451 / 2.232 / 3.543 / 3.696 / 4.204      | 2.200 / 2.074 / 3.139 / 3.396 / 4.099      |         2/2 |
| B11 D/F | dense-64          | 2.583 / 2.193 / 3.818 / 4.010 / 4.263      | 2.391 / 2.255 / 3.258 / 3.521 / 3.664      |         2/2 |
| B12 D/F | dense-256         | 4.820 / 4.792 / 5.996 / 6.268 / 6.444      | 4.584 / 4.550 / 5.456 / 5.883 / 6.731      |         2/2 |
| B13 D/F | irrelevant-1024   | 5.793 / 5.954 / 7.646 / 7.906 / 8.589      | 5.412 / 5.336 / 7.324 / 7.548 / 7.849      |         0/2 |
| B14 D/F | irrelevant-4096   | 19.248 / 19.270 / 22.443 / 23.796 / 24.063 | 17.700 / 17.681 / 21.144 / 22.325 / 23.145 |         0/2 |
| B15 D/F | moving-1024       | 10.295 / 10.306 / 12.307 / 14.249 / 15.864 | 7.466 / 7.417 / 8.868 / 9.321 / 9.751      |         0/2 |
| B16 D/F | moving-4096       | 21.583 / 20.990 / 25.328 / 26.851 / 29.049 | 20.206 / 20.335 / 22.659 / 23.654 / 24.758 |         0/2 |
| B17 D/F | 4k-1024           | 20.052 / 19.974 / 21.712 / 22.548 / 24.688 | 15.415 / 15.231 / 17.421 / 17.944 / 19.504 |         0/2 |
| B18 D/F | 4k-4096           | 33.587 / 33.706 / 36.429 / 36.886 / 37.672 | 28.343 / 28.539 / 31.095 / 32.202 / 32.676 |         0/2 |
| B19 D/F | 4k-dense-256      | 10.283 / 10.210 / 11.427 / 11.769 / 12.507 | 10.511 / 10.431 / 11.637 / 12.103 / 12.450 |         0/2 |
| B20 D/F | two-view-1024     | 16.503 / 16.087 / 19.970 / 21.982 / 22.671 | 11.691 / 11.595 / 14.346 / 15.610 / 15.904 |         0/2 |
| B21 D/F | orthographic-1024 | 10.114 / 10.195 / 11.925 / 12.967 / 13.072 | 7.162 / 7.166 / 8.520 / 8.921 / 9.453      |         0/2 |
| B22 D/F | shadows-1-1       | 10.622 / 10.560 / 12.665 / 13.791 / 14.011 | 7.435 / 7.519 / 8.770 / 9.322 / 10.134     |         0/2 |
| B23 D/F | shadows-4-8       | 10.341 / 10.371 / 12.344 / 13.781 / 14.867 | 7.623 / 7.536 / 9.242 / 10.381 / 11.242    |         0/2 |
| B24 D/F | shadows-5-9       | 10.375 / 10.285 / 12.525 / 13.214 / 14.939 | 7.760 / 7.791 / 9.091 / 9.362 / 9.851      |         0/2 |
| B25 D/F | shadows-moving    | 11.462 / 11.417 / 13.625 / 14.655 / 16.031 | 8.603 / 8.714 / 10.345 / 11.003 / 12.659   |         0/2 |
| B26 D/F | shadows-finite    | 10.560 / 10.542 / 12.977 / 13.705 / 15.029 | 7.684 / 7.782 / 9.028 / 9.388 / 9.902      |         0/2 |
| B27 D/F | shadows-wide      | 11.980 / 11.935 / 13.632 / 14.312 / 15.287 | 8.197 / 8.310 / 9.570 / 10.266 / 10.954    |         0/2 |

### Recorded CPU/GPU and memory operating points

GPU columns are means: spatial-grid / lighting stage (deferred) or base pass
(forward). CPU is the union of lighting scopes in elapsed milliseconds, including
waits where present, not scheduler-derived active CPU. Memory is lighting-domain
allocated MiB; it is a subset of the whole allocator. Full stage percentiles,
local/non-local allocation, block/slack/usage counters, staging bytes per frame
and creation counts are in the versioned JSON for each ID.

Allocated bytes include retained resources. This capture does not expose separate
queued/retired/cached byte histograms; do not infer those categories from a total
allocation counter. Resource admission ceilings are not performance targets.

| IDs | D GPU grid / lighting ms    | F GPU grid / base ms | CPU D / F ms  | Lighting MiB D / F | New buffers/textures D ; F |
| --- | --------------------------- | -------------------- | ------------- | ------------------ | -------------------------- |
| B01 | not executed / not executed | not executed / 0.294 | 0.011 / 0.011 | 0.195 / 0.195      | 0/0 ; 0/0                  |
| B02 | 0.097 / 0.031               | 0.097 / 0.395        | 0.137 / 0.131 | 2.008 / 1.883      | 0/0 ; 0/0                  |
| B03 | 0.145 / 0.205               | 0.147 / 0.516        | 0.273 / 0.153 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B04 | 0.146 / 0.200               | 0.150 / 0.514        | 0.291 / 0.169 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B05 | 0.155 / 0.203               | 0.146 / 0.518        | 0.876 / 0.146 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B06 | 0.195 / 0.342               | 0.203 / 0.624        | 0.379 / 0.176 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B07 | 0.724 / 1.295               | 0.795 / 1.205        | 1.136 / 0.271 | 2.695 / 2.445      | 0/0 ; 0/0                  |
| B08 | 3.200 / 3.002               | 2.721 / 2.071        | 2.795 / 0.642 | 3.258 / 2.633      | 0/0 ; 0/0                  |
| B09 | 10.959 / 3.815              | 10.800 / 2.258       | 4.596 / 2.280 | 4.133 / 3.445      | 0/0 ; 0/0                  |
| B10 | 0.150 / 0.211               | 0.150 / 0.560        | 0.287 / 0.170 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B11 | 0.197 / 0.329               | 0.202 / 0.740        | 0.360 / 0.176 | 2.570 / 2.445      | 0/0 ; 0/0                  |
| B12 | 0.674 / 1.166               | 0.813 / 1.872        | 1.065 / 0.308 | 2.695 / 2.445      | 0/0 ; 0/0                  |
| B13 | 2.803 / 0.383               | 2.671 / 0.635        | 0.793 / 0.559 | 2.758 / 2.633      | 0/0 ; 0/0                  |
| B14 | 11.433 / 0.398              | 11.556 / 0.624       | 2.941 / 2.245 | 3.883 / 3.445      | 0/0 ; 0/0                  |
| B15 | 3.214 / 3.013               | 2.746 / 2.084        | 2.867 / 0.561 | 3.258 / 2.633      | 0/0 ; 0/0                  |
| B16 | 10.866 / 3.855              | 11.101 / 2.280       | 4.592 / 2.029 | 4.133 / 3.445      | 0/0 ; 0/0                  |
| B17 | 3.197 / 9.489               | 3.045 / 7.007        | 2.768 / 0.746 | 9.445 / 8.820      | 0/0 ; 0/0                  |
| B18 | 11.949 / 10.506             | 11.925 / 7.408       | 4.713 / 2.321 | 10.320 / 9.633     | 0/0 ; 0/0                  |
| B19 | 1.000 / 3.896               | 0.971 / 5.574        | 1.242 / 0.350 | 8.883 / 8.633      | 0/0 ; 0/0                  |
| B20 | 5.571 / 3.580               | 5.308 / 2.589        | 4.913 / 0.898 | 5.008 / 4.070      | 0/0 ; 0/0                  |
| B21 | 3.171 / 2.881               | 2.829 / 1.751        | 2.972 / 0.587 | 3.258 / 2.633      | 0/0 ; 0/0                  |
| B22 | 3.068 / 2.986               | 2.703 / 2.094        | 3.330 / 0.682 | 43.258 / 42.633    | 0/0 ; 0/0                  |
| B23 | 2.931 / 3.025               | 2.590 / 2.110        | 3.349 / 1.007 | 43.258 / 42.633    | 0/0 ; 0/0                  |
| B24 | 2.928 / 3.100               | 2.707 / 2.129        | 3.413 / 1.049 | 43.258 / 42.633    | 0/0 ; 0/0                  |
| B25 | 2.776 / 2.972               | 2.844 / 2.159        | 3.830 / 1.422 | 43.258 / 42.633    | 0/0 ; 0/0                  |
| B26 | 3.038 / 3.249               | 2.735 / 2.331        | 3.234 / 0.705 | 43.258 / 42.633    | 0/0 ; 0/0                  |
| B27 | 2.564 / 4.568               | 2.898 / 2.651        | 3.289 / 0.709 | 27.258 / 26.633    | 0/0 ; 0/0                  |

The recorded requests disambiguate distribution, resolution, source radius,
projection and view count. Shadow suffixes specify requested point/spot owners;
a hemispherical spot uses cube coverage, so `shadows-wide` publishes two cube
maps and zero projected maps for its one point plus one spot. Actual map counts,
grid occupancy/fallback and visible-contributor checks are recorded per image.

Primary inspection images use the fixed x/(1+x), gamma-2.2 transform; numerical
comparisons use original floats, not these PNGs.

![Deferred primary](baselines/ex07d-20260924/primary-deferred.png)

![Forward primary](baselines/ex07d-20260924/primary-forward.png)

## Application scene baselines

Both scenes use RenderScene, conventional directional shadows, 2560x1400,
`--fps 0 --vsync false --debug-layer false`, static saved camera/settings and a
55-second collection. The measurement window is 20–50 seconds after startup,
excluding content loading and PSO warmup. The screenshot is taken afterward.
The archived startup settings and ImGui layout define the replay; `--scene`
overrides the runtime scene without replacing the persisted library selection.
Native logs and inspected screenshots confirm the runtime scene. These are
frozen current views, not claims that historical camera/settings are identical.

Instancing source: `Examples/Content/.cooked/Scenes/InstancingTestScene.oscene`,
1,000 mesh instances, one directional and 49 point lights; 49 source lights cast
shadows, authored ranges 6/7/8/10 m. Sponza source:
`Examples/RenderScene/.cooked/Scenes/NewSponza_Main_glTF_003.oscene`, 115 meshes,
one directional and 23 points, all 24 shadow-enabled; point ranges 4,096 m.
Preview-sun/environment overrides are frozen in settings. Node/asset counts
are not GPU draw counts; instancing and conservative view rejection still apply.
The current traced Instancing view records 39 point-light draws plus one
directional; Sponza records 23 point-light draws plus one directional per frame.

### Frame distributions and collection identities

| Baseline            | Frame mean / p50 / p95 / p99 / max ms      | FPS from mean | Complete intervals | Block variation |
| ------------------- | ------------------------------------------ | ------------: | -----------------: | --------------: |
| A-INSTANCING-TRACY  | 36.319 / 36.458 / 37.843 / 38.938 / 42.079 |         27.53 |                825 |           0.50% |
| A-INSTANCING-NATIVE | 27.416 / 27.000 / 29.000 / 30.000 / 31.000 |         36.48 |               1094 |           0.01% |
| A-SPONZA-TRACY      | 63.492 / 63.447 / 64.284 / 64.929 / 65.885 |         15.75 |                472 |           0.11% |
| A-SPONZA-NATIVE     | 62.229 / 62.000 / 64.000 / 66.000 / 68.000 |         16.07 |                481 |           0.27% |

Tracy rows contain GPU intervals; native rows contain CPU scene-completion
intervals quantized to 1 ms. FPS is reciprocal mean interval, not mean reciprocal
FPS. Do not use the difference between the two modes as a renderer improvement.

### Tracy stage baselines

| Stage, mean ms                  |   Instancing | New Sponza |
| ------------------------------- | -----------: | ---------: |
| Vortex.Stage3.DepthPrepass      |        0.099 |      0.859 |
| Vortex.Stage6.SpatialLightGrid  |        0.284 |      0.161 |
| Vortex.Stage8.ShadowDepths      |        0.288 |      3.470 |
| Vortex.Stage9.BasePass          |        0.377 |      2.733 |
| Vortex.Stage12.DeferredLighting |       32.413 |     46.309 |
| Vortex.Stage12.PointLight       |       26.433 |     45.288 |
| Vortex.Stage12.DirectionalLight |        0.759 |      0.822 |
| Vortex.Stage18.Translucency     | not executed |      7.726 |

Per-stage p50/p95/p99/max, scope counts per frame, CPU per-call distributions,
raw trace hashes, code/DLL/shader/scene/index/settings hashes and load observations
are recorded in [Instancing Tracy](baselines/ex07d-20260924/instancing-tracy.json)
and [Sponza Tracy](baselines/ex07d-20260924/sponza-tracy.json). Native frame records:
[Instancing](baselines/ex07d-20260924/instancing-native.json),
[Sponza](baselines/ex07d-20260924/sponza-native.json).

Application memory capture includes process working-set/private bytes and the
device-wide NVIDIA memory sample. These are not per-renderer GPU allocation
inventories. Do not claim scene allocation savings from device-wide usage; obtain
matched allocation snapshots when that is the proposed E change. The benchmark
rows supply actual renderer allocation-domain baselines. E subsequently recorded
bounded lifecycle/release memory evidence; F credits that evidence and the user's
RenderScene interaction approval. Only editor sign-off remains. No indefinite
application soak is claimed.

![Instancing frozen view](baselines/ex07d-20260924/instancing.png)

![New Sponza frozen view](baselines/ex07d-20260924/sponza.png)

These application screenshots are visual anchors with UI/FPS overlays, not
bit-exact HDR oracles. They must be inspected with the frozen camera/settings.
Raw Tracy files: [Instancing](baselines/ex07d-20260924/instancing.tracy) and
[Sponza](baselines/ex07d-20260924/sponza.tracy).

## Accepted historical controls retained

| Control                   | Recorded operating point                                                                                                                                                                                            | Comparison boundary                                                                                                                                                             |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| H-MULTIVIEW, model 2      | 2560x1440 fullscreen, 600 frames / first 64 excluded; 13.098 -> 7.804 ms mean; p95 13.757 -> 10.760; deferred total 8.427 -> 2.901 ms; BRDF native allocation 4.5 MiB -> 64 KiB.                                    | Three deferred views plus one forward offscreen view, three directionals/point/spot; accepted physical/model-2 control. Not the many-light scene or current Sponza.             |
| H-INSTANCING              | 22.810 ms / 43.84 FPS; p95 25.054; shadow depths 0.237 ms; deferred lighting 19.09 ms.                                                                                                                              | Earlier conventional-shadow static capture; source/camera/implementation differ from current application references. Credit it, do not silently substitute it for A-INSTANCING. |
| H-SPONZA-10M              | Accepted mean 23.351 ms / 42.82 FPS; p95 23.560; shadow depths 3.80 ms; deferred 9.21 ms; translucency 3.33 ms. Earlier matched pre-repair mean was 168.99 ms, shadows 152.71 ms.                                   | The accepted cooked points had 10 m ranges. Current source policy uses 4,096 m. No isolated code-speedup claim across those assets.                                             |
| H-BENCH-TRACY             | Established 1,024: deferred 59.530 ms (p95 67.179), forward 8.311 ms (p95 10.425). Optimized fully traced deferred 24.327 ms (p95 26.621). CPU lighting union 14.278 -> 4.010 ms; GPU lighting 18.125 -> 15.413 ms. | Matched earlier full-Tracy optimization comparison; before the later receiver-footprint correction. Retained history, not current B-row mode.                                   |
| H-BENCH-OPT-NATIVE        | Earlier no-Tracy primary 9.999 ms deferred / 7.230 forward; 4,096 lights 22.001 / 20.718 ms.                                                                                                                        | Earlier stage-timing operating points, before the later shadow correction. Never report 59.530 -> 9.999 as a same-instrumentation speedup.                                      |
| H-SPONZA-4096M-DIAGNOSTIC | Post-fix diagnostic full-Tracy 72.406 ms / 13.81 FPS, p95 76.834; deferred 45.568, point draws 38.609, translucency 14.260 ms. Separate native log sample 65.477 ms / 15.27 FPS.                                    | Diagnostic evidence supporting E priorities; no recorded CPU preflight. Current A-SPONZA rows are the new comparison references.                                                |

The versioned historical JSON retains source values and hashes. Original reports:
[model 2](../../../out/build-tracy-ninja/analysis/vortex/exposure-lightbench/ex07c/model2-results.md),
[conventional shadows](../../../out/analysis/light-shadow-audit-20260924/REPORT.md),
[initial qualified count set](../../../out/analysis/ex07d/qualified-20260924/accepted-summary.json).
The interrupted original 4,096-light run and pre-lifetime-fix shadow attempts
are not admitted as baselines. The 288.300 ms 4,096-light result was an intermediate
batch-retirement candidate, not a completed original reference.

## How E and F use this register

| Change under evaluation                  | Minimum targeted comparisons before wider qualification                                                                                                                                                                                       |
| ---------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Deferred shader/BRDF/bindless submission | B08 primary, B12 dense, B20 two-view, B22/B27 shadow variants; A-INSTANCING and A-SPONZA in matched modes; retained independent physical/material references.                                                                                 |
| Grid/list scaling                        | B01–B09 boundaries/counts, B13–B14 irrelevant lights, B15–B16 motion, B17–B19 4K, B20–B21 views/projection. Compare grid cost, whole frame, memory and fallback completeness.                                                                 |
| Shadow quality/cache/bias/filtering      | B22–B27, both application scenes, and independent point/spot near/far, off-screen caster, contact/grazing/cube-seam controls. Keep changed quality distinct from performance.                                                                 |
| Lifetime/resource changes                | Small/count-zero and two-view controls plus actual application close, resize/hide/recreate and scene replacement. Static snapshots do not replace queued-reader and soak tests.                                                               |
| Final F acceptance                       | Integrated relevant matrix with matched collection, real application interaction, user visual approval and explicit supported operating budgets. Reuse unaffected evidence; no automatic fresh campaign just because the stage label changes. |

The [regression analysis](EX07-NewSponza-regression-analysis.md) and
[execution plan](EX07-lighting-correctness-and-scalability.md#recorded-e-priorities-after-the-new-sponza-regression-analysis)
record open GPU shading, translucent lighting, grid-scaling and bias/filtering
work. Preserve conservative culling and off-screen shadow contributors. Do not
shorten imported ranges, drop shadows or switch renderer policy to manufacture
a better result. UE5.7 comparison informs the design; it does not establish
performance or complete filtering/bias parity.

## Replay, audit and disposition

Use the frozen requests and archived settings rather than current user defaults.
All raw paths and SHA256 identities are in the register. Use a fresh output
directory for a changed renderer/recipe. Example commands from the engine root:

```powershell
# Native benchmark matrix: existing non-Tracy Ninja Release.
python tools/vortex/RunManyLightBaseline.py --build-tree build-ninja --output out/analysis/ex07-candidate
python tools/vortex/SummarizeManyLightBaseline.py out/analysis/ex07-candidate

# Application capture, after restoring the archived per-scene settings.
# Use build-tracy-ninja/bin/Release for the Tracy run; build-ninja/bin/Release
# for native timing. Capture 55 seconds and analyze complete 20–50 s intervals.
Oxygen.Examples.RenderScene.exe --scene InstancingTestScene --resolution 2560x1400 --frames 0 --fps 0 --vsync false --directional-shadows conventional --debug-layer false -v=-1
Oxygen.Examples.RenderScene.exe --scene NewSponza_Main_glTF_003 --resolution 2560x1400 --frames 0 --fps 0 --vsync false --directional-shadows conventional --debug-layer false -v=-1
```

For native application log intervals use `-v=0`. The collector/analyzer snapshots
in the archive record the exact commands and original raw-output directory layout;
their original location is `out/analysis/ex07d/closure-137b681b2`. Settings and
ImGui state are restored after every run. `--qualified-from` may reuse images
only when executable/DLL/shader/recipe hashes match; it does not import prior timing.
The blanket repeat was curtailed to prioritize captures and return the machine;
the 30 un-repeated preflight gaps remain recorded, not erased or labeled passed.

Correctness foundation: 226 scoped tests per Debug/Release configuration preceded
the application follow-up; its 17 native and three glTF tests pass in both
configurations, with 218-module shader archives. The fixes cover fenced staging
and target lifetime, nonuniform shadow indexing, batched/reused CBVs, conservative
draw culling, spot volume classification, redundant draw state and GPU scope
storage growth, plus the receiver-depth shadow footprint. This inventory closes
the baseline-document delivery. Subsequent E performance acceptance and F
engine-side qualification are recorded in their linked reports above.
