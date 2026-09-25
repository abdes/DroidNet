# EX07 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Starting evidence and owners

The initial review established the areas below. Current behavior and remaining
work are summarized here; measured regression-repair results are in the tracker.

| Owner                                                                            | Implementation and remaining work                                                                                | Consequence for EX07                                                                                         |
| -------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Lighting/Internal/ForwardLightPublisher.cpp`                                    | Every cluster references the complete local-light list. CPU vectors/grid entries are rebuilt during publication. | Establish real conservative spatial lists; measure preparation, upload and shader work.                      |
| `Lighting/Internal/LightGridBuilder.cpp`                                         | Builds records and grid metadata; this service path does not dispatch GPU spatial culling.                       | A baked culling shader alone is not evidence of an operational culler.                                       |
| Shader `Services/Lighting/LightCulling.hlsl`                                     | Scans all lights for each cell and clamps output count to per-cell capacity.                                     | Review ABI/coordinates before reuse; eliminate silent light loss on overflow.                                |
| `Types/LightCullingConfig.h`                                                     | 64-pixel XY cells, 32 depth slices and nominal 32 entries per cell.                                              | Exercise 31/32/33 overlap and viewport/depth boundaries; these constants are not a proven scalable envelope. |
| `Lighting/Passes/DeferredLightPass.cpp`                                          | Produces per-local-light volume/fullscreen draws and per-draw constants.                                         | Measure CPU submission, visibility rejection, camera-inside fallback and overlapping volume cost.            |
| `Shadows/Internal/{Point,Spot}ShadowSetup.cpp` and `Types/ShadowFrameBindings.h` | Indexed dynamic families replace the former four-point/eight-spot arrays.                                        | Test capacity, mapping and overflow explicitly; never equate total light count with shadow count.            |
| `Diagnostics/ShaderDebugModeRegistry.cpp`                                        | Light-grid debug views are marked unsupported.                                                                   | Publish truthful culling statistics and a useful opt-in visualization for qualification.                     |

Paths above are under `src/Oxygen/Vortex`; shader paths are under
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Existing `LightingService`, shadow
owners, view publications, upload allocators and profiling remain authoritative.

## Measurements and improvement order

Measure CPU scene gather/selection, transform/bounds processing, grid/list
construction, allocation, upload bytes and submission separately. Distinguish
active work from waits. Measure GPU culling/list build, forward evaluation,
deferred volume/lighting, shadow depth and shadow sampling, plus total GPU and
native frame intervals. Use non-overlapping intervals; do not add nested scopes
or independent percentiles. Attribute shared passes with matched controls when
their work cannot be separated by timestamps; label estimates as estimates.

Record selected/rejected lights, cell occupancy p50/p95/max, candidate references,
overflow/required capacity, shaded light evaluations or a clearly labelled proxy,
draws/dispatches/PSO switches, uploaded bytes, live/in-flight/cached memory and
steady allocation churn. Deep shader counters are opt-in and outside timed runs.
Counters/heatmaps must describe the executed product, not nominal configuration.

The conventional-shadow sequence above governs the prerequisite delivery. The areas below
are covered by that work or assessed afterward from the remaining measured costs;
they are not a second ordered implementation list:

- Eliminate the all-lights-per-cluster publication. Start with conservative view
  and cell assignment; evaluate light-driven versus cell-driven work and compact
  lists against occupancy. Reuse the existing grid contract where suitable, but
  review the old shader's ABI and coordinate conventions before connecting it.
- Reject noncontributing deferred draws conservatively and reduce avoidable
  per-light CPU binding/constant/PSO overhead. Compare light volumes with a
  tiled/clustered deferred candidate only if profiling justifies it. Such a change
  stays within Stage 12 and requires an explicit LLD/ABI decision; do not retain
  duplicate production algorithms without a measured, documented need.
- Inspect actual light loops, repeated BRDF/conversion work, buffer layout/access,
  bandwidth, divergence, register pressure/spills and overdraw. Hoist only
  view/light/material invariants. Avoid blanket loop unrolling, forced precision
  loss, source-range reduction or removing valid contributions for speed.
- Reuse/grow resources safely, share immutable light records across views, and
  keep visibility/list products view-specific. Remove avoidable steady CPU/GPU
  allocations and duplicate uploads. Any cache must invalidate on relevant
  content or projection mutation. A camera move alone does not invalidate
  a complete light-space local-depth cache; directional cascades remain view-dependent.
- Optimize existing shadow setup/caster culling/update work when it dominates.
  Do not hide that cost inside unshadowed results, drop shadow requests or reduce
  resolution/update frequency silently. Preserve off-screen contributing casters.

Independent directional contributions and supported shader families must satisfy
their contracts after these changes. Keep implementation focused on the measured
lighting path and necessary dependencies; avoid speculative frameworks and
unrelated exposure optimization. Required correctness and bottleneck repairs
remain part of EX07 until both gates pass.

## Performance acceptance and run discipline

### Bounded shadow memory work

The [2026-09-22 stencil/ownership audit](EX07E/shadow-memory.md) selects
three focused changes under EX07-10/11. Source tracing and a native GPU format
comparison supported the initial design. D32 cutover, retained ownership,
cross-frame cache reuse and focused/static performance acceptance are complete;
broader workload qualification and cross-view sharing subsequently completed in
E. The [F report](EX07F/validation.md) records final evidence applicability.

| Work                            | Scope and implementation owner                                                      | Required result                                                                                                                                                                 |
| ------------------------------- | ----------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Depth-only conventional maps    | `ConventionalShadowTargetAllocator`, `ShadowDepthPass`, views/PSOs and owning tests | D32 depth with unchanged resolution, bias, reversed-Z and PCF; no shadow stencil clears. Scene/custom stencil, receiver/contact products and VSM remain independently owned.    |
| Compatible local-map sharing    | `ShadowService`, `CascadeShadowPass` and indexed bindings                           | One render/allocation for identical local-light shadow content within the frame; distinct per-view CSM/contact products. Different caster content or generations cannot share.  |
| Bounded allocation reuse/growth | Existing shadow allocator and frame leases/fences                                   | Correct per-light resolution buckets, no redundant complete-set duplication, only affected buckets grow, fence-safe retirement and bounded spare capacity without steady churn. |

The conventional-shadow sequence completes D32, bounded allocation ownership and
cross-frame point/spot depth-content reuse with complete light-space coverage.
Compatible local contents now share immutable per-light versions across views
through the completed EX07E allocation/content ownership implementation.
Cascades remain view-dependent. Do not schedule a
second allocator, D32 conversion or cache implementation under EX07E. Step 5
separately selects a manually verified resolution/fade policy; memory exhaustion
remains distinct from that intentional quality choice. No VSM or global aliasing
framework is introduced.

The linked audit owns the consumer inventory and targeted regression cases:
CSM motion/blends, masked casters, local face/seam coverage, forward/deferred/
translucent consumption, stencil preservation, incompatible view content and
delayed/discarded submissions. Include whole-frame CPU/GPU timing and lifecycle
spikes; evaluate memory savings against quality and whole-frame cost. Avoid
queue drains, duplicate raster work or reduced concurrency
introduced solely to lower allocation counts.

The 4 GiB / 128 MiB ceilings remain upper bounds, not normal allocation targets.
Report actual unique map bytes, allocator committed bytes/slack, pending/retired/
cached resources and whole-process DXGI usage/budget separately. Charge other
engine commitments and explicit headroom at the parent allocator before admission;
do not present the standalone allocation query as whole-engine capacity proof.

### Timing and acceptance

Use the reference RTX 3080 / Ryzen 9950X for comparable optimized Release
measurements. Record adapter/LUID, driver, clocks/power conditions, compiler and
shader identities, scene and memory configuration. Keep VSync and frame caps
disabled. Use Tracy captures for CPU/GPU attribution; report uninstrumented
throughput separately when measured. Debug/GBV, RenderDoc and deep shader counters
belong to separate diagnostic runs. Keep geometry, light values, shadow quality,
simulation time and physical output resolution identical within a comparison.

- **Operating points:** measure the 1,024-light sparse/mixed 1080p workload in
  each rendering family, plus required count, dense-overlap, 4K, multi-view and
  shadowed cases. Report p50/p95/p99/max frame and stage costs, quality differences
  and actual memory. Select the quality/time/memory compromise from measured
  alternatives; the user decides whether the resulting budget is acceptable.
- **Controlled MultiView profiling:** use fullscreen `--resolution 2560x1440`
  (the parser also accepts `X`), `--fps 0`, `--vsync false`,
  `--directional-array-proof true`, `--offscreen-proof-layout true`,
  `--pip-wireframe false` and `-v=-1`. Verify actual framebuffer dimensions and
  uncapped presentation in the trace. This regression workload complements the
  many-light matrix; it does not establish its scaling result.
- **Resources:** distinguish live, queued, retired and cached allocations,
  allocator slack and whole-process usage. Repeated lifecycle cycles should
  stabilize memory; warmed fixed-capacity resources should not allocate each
  frame. The resource admission ceilings remain separate from performance goals.
- **Bounded runs:** warm until assets, PSOs and capacities are ready, and record
  the excluded warmup and sampled frame counts. Include complete movement cycles
  for dynamic workloads. Use short targeted experiments during optimization and
  collect the final matrix on integrated code. Retain slow frames and reject
  incomplete profiler output. Repeat only when code changes or diagnosed noise
  warrant it.
- **Visual captures:** let exposure settle and record applied/target values for
  each view. The qualified model-2 MultiView capture uses frame 2000; verify
  convergence rather than treating an early fixed frame as correctly exposed.

### Comparison and acceptance

Record baseline/candidate recipe hashes, code and shader identities, correctness
results, absolute times, memory and image/numerical comparisons. Measure noise
and report changes within it as inconclusive. Include small-light, shadowed and
multi-view controls so a local optimization does not conceal a whole-frame cost.
Explain retained compromises and rejected alternatives in the owning design doc.

Implementation defects such as missing lights, invalid lifetimes, ABI mismatches
or nonfinite results must be fixed. Deliberate shading approximations are assessed
through their measured quality, timing and memory together. Do not change inputs,
shadow settings or image coverage to manufacture an apparent improvement.

Create correctness tests under the owning `Test` directories and opt-in timing
workloads under `Vortex/Benchmarks` in their own lighting benchmark executable.
Reuse current GPU/CPU profiling, fixtures and capture tooling. This precedes EX08:
it must not depend on its future measurement API, console work or ImGui automation.
Record exact targets/commands as implemented; planned commands are not usable tools.

## Deterministic workload envelope

Use seed-controlled static scenes and fixed simulation-time camera/light paths.
Record light positions, ranges, cones, colors, intensities, shadow settings,
geometry/materials, view projection and asset hashes. Freeze actual overlap
histograms and coverage before optimizing; do not change them to improve results.

| Family                            | Required cases                                                                                                                                                                                                                     | Purpose                                                                                                                   |
| --------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| Calibration and list boundaries   | 0/1 lights; directional None/Primary/Secondary/both; 31/32/33 and 64 lights contributing to one region.                                                                                                                            | Physical response, complete lists, zero-light clearing and additive contribution.                                         |
| Sparse indoor/city distribution   | 64, 256, 1,024 and 4,096 local lights, mixed point/spot; static and moving.                                                                                                                                                        | Separate total count from visible count and local overlap; primary target is 1,024.                                       |
| Mostly irrelevant lights          | Fixed visible subset, with the remainder off-frustum or outside all receiver influence; include off-screen lights whose volumes do reach receivers.                                                                                | Culling removes irrelevant work without rejecting valid off-screen sources.                                               |
| Dense overlap                     | At least 33, 64 and 256 relevant lights in the same region, narrow/wide cones and large ranges.                                                                                                                                    | Correctness when spatial culling cannot reduce real lighting work; stress, not a blanket 60-fps promise.                  |
| Projection and receiving surfaces | Perspective/orthographic; near-plane/camera-inside volumes; viewport offsets, partial tiles, reverse-Z and depth-slice edges; opaque, masked, two-sided/normal-mapped and translucent receivers.                                   | Conservative bounds and valid shader lookups for every supported consumer.                                                |
| Multiple views and lifecycle      | 1080p main plus 960x540 secondary; conflicting visibility, reordered views, resize/hide/recreate, scene replacement, moving/disabled/deleted lights and in-flight frames.                                                          | Per-view isolation, identity, update invalidation and resource retirement.                                                |
| Shadowed subset                   | In a 1,024-light scene: 0/1/4 point and 0/1/8 spot shadows; 5th/9th requests must work when budget permits. Include finite and 90-degree soft spots, isolated/combined directionals, and deliberate actual-byte-budget exhaustion. | Shadow cost and stable mapping, removal of inherited 4/8 limits, explicit failure/recovery at the real resource boundary. |

Both active forward and deferred paths require correctness coverage. Use 1920x1080
as the primary timing resolution and 3840x2160 for scaling. Execute the full light
count sweep at 1080p; restrict 4K to the primary and stress endpoints. The two-view
case and shadowed subset are selected interaction rows, not a Cartesian product
of every feature/count/resolution. The 4,096-light and dense-overlap cases remain
required correctness/scaling evidence, with measured throughput rather than an
unqualified 60-fps claim.

The primary case has equal point/spot populations and at least 256 local lights
with nonzero contribution to visible receiver samples in each timed frame. Record
projected influence area and overlap as well as that count. A scene with almost
all lights off-screen cannot substitute for this case. The separate irrelevant-
light workload measures that optimization explicitly.

## Correctness gates

1. **Independent reference.** Verify selected receiver samples with double-precision
   CPU physical/BRDF calculations. Compare production images against a small,
   opt-in unculled or serially accumulated reference using all eligible lights.
   That reference may reuse shading to isolate culling errors; it cannot replace
   the independent photometric oracle. Keep it in qualification targets, not a
   second shipping renderer. Match material decoding, P, output transform and time.
   `tools/vortex/AssertLightingPhysicalProbe.py <native-results.json>` checks
   report completeness, schema and model revision. Native implementation checks
   must pass; independent numerical differences quantify model-2 approximation
   quality rather than enforce the former reciprocal/integrated-source model.
2. **No false negatives.** Check that every light with a nonzero supported
   contribution at a sampled receiver is present exactly once. Conservative
   false positives are acceptable and measured. Test tangencies, spot-axis/sign
   conventions, near/far depth, off-screen source volumes and origin offsets.
   Do not use opaque-depth or normal rejection for translucent/two-sided/normal-
   mapped receivers unless its conservativeness is independently established.
3. **No truncation.** Size/scan/allocate complete lists or implement another
   correctness-preserving overflow path. Do not keep only the first/brightest N,
   silently clamp counts, or claim fixed capacity implies unsupported illumination.
   Freeze and validate total-light/list-memory limits in EX07A. Follow the LLD's
   atomic input rejection and invalid-view publication contract for overflow or
   allocation failure; verify caller diagnostics, presentation and recovery.
   Partial lighting cannot be reported as success; diagnostics do not spam frames.
4. **Image and accumulation.** Report model-2 differences from independent
   predictions. Apply the 0.5% relative + 2e-5 absolute float-product budget to
   matched culled/unculled results using the same shading model. Check light-order permutations, HDR accumulation,
   finite outputs and exactly-once pre-exposure. No disappearance/pop when crossing
   cells or culling boundaries. Freeze any summation-specific bound before results.
5. **Visibility and mutation.** Node visibility, affects-world, range/cone,
   intensity/color, transform and assignment edits invalidate the proper selection
   and view products. Light removal cannot reuse another light's shadow/index.
   Zero-light frames clear prior lists; recreated scenes/views inherit no stale state.
6. **Shadow semantics.** Direct-light admission and shadow-map capacity are separate.
   Preserve light-to-shadow identity after reordering/culling. Test supported local
   shadows in both consuming families, off-screen casters and moving receivers.
   Over-capacity requests must be explicit; silently rendering requested shadows
   as unshadowed is not a passing result. Freeze the supported shadow envelope and
   any capacity-policy decision before optimization; do not imply thousands of
   fully shadowed lights from a thousands-of-unshadowed-lights result.
7. **Lifetime and editor input.** Use immutable per-frame/view light products and
   fence-safe retirement for growing/reused buffers, descriptors and constants.
   Exercise failed submissions/readback delay under the D3D12 debug layer. Load a
   deterministic editor-authored fixture through source/cook/PAK/runtime and compare
   its resolved inputs with the native case; check live supported light mutations.
   Preserve EX06's C++20 SDK boundary and strict formats.
