# EX07 — Shadow memory review and selected optimizations

**Historical audit.** The selected D32/reuse work and subsequent cross-view
sharing are now implemented and qualified. Current ownership is documented in
[ShadowService](../../../../lld/shadow-service.md#compatible-local-map-ownership), with
[accepted measurements](validation.md). Later-work statements
below describe this audit's original checkpoint, not outstanding EX07 tasks.

Date: 2026-09-22. Source review: repository HEAD `09aa65362`, with the existing
EX07 documentation changes. **Updated 2026-09-24: D32, bounded allocation reuse,
local depth caching and focused/static performance acceptance are complete.
Cross-view sharing and broader workload qualification remain later work.**

The stencil review checks whether removing shadow-map stencil could remove a
required CSM, VSM or other rendering product, followed by a small set of useful
optimizations without substantial frame-time regressions. The
[EX07 execution plan](../validation.md#bounded-shadow-memory-work)
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
The [receiver/contact contract](../../../../lld/editor-rendering.md#4-conventional-shadows-and-receiver-control)
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

Reference RTX 3080 (nominal 10 GiB), driver 610.62; native D3D12, sample count one.
The historical descriptor query ran on 2026-09-23 through the versioned
[`ShadowAllocationRequirements_test.cpp`](../../../../../../src/Oxygen/Vortex/Test/Lighting/ShadowAllocationRequirements_test.cpp).
That version used descriptor conversion and `GetResourceAllocationInfo` without
creating shadow textures. It supplied the historical comparison below; it did not
validate production allocator output.

The [historical query](../../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/ex07a/allocation-requirements-current.json)
contains 18 observations: eight large descriptors and a small alignment control
for each format. All eight large D32 arrays require half the D32S8 allocation.
Both six-slice 32x32 controls require 64 KiB, so format savings must use backend
sizes rather than assuming every texture is exactly halved.

| Queried set              |     D32S8 |       D32 |
| ------------------------ | --------: | --------: |
| Medium, projected spots  |   512 MiB |   256 MiB |
| Medium, cube spots       |   832 MiB |   416 MiB |
| Maximum, projected spots | 2,048 MiB | 1,024 MiB |
| Maximum, cube spots      | 3,328 MiB | 1,664 MiB |

Each set includes eight directional layers and 24 point-cube layers; its spots
use either eight projected layers or 48 cube layers. These remain illustrative
resource sets, not product count limits. The query records DXGI's reported
10,539,237,376 dedicated bytes and the current local budget/usage separately.
Budget/usage is an instantaneous sample, not a resident-frame or whole-engine
memory qualification.

The current `NativeShadowAllocationRequirements` test now creates actual
`ConventionalShadowTargetAllocator` resources and queries their live native
`ID3D12Resource` descriptors. Both Debug and Release pass with:

| Actual D32 allocation       | Resolution | Layers | Native allocation |
| --------------------------- | ---------: | -----: | ----------------: |
| Directional                 |       1024 |      2 |             8 MiB |
| Low point chunk             |        512 |     60 |            60 MiB |
| Medium point chunk          |       1024 |     12 |            48 MiB |
| Appended medium point chunk |       1024 |     12 |            48 MiB |
| Low spot chunk              |        512 |     64 |            64 MiB |
| Medium spot chunk           |       1024 |     16 |            64 MiB |
| **Total**                   |            |        |       **292 MiB** |

All six resources have native R32 typeless backing with depth-stencil support and
64 KiB alignment. Each allocation's backend byte size matches the incremental
lighting budget charge. Reacquiring a chunk preserves its backing and charge;
releasing the allocator and draining deferred retirement restores the starting
budget. The GTest property records `shadow_resources_allocated = true` and each
actual descriptor/size. This measures allocator-owned textures, not whole-frame
committed heaps or process peaks.

Reproduce after building `Oxygen.Vortex.LightingGpuAbi.Tests` in either configuration:

```powershell
./out/build-tracy-ninja/bin/Debug/Oxygen.Vortex.LightingGpuAbi.Tests.exe -v=-1 --gtest_filter=LightingGpuAbiTest.NativeShadowAllocationRequirements
```

**Historical format comparison:** the 2026-09-22 standalone native run recorded
12,288 bit-identical depth/PCF values, zero CPU-oracle mismatches and zero debug
warnings/errors for D32 versus D32S8. Its original source/output files are no
longer available. Preserve that result as historical evidence, not a currently
reproducible GPU gate. Production cutover is now covered by the shadow service and existing native
ABI/image/admission tests. Manual visual approval of the conventional-shadow
changes and quality policy was received on 2026-09-24. The allocation test
measures resource descriptors and accounting; visual acceptance is manual.

## Selected work

1. **Depth-only conventional targets — complete.** Use `kDepth32` only for CSM, projected
   local and cube-local maps. Preserve 32-bit floating-point depth, requested
   resolutions, bias, reversed-Z and 3×3 PCF. Migrate allocation, optimized clear,
   per-slice DSV, SRV, PSO identity, explicit clear and test expectations together.
   Keep scene/custom stencil and VSM ownership untouched. No new user toggle,
   compatibility renderer or compensating stencil/mask pass.
2. **Local reuse — cross-frame complete; cross-view sharing remains later work.** The same light may be rendered
   once for views with identical shadow content. Match light/source identity and
   mutation generation, transforms/projection/near-far/source extent, requested
   resolution/format/bias, complete caster eligibility and geometry/material
   generations, and coverage. Share only if equality is established; view-specific
   prepared-scene content is not automatically equivalent. Derive compatibility
   from existing versioned scene/light products rather than rescanning all geometry
   for every view pair. Keep per-view bindings,
   CSMs and contact depth separate. Reuse unchanged local depths across frames
   with complete light-space coverage. Re-evaluate current caster membership,
   including objects entering the light volume. Publish reusable contents only
   after successful submission and retain the producing queue/fence dependency
   for consumers. Invalidate for light, caster, material, projection or resolution
   changes, including geometry-content revisions with stable descriptors. Current
   caches are per-view; the original audit's proposed cross-view sharing is not
   implemented by this conventional repair.
3. **Bounded reuse and growth within the existing allocator — complete.** Use the indexed
   ABI's resolution buckets so one high-quality light does not enlarge unrelated
   maps. Reuse unchanged compatible backing, grow only affected buckets, preserve
   unchanged published layers, and reclaim retired/cached storage only after its
   final GPU/external consumer. Bound retained spare capacity under the same
   budget, without repeated shrink/grow churn. Do not duplicate every map merely
   because several frame slots exist.

The audit baseline held one surface per kind and selected the family-wide
maximum local resolution. The conventional repair now owns per-view directional
surfaces and bounded local resolution chunks, with retained Nexus light slots
and independently validated per-light depth contents. The current implementation
does not share depth contents between views. Mixed-quality requests keep separate
resolutions under the selected projected-size quality profile.

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
quality first. EX07D is closed (2026-09-24); its
[baseline register](../EX07D/validation.md) records timing, memory scope,
quality and confidence limits in commit `894a25e57`. The
[EX07E handoff](validation.md#ex07e-handoff--closed)
subsequently closed with matched comparisons and explicit confidence limits.
[F engine acceptance](../EX07F/validation.md) credits that qualification;
final editor approval closes F and overall EX07.
Existing source correctness failures cannot serve as baselines. Delivered D32,
allocation ownership and local-map cache work remains credited by the tracker.

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
policy changes and allocation-pressure-driven resolution/light-count/update-rate reductions are
not added to EX07. Use existing lifetimes/fences/allocator infrastructure; do not
expand this review into unrelated engine optimization or repeat EX051 campaigns.
