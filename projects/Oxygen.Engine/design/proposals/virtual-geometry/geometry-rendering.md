# Virtual geometry rendering and cross-pass contracts

Status: proposal. Part of [virtual geometry](README.md). Owner module: Vortex.

Last updated: 2026-10-28

Read: [stages](#stage-placement), [draw and shading](#draw-and-shading-contract),
[shadow and temporal identity](#shadow-and-temporal-identity),
[failure](#failure-and-lifetime), [verification](#verification).

## Stage placement

Use the reserved `GeometryVirtualizationService` family. Today it is a comment
at Stage 4 in `SceneRenderer.cpp` and an empty `Vortex/GeometryVirtualization/`
directory; services otherwise sit at the top level of Vortex and shaders under
`Shaders/Vortex/Services/`. The
[stage order](../../vortex/ARCHITECTURE.md#62-runtime-stage-contract) is kept:
no second renderer graph and no conventional shadows moved ahead of their
dependencies.

| Stage                         | Virtual geometry extension                                                                                                          |
| ----------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| Before per-view recording / 2 | Resolve admitted instances, accepted installs, the resource epoch and declared views; no CPU cluster traversal                      |
| 3 DepthPrepass                | Classic geometry depth and occlusion. Completeness is `kIncomplete` whenever the view needs virtual opaque geometry from Stage 4    |
| 4 Geometry virtualization     | GPU cut, cluster culling, virtual opaque and masked depth, the final camera selection snapshot, and per-cluster two-phase occlusion |
| 5 HZB / occlusion             | The canonical screen HZB builds from final depth including Stage 4; downstream consumers see truthful completeness                  |
| 8 Shadows                     | Independent shadow-view selection and error policy; conventional shadow depth consumes virtual casters plus classic casters         |
| 9 BasePass                    | Reuse the camera selection, deformation and material snapshot for the GBuffer and the current opaque velocity policy                |
| Later geometry consumers      | Attach the declared snapshot and epoch, or run their own qualified view selection; never reuse the camera list by accident          |

`SceneRenderer` owns orchestration and resource-product publication. Depth
completeness is the existing per-view `DepthPrePassCompleteness` on the render
context. Stage 3 currently publishes scene depth only when complete; with a
virtual instance in the view it publishes `kIncomplete` (the partial-depth
copy for the occlusion pyramid still happens), and Stage 4 completes and
publishes scene depth. Shadow receivers, the screen HZB and other consumers
already gate on completeness, so no parallel depth binding is added. Stage 5
remains the canonical final-depth HZB owner. Classic occlusion in Stage 3
cannot be occluded by virtual depth; it just draws, which is correct and costs
only culling opportunity. Forward and custom paths need explicit
qualification and must not silently omit virtual geometry just because
deferred is the first target.

Stage 4 runs its own two-phase cluster culling (see
[selection](geometry-selection.md#visibility-and-history)): it draws last
frame's visible clusters, builds a private transient pyramid from classic final
depth plus that partial virtual depth, tests the remaining clusters, and
completes depth. The private pyramid never escapes with a complete-depth
claim, and the logical cut is fixed for depth and shading.

## Draw and shading contract

### Public API and reuse

Stage integration, material bins and selected-work records stay
Vortex-private. Reuse `CommandRecorder`, the frame and scene publications,
material evaluation, `ViewId` and shadow identities. Composite representation
and product keys use existing owner IDs plus domain `NamedType`s, never bare
interchangeable integers. HLSL wire records may be shared without exporting
their mutable C++ storage. Mesh-shader APIs need a later Graphics owner
change, not speculative exports. See the [shared rules](README.md#shared-rules).

Virtual instances publish through `PreparedSceneFrame` like classic draws: a
new immutable span of per-instance records (transform and previous-transform
handles, material and submesh bindings, manifest identity, sidedness and
revision) with its own bindless slot beside the existing draw metadata and cull
records. ScenePrep emits them from a virtual representation reference instead
of a per-LOD `GeometryRef`.

The initial backend is hardware rasterization with GPU-generated indirect draw
batches and vertex pulling. CPU submission scales with compatible pipeline and
material batches, not with visible clusters or resident pages. A draw renders
`visible_clusters × cluster_triangles × 3` vertices and decodes cluster and
triangle from `SV_VertexID` through the compacted list, with short clusters
padded by degenerate triangles; the draw index travels in
`StartInstanceLocation`. Existing `ExecuteIndirect` supplies counted draws with
the cluster count as the GPU-written count, and range validation is the
[Graphics gap](paged-resource-backing.md#d3d12-realization) closed first.

The visible-work record holds the instance index, `ClusterId`, manifest and
selection identity and a resolved page handle or validated mapping reference.
Cluster identity addresses geometry; instance identity provides transforms,
material slots, sidedness and revision. Page handles are validated against the
selected epoch. The record layout, command arguments and material-bin bounds
are frozen with C++/HLSL layout probes before use.

Depth and GBuffer use the same decoded positions, winding, transforms,
clipping, masked-material inputs and supported deformation, and keep the
existing GBuffer, HDR, normal and velocity conventions. Geometry
virtualization does not authorize a visibility-buffer rewrite, a software
micropolygon rasterizer or a second opaque material evaluation path. That is
this proposal's scope, not a ceiling: a compute rasterizer cannot write the
GBuffer, so it comes with a visibility buffer and a material resolve pass as
one later renderer decision
([evolution](README.md#rasterization-evolution)). Baseline
support is static opaque and masked geometry; masked materials still write the
GBuffer with real alpha clip. Unsupported deformation or material modes are
explicit admission errors or asset-policy fallbacks to a classic
representation, never arbitrary missing draws.

Quantized positions reconstruct on the Data-defined compatible lattice and
then to camera-relative coordinates with a common arithmetic path. Material
coordinates are authored surface coordinates independent of topology and page
placement. Texture residency and composed surface caches have separate layouts,
budgets and updates.

Mesh shaders are a later execution profile over the same page, group and
resource contracts. They need, first, a Graphics mesh pipeline state, a
`DispatchMesh` command and tier detection (all absent today, though the shader
compiler can build mesh stages), then the vertex-pulling path stays as the
fallback. Cluster limits come from measurement within the mesh-shader
output maximum of 256 vertices and 256 triangles, so that profile reads the
same pages; no vendor's wave or output recommendation is baked into page
identity.

## Shadow and temporal identity

Shadow views select their own valid cuts, including off-camera casters. Their
demand merges with camera demand while each keeps its error policy,
projection and visibility. A page relocation does not change shadow-producing
surface content; an approximation change may, even when the authored surface
revision did not.

### Conventional-shadow cache policy

The existing local-map reuse decision is CPU-side and precedes GPU selection.
`PrepareLocalShadowRequest` already filters each caster dependency's bounds
against the light range, and the spot frustum for non-cube lights, before it
clears the request's reusable flag; the target allocator then takes the
non-cached path. Virtual casters use this unchanged:

- Publish a conservative `ShadowCasterDependency` for every admitted virtual
  caster that can contribute to each prepared shadow view, including off-camera
  instances, with `reusable = false` and no classic record. The dependency
  owner is extended; no classic buffer-address `ShadowCasterRecord` is
  fabricated. Conservative world bounds are used, and invalid or unavailable
  bounds never justify omitting a contributor.
- Only products a virtual caster intersects bypass cache lookup and insertion
  and refresh fully. Classic-only and disjoint lights keep reuse. For point
  lights the acquisition unit is the whole cube, so all six faces refresh;
  independent face caching is outside this change. Directional maps keep their
  refresh policy and include virtual draws. Each requesting view and light uses
  its own prepared dependencies.
- This needs no GPU readback and is correct when arriving detail changes a
  shadow cut with no CPU-known revision change. Its cost is the lost cache hits
  and cross-view reuse of affected products, plus the existing copy-on-write
  pressure while prior readers hold old maps. Refresh targets still obey
  reader exclusion and retirement.

GPU-conditional clear, draw and publication would be a larger cache contract:
zero indirect draws alone cannot safely replace conditional clearing, and a
CPU asset epoch cannot stand in for independently changing GPU shadow cuts.

Representation-change information publishes separately for temporal consumers
and later shadow optimization, scoped by consumer kind, view and shadow product
(light plus projection-family identity), surface region and manifest revision,
because different products may select different cuts at once. Physical
offsets and descriptors never enter semantic identity. Relocation alone emits
no surface or approximation change, although the conservative policy still
refreshes an affected map. GPU change notifications are not the trigger for CPU
shadow reuse decisions.

Temporal consumers distinguish rigid motion, approximation change and real
surface edit or deformation. Stable UVs do not prove cross-topology
correspondence. Rigid motion uses the existing previous-transform and camera
contract; an approximation change marks localized history rejection rather than
inventing object velocity. Dynamic deformation needs bounded geometry and
previous-state evaluation in every geometry pass and is outside this design.

When the planned [virtual shadow maps](../../vortex/lld/virtual-shadow-maps.md)
exist, their caster culling and raster stage (which already reuses the
occlusion `IndirectListBuilder`) consumes virtual casters through this same
selection snapshot and keeps its own page demand and invalidation.

Edited manifests publish a coherent region and boundary revision only when
ready. Old selections and their registrations stay ordered through their final
users. Collision publication is a separate terrain transaction: a rendering
receipt alone cannot authorize a gameplay or collision revision switch.

## Failure and lifetime

Pool registrations, immutable per-recording input buffers and the selected
epoch use attach before recording, reusing the established receipt
dependencies. No raw callback outlives its owning state; submitted batches
retain internal tokens, not a service or renderer facade.

Unavailable fine content keeps a valid coarse selection. Invalid baseline
admission, unsupported materials, malformed geometry, recording or submission
error and unknown execution remain explicit failures. A complete depth product
is never published after a required geometry pass was skipped. A failed region
replacement continues the previously admitted revision when its contract
permits.

## Verification

- Native clustered geometry depth and GBuffer positions and masked coverage
  agree.
- Stage 3 partial and Stage 4 complete publication reach the canonical Stage 5
  HZB correctly, with truthful completeness for every consumer.
- Bounded indirect batches handle exact and full output and keep the selection
  fallback.
- A stationary light and CPU revision with newly selected detail matches an
  uncached spot or cube reference on its first consuming frame; off-camera
  virtual casters disable affected reuse while disjoint classic-only products
  still hit the cache; delayed old readers preserve copy-on-write safety.
- Relocation preserves the image and semantic identity with no surface-change
  event; two shadow products with different cuts have independent change
  state.
- Representation changes and authored revision changes produce distinct,
  localized notifications.
- Recorded readers, captures and old revisions survive delayed execution and
  failure safely.
- Camera motion and disocclusion recover through the two-phase occlusion.

Decoded geometry is compared numerically and native images and resources are
analyzed: a matching command count or an unanalyzed capture is insufficient
evidence.
