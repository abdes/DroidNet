# Occlusion Culling LLD

**Phase:** 5C - Remaining Services
**Deliverable:** D.16
**Status:** `planned`

## Summary

Vortex culls occluded draws on the GPU in the same frame, with two phases per
view (§4):

- Phase 1 draws the depth of last frame's visible draws.
- Their furthest HZB then tests every draw.
- Phase 2 draws the newly visible ones.

The same machinery runs for the camera view and for every rendered shadow view
(§6.4). Every pass draws its visibility through GPU-compacted
`ExecuteIndirect` lists (§5), which keep each pass's CPU sort order.

Correctness never depends on history:

- A stale or missing history only moves work between the two phases.
- A draw that is visible this frame is drawn this frame.

The culling unit is a mesh view. The cooker splits large submeshes into
spatially coherent mesh views, so large meshes are culled in parts (§2.1).
Cluster culling and a mesh-shader raster path are the documented next steps
(§10).

This design replaces the CPU-readback tester (§9). That design culled from
results one or more frames old and had no recovery for a wrong cull.

## 1. Scope And Context

### 1.1 What This Covers

`OcclusionModule` owns per-view visibility for the prepared draws of camera
and shadow views:

- per-draw culling records and their per-view visibility history
- GPU frustum, coverage and HZB occlusion tests for opaque, masked, translucent
  and shadow-caster draws
- GPU construction of the indirect draw lists that depth, base, translucency
  and shadow passes consume
- fallback, invalidation and diagnostics for that visibility

It does not own:

- HZB construction, owned by `ScreenHzbModule` ([hzb.md](hzb.md))
- mesh-view splitting and bounds, owned by geometry cooking
  ([geometry pipeline](../../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md#mesh-build-performed-by-meshbuildpipeline),
  [format](../../content-pipeline/geometry-cooking-architecture.md#95-meshviewdesc-40-bytes-pakformat_geometryh))

### 1.2 Stage Position

Occlusion splits the depth prepass in two:

| Order | Work                           | Owner                | Product                            |
| ----- | ------------------------------ | -------------------- | ---------------------------------- |
| 3.1   | Phase 1 cull and list build    | `OcclusionModule`    | Phase 1 depth lists                |
| 3.2   | Phase 1 depth draws            | `DepthPrepassModule` | Partial `SceneDepth`               |
| 3.3   | Occlusion pyramid              | `ScreenHzbModule`    | Transient furthest pyramid         |
| 3.4   | Phase 2 cull, history update   | `OcclusionModule`    | Phase 2 depth lists, final bits    |
| 3.5   | Phase 2 depth draws            | `DepthPrepassModule` | Complete `SceneDepth`              |
| 5     | Screen HZB                     | `ScreenHzbModule`    | Published HZB from complete depth  |
| 8     | Per shadow view: steps 3.1-3.5 | `ShadowService`      | Complete shadow depth (§6.4)       |
| 9     | Base pass list build, draws    | `BasePassModule`     | GBuffer from the final visible set |
| 18    | Translucent cull and draws     | `TranslucencyModule` | Back-to-front translucent lists    |

`PartialDepth` is copied after 3.5, so `DepthPrePassCompleteness` stays
complete.

### 1.3 References

The algorithm follows the two-pass occlusion of GPU-driven pipelines: Haar and
Aaltonen, "GPU-Driven Rendering Pipelines" (SIGGRAPH 2015); UE5.7 Nanite main
and post culling passes (`Renderer/Private/Nanite/NaniteCullRaster.cpp`). The
box-against-HZB test keeps the shape of UE5.7 `Shaders/Private/HZBOcclusion.usf`
with the corrections in §4.5.

### 1.4 Rejected Alternatives

| Alternative                                  | Why rejected                                                                                                                                                                                                                    |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| CPU readback of HZB results (previous model) | Culls from 1-3 frame old results: disocclusion pops in late. The prepass drew draws the base pass skipped, leaving depth with no GBuffer.                                                                                       |
| Same-frame predication of base-pass draws    | Culls the base pass only; the prepass and shadows still draw everything. The graphics layer has no predication API.                                                                                                             |
| Phase 1 against reprojected previous HZB     | Reprojection leaves holes and needs a depth-reprojection pass. It only saves overdraw during fast camera motion. Two phases test against real current depth instead.                                                            |
| Atomic-append compaction                     | Nondeterministic order: equal-depth surfaces flicker. Loses front-to-back and material order.                                                                                                                                   |
| Per-instance culling inside batches          | Batches never span scene nodes (`DrawMetadataEmitter` batching key includes the node), so their bounds are tight. `SV_InstanceID` ignores `StartInstanceLocation`.                                                              |
| Compute triangle culling                     | Fixed-function rasterization already discards back-facing and zero-area triangles. Compute culling pays off only for very high triangle counts with expensive vertex work; cluster cone culling (§10.1) covers the useful part. |

## 2. Data Model

### 2.1 Culling Units And Records

A draw is one mesh view of one submesh, so the mesh view is the culling unit.
Geometry cooking splits large submeshes into spatially coherent mesh views and
stores each view's local bounds. A large mesh is therefore culled in parts
without a second culling level.

`DrawMetadataEmitter` uploads one `DrawCullRecord` per draw, in draw-metadata
order, beside the existing metadata:

```cpp
struct DrawCullRecord {        // 32 bytes, std430-compatible
  glm::vec3 box_center;        // local space, or world (kWorldSpaceBox)
  std::uint32_t _pad0;
  glm::vec3 box_extent;        // half size, >= 0
  std::uint32_t flags;         // DrawCullFlags
};
```

- **Local box.** A single draw stores its mesh view's local bounds. The cull
  kernel transforms the 8 corners by the draw's world matrix (from
  `DrawMetadata.transform_index`). The result is an oriented box, up to √3
  tighter per axis than a world AABB of a rotated mesh, at the same cost.
- **Instanced batch.** A batch stores the world AABB union of its instances
  and sets `kWorldSpaceBox`.
- **Deformed meshes.** Skinned and morphed draws use the same bind-pose
  bounds as scene-prep frustum culling. Occlusion is never less conservative
  than the frustum test for them. Their bounds under animation are a
  deformation-runtime concern ([VX-MOTION-01](../OPEN_ITEMS.md)).
- **Flags.** `DrawCullFlags` mirrors what culling needs from
  `DrawMetadata.flags`: opaque, masked, transparent, shadow caster, main-view
  visible. Everything else stays in `DrawMetadata`.
- **Bad bounds.** A draw with non-finite bounds has `kAlwaysVisible` and is
  never culled.

The record is published through `DrawFrameBindings` (`cull_records_slot`) so
every list builder reads it the same way. The bounding sphere remains for
other consumers.

The record carries no history slot: one prepared frame serves several culling
views (the camera and its shadow views), and each view assigns its own slots
(§2.2).

### 2.2 Visibility History

History is advisory: it decides which phase draws a draw, never whether it is
drawn.

Draw indices change every frame because they follow the sort order. A draw's
identity across frames is its `DrawSourceKey`:

- the scene node
- `data::LodIndex`
- `data::SubmeshIndex`
- `data::MeshViewIndex`

The LOD is part of the key, so after a LOD switch the new mesh starts without
history. `MeshViewIndex` is a new strong index beside the other two in
`Data/GeometryIndices.h`.

`OcclusionModule` keeps one `HistorySlotAllocator` per culling view (camera or
shadow). Each frame it:

1. Maps every draw's key to a slot. A key seen last frame keeps its slot.
2. Gives a new key a fresh slot. Its history reads as not visible.
3. Frees the slots of keys absent this frame.
4. Gives no slot to a key that produced more than one draw this frame. Those
   draws are tested in phase 2 every frame.

The view uploads one slot word per draw, in draw order: the slot, with bit 31
set when the slot is fresh, or `0xFFFFFFFF` without a slot.

The GPU history buffer is one `uint` per slot: 1 if the draw was visible after
the last phase 2. It grows with the slot capacity. Growth copies the existing
slots, so history survives.

### 2.3 Per-Frame Visibility State

Each culling view owns a per-frame `uint` per draw, written by the cull
kernels:

| Bit            | Meaning                                                   |
| -------------- | --------------------------------------------------------- |
| `kInFrustum`   | box intersects the view frustum and covers a pixel center |
| `kPhase1Drawn` | in frustum and visible last frame (or history reset)      |
| `kPhase2Drawn` | in frustum, not phase 1, passes the occlusion test        |
| `kVisible`     | in frustum and passes the occlusion test (next history)   |

The final drawn set is `kPhase1Drawn | kPhase2Drawn`. All opaque consumers
draw exactly this set.

## 3. Indirect Lists

### 3.1 Command Layout

All mesh passes use vertex pulling with non-indexed draws and follow the mesh
draw index contract (ARCHITECTURE §10.3): the draw index travels in
`StartInstanceLocation` and shaders read it as `SV_StartInstanceLocation`. One
indirect command is therefore a plain draw record:

```cpp
struct IndirectDrawCommand {   // 16 bytes, D3D12_DRAW_ARGUMENTS
  std::uint32_t vertex_count_per_instance;
  std::uint32_t instance_count;
  std::uint32_t start_vertex_location;    // 0
  std::uint32_t start_instance_location;  // the draw index
};
```

The command signature is the backend's plain `DRAW` signature. It changes no
root arguments, so it needs no root signature and serves every pipeline; root
constant 1 (pass constants) is set by the CPU before each `ExecuteIndirect`.

### 3.2 Lists, Buckets and Order

A list is a pass's ordered candidate draws plus a visibility predicate:

- The CPU builds each list's candidate order exactly as today: depth
  front-to-back, base pass by material/LOD/submesh, translucency
  back-to-front.
- Candidates are cut into segments: maximal runs of consecutive candidates
  with the same `MeshRasterState` (masked, double-sided, reverse winding).
  Each segment needs its own PSO. Runs, not regrouping, keep the CPU order
  across segments, which back-to-front translucency requires.
- The CPU uploads the candidates and the segment table once per list.

The GPU keeps the candidates that satisfy the list's predicate, in candidate
order. It writes their commands and one count per segment. The CPU issues one
`ExecuteIndirect` per segment, with the segment's candidate count as the
maximum and the GPU count as the count. Segments never overflow because
capacity comes from the CPU's candidate count.

### 3.3 Order-Preserving Compaction

Compaction is one stream compaction over the whole list:

1. Evaluate the predicate per candidate.
2. Scan the 0/1 flags, each workgroup over 256 candidates.
3. Scan the workgroup totals. One extra level covers lists above 65,536
   candidates.
4. Scatter each kept command to its segment's first slot plus its rank in
   the segment, and write each segment's count.

No step depends on scheduling order, so lists are deterministic and keep the
CPU sort. Decoupled look-back is not used, because D3D12 does not guarantee
forward progress between workgroups.

## 4. Two-Phase Algorithm

This section uses the camera view. Shadow views run the same steps with the
differences in §6.4.

### 4.1 Phase 1

`OcclusionModule::BuildPhase1(ctx, view)` dispatches one thread per draw:

```text
box        = OrientedBox(record, world_matrix)
in_frustum = kAlwaysVisible
             || (FrustumIntersects(view, box) && CoversPixelCenter(view, box))
slot       = view_slot_words[draw]                          (§2.2)
prev       = history_valid && slot valid && !fresh(slot) && history[slot]
phase1     = in_frustum && (prev || history_reset || !occlusion_enabled)
```

`CoversPixelCenter` is exact:

- It rejects a box whose projected rectangle, clamped to the view rect,
  contains no pixel center. The rasterizer would produce nothing for it.
- A box crossing the near plane always passes.

It then compacts the depth prepass lists with predicate `kPhase1Drawn`.
`DepthPrepassModule` draws them into `SceneDepth`, opaque segments before
masked.

### 4.2 History Reset

Each of these resets a culling view's history:

- `history_discontinuity`, which covers a camera cut
- an unjittered projection or view-rect change
- view recreation
- a frame whose phase 2 did not run, such as one with occlusion off

After a reset every in-frustum draw is phase 1 for one frame. That frame
renders like occlusion disabled, front-to-back, and seeds the history.

### 4.3 Occlusion Pyramid

`ScreenHzbModule` builds a furthest-only pyramid from the phase 1 depth into a
per-view transient texture ([hzb.md §4.5](hzb.md#45-occlusion-pyramid)). It is
never published as the frame's Screen HZB and never becomes HZB history.

### 4.4 Phase 2

`OcclusionModule::BuildPhase2(ctx, view)` dispatches one thread per in-frustum
draw. It tests every in-frustum draw, including phase 1 draws:

```text
visible = kAlwaysVisible || !occlusion_enabled
          || !OccludedByHzb(occlusion_pyramid, box)
phase2  = in_frustum && !phase1 && visible
history[slot] = in_frustum && visible        (when the draw has a slot)
```

- It compacts the depth prepass lists with predicate `kPhase2Drawn`.
- `DepthPrepassModule` draws them, which completes `SceneDepth`.
- Stage 5 then builds the published Screen HZB from complete depth.

A phase 1 draw can fail its own test only behind other phase 1 depth; the
test is conservative against its own surface (§4.5). It is still drawn this
frame. Next frame it moves to phase 2.

Phase 2 catches disocclusion in the same frame. A draw whose occluder moved
away is not covered by phase 1 depth at its current position, so it passes
and is drawn now.

### 4.5 Box Test

`OccludedByHzb` is conservative at every step:

1. Project the 8 oriented-box corners with the view-projection the
   rasterizer used, jitter included, so the rectangle matches the phase 1
   depth texels.
2. If any corner has `w <= near_epsilon` or `z >= w`, the box crosses the
   near plane: visible.
3. Take the pixel centers inside the projected rectangle and the view rect.
   If any lies outside the pyramid's source rect, there is no depth for it:
   visible.
4. Take the box's nearest device depth as the maximum corner depth
   (reversed-Z).
5. Map the pixel range to mip-0 texels (texel `t` reduces pixels `2t` and
   `2t + 1`) and climb mips until the range spans at most 2 x 2 texels. The
   last mip is at most 2 texels wide, so the climb always ends.
6. Load those texels, at most 2 x 2, and keep the furthest. Use `Load`, not
   filtered sampling.
7. Convert both depths to linear view depth with the projection's z and w
   rows. The box is occluded when its nearest view depth exceeds the furthest
   occluder view depth by more than `depth_bias` × occluder depth.
   Comparisons with NaN or infinite depths never cull.

This fixes the three test defects of the previous shader:

- Near-plane boxes were read as far, and so could be culled.
- Axis offsets underestimated the projected extent of a sphere.
- `floor(log2)` mips sampled only 5 of up to 9 texels.

The bias is relative and in linear depth, never a fixed device-depth epsilon,
because reversed-Z precision is not uniform. The pyramid's coverage of every
source texel is an [hzb.md gate](hzb.md#91-unit--integration-proof).

### 4.6 Rasterization Invariance

The base pass tests `GreaterOrEqual` against prepass depth. Both passes must
produce bit-identical positions for the same draw, so `DepthPrepass.hlsl` and
`BasePassGBuffer.hlsl` compute clip position through the same function,
declared `precise`.

## 5. Consumers

| Consumer                          | List predicate                             | Notes                                                                   |
| --------------------------------- | ------------------------------------------ | ----------------------------------------------------------------------- |
| Depth prepass phase 1 / phase 2   | `kPhase1Drawn` / `kPhase2Drawn`            | Opaque then masked segments.                                            |
| Base pass (all modes)             | `kPhase1Drawn \| kPhase2Drawn`             | Deferred, forward, radiance replay and wireframe all draw the same set. |
| Base pass velocity auxiliary pass | final set and the draw's velocity-aux flag | Subset of the base pass set.                                            |
| Contact-shadow caster depth       | final set                                  | Reuses the depth list candidates.                                       |
| Translucency                      | `kVisible`                                 | Phase 2's test against phase 1 depth; keeps back-to-front order.        |
| Shadow depth phase 1 / phase 2    | caster and the shadow view's phase bit     | Shadow-view culling only (§6.4).                                        |

No consumer reads visibility on the CPU. A pass never draws a draw the depth
prepass skipped, and the prepass never draws one the base pass skips.

Translucent draws are tested by phase 2 like every other draw. Phase 1 depth
is a subset of complete depth, so a draw it occludes is occluded by complete
depth too; testing against the Screen HZB instead would cull a little more for
another dispatch.

## 6. Policies

### 6.1 Enablement

`vtx.occlusion.enable` turns the occlusion test on or off for camera and
shadow views, and defaults to `true`.
A camera view culls by occlusion only when it runs the depth prepass, whose
phase 1 depth phase 2 tests against, and uses reversed-Z depth.

With occlusion off:

- Phase 1 draws every in-frustum draw.
- Phase 2 lists are empty and no occlusion pyramid is built.

Every pass still draws through the GPU lists, so one draw path serves both
settings. `vtx.occlusion.max_candidate_count` is removed: capacities come from
CPU counts and nothing overflows.

### 6.2 Fallbacks

| Condition                     | Behavior                              |
| ----------------------------- | ------------------------------------- |
| Occlusion disabled            | Frustum-only phase 1; no phase 2.     |
| History reset (§4.2)          | All in-frustum draws in phase 1.      |
| Occlusion pyramid unavailable | Phase 2 treats every draw as visible. |
| Draw without history slot     | Tested in phase 2 every frame.        |
| Non-finite bounds             | `kAlwaysVisible`.                     |
| Near-plane-crossing box       | Visible (§4.5).                       |

No fallback can drop a draw.

### 6.3 Multiple Views

Everything in §2-§4 is per culling view. Camera views are keyed by `ViewId`,
shadow views by their shadow-view identity (§6.4). Each culling view has its
own:

- slot allocator
- history
- visibility state
- lists and pyramids

Views never share visibility. `SceneRenderer::RemoveViewState` releases a
camera view's occlusion state; a shadow view's state is released with its
shadow map. A camera view without a view-state handle keeps nothing across
frames: it has no history, so it culls by frustum only, records no counters
and leaves no state behind.

### 6.4 Shadow Views

Camera visibility never culls shadow casters, because a caster hidden from the
camera can still cast a visible shadow. Each rendered shadow view culls its
own casters with §4:

- a directional cascade
- a projected local-light map
- a cube-map face

Shadow-view occlusion is exact. A caster hidden from the light by another
caster casts a shadow entirely inside that occluder's shadow, so not drawing it
changes no shadow-map texel.

Differences from the camera view:

- **Identity.** History is keyed by the shadow view's identity: its
  surface and slice, the light, and the generation of the map allocation. It
  is not reset when the light's projection moves: cascades follow the camera
  every frame, and a stale history only moves work between phases.
- **Candidates.** Candidates are every shadow caster; the GPU frustum and
  coverage test of phase 1 removes those outside the light view. This
  replaces the CPU sphere tests of `ShadowCasterCulling`.
- **Pyramid.** The occlusion pyramid is built from the slice of phase 1
  shadow depth through the same `HzbPyramidBuilder`, which reads one array
  slice. Pyramids are transient: the shadow depth pass keeps one per extent
  and reuses it slice after slice.
- **Depth encodings.** The box test compares depths in the target's
  encoding (`DrawCullDepth`):
  - Cascades store orthographic reversed-Z device depth; linear depth is the
    distance from the near plane, `1 - z`.
  - Projected local maps store `1 - axial distance / range`, written by the
    shadow depth shader. A box's nearest depth comes from its clip `w`, the
    axial distance.
  - Cube faces rasterize perspective reversed-Z depth, linearized like the
    camera's.
- **Bias.** The shadow depth shader only moves written depth away from the
  light (constant and slope bias). A caster culled because its nearest depth
  lies behind the furthest written occluder depth would have written depth
  further still, so culling it changes no texel; the camera's relative bias
  suffices.
- **Caching.** A cached local map that is reused without re-rendering runs no
  culling. A map's cache dependency set remains every caster in the light's
  frustum, not only the drawn ones. A moving occluded caster therefore still
  invalidates the map, and the next render tests it again.

## 7. Diagnostics

The cull and list kernels accumulate per-culling-view counters in a GPU stats
buffer:

- draws, in frustum, coverage-culled, and history slots
- phase 1 drawn, phase 2 drawn, and occluded
- translucent culled

The stats are read back asynchronously for `DiagnosticsService` and capture
manifests (`Vortex.OcclusionFrameResults`). They are diagnostics only:
rendering never reads them back. Camera views count; shadow slices do not
count yet.

Pass markers, inside `Vortex.Stage3.DepthPrepass`:

- `Vortex.Stage3.Occlusion.Phase1`
- `Vortex.Stage3.DepthPrepass.Phase1`
- `Vortex.Occlusion.PyramidBuild`
- `Vortex.Stage3.Occlusion.Phase2`
- `Vortex.Stage3.DepthPrepass.Phase2`
- `Vortex.Stage8.ShadowDepths.Occlusion.Phase1` / `Phase2`, per shadow
  slice
- `<Pass>.Lists`, one per list build, such as
  `Vortex.Stage9.BasePass.Lists`

## 8. Validation Gates

1. CPU tests:
   - `HistorySlotAllocator` keeps slots for persistent keys and frees absent
     ones.
   - Fresh slots read as not visible.
   - LOD changes produce new keys.
   - Duplicate keys get no slot.
   - Growth preserves history.
2. CPU tests:
   - list candidate order and segments match the existing pass sorts
   - cull records carry mesh-view local bounds; instanced batches carry world
     AABBs
3. GPU tests, one test process at a time:
   - a hidden box without visible history is culled in the frame it is
     tested; one drawn last frame is drawn in phase 1 once more and culled
     the next frame
   - an occluder moved away reveals its occludee in the same frame
   - a box straddling the near plane is drawn
   - a rotated thin mesh near an occluder edge is kept or culled per its
     oriented box
   - a draw covering no pixel center is culled; one covering a single center
     is drawn
   - masked occluders with holes do not cull through the holes
   - a LOD switch and a camera cut never drop a visible draw
4. GPU tests:
   - prepass, base pass and velocity lists contain exactly the final set
   - two runs of the same input produce identical lists
   - translucent lists keep back-to-front order
5. GPU tests, shadow views:
   - camera occlusion never removes a caster
   - a caster hidden from the light behind another caster is culled, and the
     shadow map is texel-identical to occlusion disabled
   - a moving occluded caster invalidates a cached local map
6. GPU test: a large chunked mesh partly behind an occluder draws only its
   unoccluded mesh views.
7. Multi-view test: two views with different occluders keep independent
   visibility.
8. The shaders are in the Direct3D12 shader catalog and pass ShaderBake. The
   D3D12 debug layer reports nothing for the occlusion path.
9. A capture of a controlled scene shows fewer base-pass and shadow draws with
   occlusion enabled, and an image identical to occlusion disabled.

## 9. Superseded Design

The CPU-readback `HzbOcclusionTester` and `OcclusionFrameResults`
(`visible_by_draw` bytes) are removed:

- The base pass stops reading `ctx.current_view.occlusion_results`.
- The unused shadow culling shaders `VsmInstanceCulling.hlsl` and
  `ConventionalShadowCasterCulling.hlsl` are replaced by the shared list
  builder.

Documents updated with the implementation:

- ARCHITECTURE stage table rows 3, 5 and 8
- [depth-prepass.md](depth-prepass.md) (two phases)
- [base-pass.md](base-pass.md) (indirect lists)
- [translucency.md](translucency.md)
- [shadow-service.md](shadow-service.md) (two-phase caster lists)

[OPEN_ITEMS.md](../OPEN_ITEMS.md):

- VX-OCC-01 (shadow reuse of camera visibility) is resolved by §6.4: no reuse.
- VX-OCC-02 (AABB candidates) is resolved by §2.1.
- VX-OCC-04 tracks this implementation.
- VX-OCC-05 and VX-OCC-06 track §10.
- VX-CULL-01 (CPU per-submesh culling stability) is unaffected.

## 10. Future Culling Granularity

Mesh views of a few thousand triangles are the granularity this design
delivers. The two steps below are where finer granularity goes next. They
build on §2-§5 without replacing any of it.

### 10.1 Cluster Culling Through Vertex Pulling (VX-OCC-05)

**What it is.** The cooker partitions each mesh view into clusters of about
128 triangles, each with:

- local bounds
- a normal cone for back-facing rejection

**How it works.**

1. The cull kernels gain a second level: a visible draw expands into its
   clusters.
2. Each cluster gets the same frustum, coverage and two-phase occlusion tests,
   plus the cone test.
3. Visible clusters are compacted per draw.
4. The draw renders `visible_clusters × 128 × 3` vertices.
5. The vertex shader decodes cluster and triangle from `SV_VertexID` through
   the compacted cluster list. Short clusters pad with degenerate triangles.

This is the approach that GPUs without mesh shaders use. It needs no
graphics-layer feature Oxygen lacks: vertex pulling, compute and
`ExecuteIndirect` are all present.

**Why it is not in this design.**

- Mesh views already cull large meshes in parts.
- Clusters add four things:
  - a cluster asset format
  - a second culling level
  - a decode path in every mesh vertex shader
  - their own validation surface

**Built on.** It is built on this design's culling records, history keys,
lists and compaction. A cluster's history key extends `DrawSourceKey` with
the cluster index.

**Sequencing.** It starts after VX-OCC-04 meets its validation gates (§8).

### 10.2 Mesh-Shader Raster Path (VX-OCC-06)

**Deferred because a capability is missing.**

- ShaderBake compiles amplification and mesh shaders.
- The graphics layer has neither a mesh pipeline state stream nor a
  `DispatchMesh` command.
- It does not detect the hardware mesh-shader tier.

**What it would add.** With clusters (§10.1) in place, an amplification shader
would cull clusters and a mesh shader would emit their triangles. That
replaces the vertex-pulling decode and its padding. It changes efficiency, not
which triangles are culled.

**Prerequisites.**

1. Graphics-layer mesh pipeline and `DispatchMesh` support, with tier
   detection.
2. §10.1 clusters.
3. The vertex-pulling path kept as the fallback for hardware without mesh
   shaders.
