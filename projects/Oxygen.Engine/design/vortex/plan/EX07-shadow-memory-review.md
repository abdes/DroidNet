# EX07 — Shadow memory review and selected optimizations

Date: 2026-09-22. Source review: repository HEAD `09aa65362`, with the existing
EX07 documentation changes. **Design selection and bounded native validation;
production implementation and performance qualification remain open.**

The user requested confidence that removing shadow-map stencil cannot remove a
required CSM, VSM or other rendering product, followed by a small set of useful
optimizations without substantial frame-time regressions. The
[EX07 execution plan](EX07-lighting-correctness-and-scalability.md#bounded-shadow-memory-work)
owns implementation order and acceptance. The approved 4 GiB total / 128 MiB
compact-index ceilings are unchanged; neither is a normal working-set target or
a whole-engine memory qualification.

## Stencil ownership audit

Light-space shadow depth, camera-space scene/custom stencil, receiver eligibility
and VSM metadata are distinct products. A format change is confined to the three
conventional shadow allocations. It must not change global depth defaults or
remove another product because its name contains "shadow" or "stencil".

| Product / owner                          | Source evidence                                                                                                                                                                                                                                                                                                                  | Decision                                                                                                                                                                                               |
| ---------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Conventional CSM, spot and point targets | `ConventionalShadowTargetAllocator.cpp` creates three D32S8 families; `CascadeShadowPass.cpp` routes all through `ShadowDepthPass::RecordSlices`.                                                                                                                                                                                | Select depth-only D32 for these allocations, subject to the production cutover checks below.                                                                                                           |
| Shadow rasterization                     | `ShadowDepthPass.cpp` disables stencil for opaque and masked PSOs. `DirectionalShadowDepth.hlsl` writes only `SV_Depth`; masked material handling clips fragments. The current clear requests depth **and stencil**.                                                                                                             | No useful stencil data is produced. Change the explicit clear to depth-only with the format; retain reversed-Z, bias, raster and alpha-test behavior. A descriptor-only edit is insufficient.          |
| Shadow receivers / inspection            | `DirectionalShadowCommon.hlsli` loads `Texture2DArray<float>` for CSM, spot and point PCF. Deferred lighting receives the textures only for state/lifetime tracking and depth sampling. Debug-view and exposure benchmark inspection routes also use the depth product. Published conventional bindings expose no stencil plane. | Keep the same depth sampling and publication interfaces. Repository-wide references to the three surface handles and `Inspect*ShadowSurface` reveal no conventional shadow-stencil reader.             |
| Scene/custom stencil                     | `SceneTextures.cpp` separately creates `SceneDepth` and optional `CustomDepth`; `SceneRenderer.cpp` publishes their stencil aspects.                                                                                                                                                                                             | Retain these resources, formats and bindings. This review does not qualify removing their stencil or repair their unrelated implementation details.                                                    |
| Receiver eligibility / contact depth     | The editor rendering contract carries Receive Shadows through per-instance/GBuffer data and defines a separate, conditional `ContactShadowCasterDepth`.                                                                                                                                                                          | Preserve their existing owners and algorithms. Do not infer either product from conventional shadow stencil or create a replacement mask pass.                                                         |
| Vortex VSM                               | `ShadowService.h::HasVsm()` returns false. `VsmFrameBindings` is separate; retained VSM shaders sample depth arrays and use explicit page metadata, hierarchy and output-mask resources. No active VSM allocator/raster path uses the conventional targets.                                                                      | Leave VSM sources, bindings and resource ownership unchanged. This is an interface/source audit, **not runtime VSM qualification**. Future activation retains its own format and validation decisions. |
| D3D12 backend                            | `Detail/FormatUtils.cpp` already maps `kDepth32` to R32_TYPELESS resource / D32_FLOAT DSV / R32_FLOAT SRV. `Texture.cpp` creates array views from those formats. `CommandRecorder.cpp` forwards explicit clear flags.                                                                                                            | Reuse these mappings. Update the shadow caller's flags and typed views/PSOs; do not globally change D32S8 handling.                                                                                    |

C++ paths above are under `src/Oxygen/Vortex`, except the identified
`src/Oxygen/Graphics/Direct3D12` backend files. Shader paths are under
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Shadows`.
The [receiver/contact contract](editor-v01-rendering-contract.md#4-conventional-shadows-and-receiver-control)
remains authoritative.

### UE 5.7 cross-check

Verified against the installed engine at `F:/Epic Games/UE_5.7/Engine`:

- `Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp`, both
  `AddClearShadowDepthPass` overloads: shadow-depth bindings use
  `DepthWrite_StencilNop`; stencil load action is `ENoAction`.
- `Source/Runtime/Renderer/Private/ShadowRendering.cpp`, projection setup around
  lines 2101–2135: projection can use `DepthRead_StencilWrite`, but its attachment
  is `SceneTextures.Depth.Target/Resolve` (or the separate hair depth target),
  **not the light-space shadow atlas**. Removing scene stencil would impair a
  different rendering operation and is not selected.
- `Source/Runtime/Renderer/Private/VirtualShadowMaps/VirtualShadowMapCacheManager.cpp`,
  `SetPhysicalPoolSize`: the VSM physical pool is `PF_R32_UINT`; page metadata is
  separately allocated. This supports independent product ownership, not changing
  Oxygen VSM to match that storage during EX07.

No replacement stencil generator is needed for conventional maps: the only
observed stencil operation there initializes an unread plane. Useful camera-space
stencil continues to be generated by its owning passes, in its existing target.

## Native evidence and limits

Reference RTX 3080, driver 610.62; native D3D12, sample count one:

- [D32S8 allocation requirements](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements.json)
  and [depth-only allocation requirements](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements-depth32.json)
  query `GetResourceAllocationInfo` for the medium/maximum directional,
  projected-spot, point-cube and wide-spot-cube descriptors. All eight tested
  large D32 arrays require half the D32S8 allocation. These queries create no
  shadow resources and do not measure resident/committed engine peaks.
- [Native GPU comparison](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/shadow-depth-format-probe.json)
  and [probe source](../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/shadow_depth_format_probe.cpp):
  six 32×32 array slices, per-slice DSVs, reversed-Z opaque draws and masked
  fragment discard, depth SRVs, 3×3 PCF and GPU readback. **12,288 values compare
  bit-for-bit; zero CPU-oracle mismatches, zero debug warnings/errors.**
  Small arrays both occupy a 64 KiB allocation: format savings must use backend
  sizes rather than assuming every texture is exactly halved.

The probe was built with VS 18 x64 `cl /std:c++20 /EHsc /W4 /WX`, linking
`d3d12.lib`, `dxgi.lib` and `d3dcompiler.lib`; its JSON records the source hash.
It validates native format/view/clear/raster/sample behavior. It does not run
Oxygen's scene, cascade setup, material pipeline, VSM or full-frame timing.
No production source or shader was modified for this review.

## Selected work

1. **Depth-only conventional targets.** Adopt `kDepth32` only for CSM, projected
   local and cube-local maps. Preserve 32-bit floating-point depth, requested
   resolutions, bias, reversed-Z and 3×3 PCF. Migrate allocation, optimized clear,
   per-slice DSV, SRV, PSO identity, explicit clear and test expectations together.
   Keep scene/custom stencil and VSM ownership untouched. No new user toggle,
   compatibility renderer or compensating stencil/mask pass.
2. **Share compatible local maps within a frame.** The same light may be rendered
   once for views with identical shadow content. Match light/source identity and
   mutation generation, transforms/projection/near-far/source extent, requested
   resolution/format/bias, complete caster eligibility and geometry/material
   generations, and coverage. Share only if equality is established; view-specific
   prepared-scene content is not automatically equivalent. Derive compatibility
   from existing versioned scene/light products rather than rescanning all geometry
   for every view pair. Keep per-view bindings,
   CSMs and contact depth separate. No cross-frame shadow-content cache is added.
3. **Bounded reuse and growth within the existing allocator.** Use the indexed
   ABI's resolution buckets so one high-quality light does not enlarge unrelated
   maps. Reuse unchanged compatible backing, grow only affected buckets, preserve
   unchanged published layers, and reclaim retired/cached storage only after its
   final GPU/external consumer. Bound retained spare capacity under the same
   budget, without repeated shrink/grow churn. Do not duplicate every map merely
   because several frame slots exist.

The current allocator holds one cached surface per kind and loops views through
it. Consequently, per-view CSM correctness and caster equivalence must be fixed
and proven before using sharing as an optimization. This review does **not**
claim that the current engine allocates duplicate complete per-view sets.
`RenderSpotView` / `RenderPointView` currently choose the maximum requested local
resolution for the entire kind; mixed-quality bucketing belongs with the already
required per-light quality/identity repair, not an automatic quality reduction.

Allocation arithmetic for two directionals × four medium cascades, four point
cubes and eight wide-spot cubes:

| Arrangement                                                   |  Map allocation |
| ------------------------------------------------------------- | --------------: |
| One complete D32S8 / D32 set                                  |   832 / 416 MiB |
| Two independent D32S8 sets                                    |       1,664 MiB |
| Separate per-view CSMs, shared compatible locals, D32S8 / D32 | 1,088 / 544 MiB |

The sharing totals are predictions for that compatible workload, not measured
current-engine savings or unconditional multi-view capacity. Other products,
growth, retained generations and heap overhead are additional.

## Production qualification and scope control

EX07C repairs required view ownership, shadow identity, eligibility and per-light
quality first. EX07D freezes correctly rendered baselines and numeric memory,
CPU/GPU/whole-frame regression and noise thresholds. EX07E introduces these
changes individually, with before/after attribution, then EX07F confirms the
integrated result. Existing source correctness failures cannot serve as baselines.

- For depth-only cutover, compare rendered depth and final visibility/images for
  CSM cascade boundaries/blends/motion, opaque/masked casters, projected spots,
  every point/wide-spot face and seams, and both forward/translucent and deferred
  consumers. Include non-default bias, debug/inspection and readback paths.
  Verify scene/custom stencil formats, bindings and seeded stencil values survive
  shadow rendering unchanged. Use debug-layer/GBV checks outside timed runs.
- For sharing/reuse, exercise same and different caster sets, camera/light/
  material mutation, two views, mixed resolutions, add/remove/resize, discarded
  submissions and delayed completion. Prove distinct CSM contents cannot overwrite
  another view and that every local consumer observes the matching generation.
- Measure unique live allocation requirements **and** committed heaps, including
  slack, pending growth, retired allocations and caches, plus process-local DXGI
  budget/usage. Distinguish whole-engine figures from lighting-owned attribution;
  account shared backing once. Admission remains bounded by the parent allocator
  after other commitments and explicit headroom; 4 GiB grants no reservation.
- A memory reduction is a useful result without a speedup, but must pass the
  predeclared CPU/GPU/whole-frame regression limits and absolute frame budgets.
  Include startup/growth/resize spikes and steady-state allocation churn. No
  queue-idle wait, serial render/shade/rerender scheme or lost async overlap is
  justified merely by a smaller memory counter. Failed candidates require repair
  or another bounded solution; documenting unused storage does not close the work.

Renderer-wide transient aliasing, a new frame graph/residency framework, texture
streaming changes, VSM activation, speculative face elimination, FP16 scene-color
policy changes and automatic resolution/light-count/update-rate reductions are
not added to EX07. Use existing lifetimes/fences/allocator infrastructure; do not
expand this review into unrelated engine optimization or repeat EX051 campaigns.
