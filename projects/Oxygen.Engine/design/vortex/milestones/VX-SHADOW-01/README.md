# VX-SHADOW-01 — Virtual shadow maps

Status: `planned`

| Field     | Summary                                                                                                                           |
| --------- | --------------------------------------------------------------------------------------------------------------------------------- |
| Outcome   | None yet. The design and this plan replace the retired-renderer VSM work; every VSM shader is rewritten on the current contracts. |
| Remaining | Prerequisites: camera-relative rendering (VX-VIEW-03) and the temporal resolve (VX-POST-02). S0 document approval, then S1–S13.   |
| Evidence  | Gates in [virtual-shadow-maps.md §12](../../lld/virtual-shadow-maps.md#12-validation-gates); findings in [review.md](review.md).  |
| Issue     | [abdes/DroidNet#60](https://github.com/abdes/DroidNet/issues/60)                                                                  |

The design owns every algorithm, format and gate:

- [virtual-shadow-maps.md](../../lld/virtual-shadow-maps.md)
- [shadow-service.md](../../lld/shadow-service.md) for the service and its
  conventional strategy
- [lighting-gpu-abi.md](../../lld/lighting-gpu-abi.md) for shadow
  association
- [occlusion.md](../../lld/occlusion.md) for the list builder reused by the
  raster

[review.md](review.md) records why the earlier VSM work is replaced and maps
every finding to a slice. This plan owns only order, files, tests, owner
actions and status. Paths are relative to `projects/Oxygen.Engine/src/Oxygen/`;
shaders are under `Graphics/Direct3D12/Shaders/Vortex/`.

## Resume

After a context compaction or in a new session:

1. Find the first slice in the [slice table](#slices) that is not
   `validated`.
2. Open its section and continue at the first unchecked box.
3. Run `git status` and `git log --oneline -8`.
4. Re-read only the LLD sections the slice cites.

Update the slice table, the checkboxes and the header table as work lands.
Commit messages describe the change; they never cite this plan or its IDs.

## Decisions

Owner answers to [LLD §2](../../lld/virtual-shadow-maps.md#2-decisions).

**Prerequisites.** Camera-relative rendering
([VX-VIEW-03](../VX-VIEW-03/README.md), VSM-D13) and the temporal resolve
([VX-POST-02](../VX-POST-02/README.md), VSM-D11) are delivered before S1
starts.

| Decision | Topic                | Value                                                                | Owner answer        |
| -------- | -------------------- | -------------------------------------------------------------------- | ------------------- |
| VSM-D1   | Stage placement      | Stage 8 aggregation point; receiver producers before it              | approved 2026-10-12 |
| VSM-D2   | Selection            | Renderer-wide `shadow_method`, all light kinds                       | approved 2026-10-12 |
| VSM-D3   | Consumer model       | SMRT projection for opaque receivers; direct sampling for the rest   | approved 2026-10-12 |
| VSM-D4   | Pool and raster      | `R32_UINT` pool, atomic-max pixel shader                             | approved 2026-10-12 |
| VSM-D5   | Raster submission    | `VsmDrawRecord` indirection, one `ExecuteIndirect` per segment       | approved 2026-10-12 |
| VSM-D6   | Static caching       | Two-slice data model from S1–S2; static slice enabled in S9          | approved 2026-10-12 |
| VSM-D7   | Pool budget          | 1024 / 2048 / 2048 / 4096 pages for Low–Ultra                        | approved 2026-10-12 |
| VSM-D8   | `detail_geometry`    | Kept; set by pixel marking                                           | approved 2026-10-12 |
| VSM-D9   | Distant local lights | Single-page maps and refresh budget in S10                           | approved 2026-10-12 |
| VSM-D13  | Numeric domain       | Renderer camera-relative convention; per-map shadow origin           | approved 2026-10-12 |
| VSM-D10  | Legacy documents     | Retired with the coverage map in review.md                           | approved 2026-10-12 |
| VSM-D11  | SMRT noise           | Temporal resolve (VX-POST-02) delivered first; no interim noise path | approved 2026-10-12 |
| VSM-D12  | SMRT source size     | Authored angular size and source radius drive penumbrae              | approved 2026-10-12 |

## Working rules

- **Commits.** One thematic commit per slice once its tests pass; no review
  pause. Work stops only at the **Owner** boxes. Design documents stay
  uncommitted until the owner approves their text (S0).
- **No legacy reference.** Retired `Oxygen.Renderer` code is not consulted
  for implementation; UE5.7 sources in LLD §1.3 are.
- **GPU tests.** One GPU test process at a time, filtered to the tests being
  worked on. A test that removes the D3D12 device is never rerun by me; the
  owner gets the command.
- **Builds.** I build engine targets and tests in Debug. The owner builds the
  SDK and the editor and runs the editor.
- **Fail loudly.** Every pass validates its descriptors and capacities on the
  CPU before recording. Shaders do not hide wiring errors with early returns.
- **Publication.** VSM uses the existing descriptor and upload contracts:
  `Graphics::RetireResource`, `StructuredGpuBuffer` and the transient upload
  ring ([descriptor lifetime](../../../../src/Oxygen/Graphics/Common/README.md#descriptor-lifetime),
  [upload.md](../../lld/upload.md)); no VSM-specific lifetime scheme.
- **Pass inputs.** Every VSM pass is a non-camera pass with one inputs record
  ([shader-contracts §4.9](../../lld/shader-contracts.md#49-pass-inputs-and-data-integrity));
  VSM layouts are generated-schema entries.
- **Issues.** Work deferred out of a slice becomes an abdes/DroidNet issue
  and an `OPEN_ITEMS` row in the same change. An issue this plan resolves is
  closed with its evidence when the resolving slice lands.
- **Captures.** RenderDoc up to v1.46 crashes replaying count-buffer
  `ExecuteIndirect` ([#30](https://github.com/abdes/DroidNet/issues/30)), which the VSM raster uses; capture VSM
  passes with PIX until that issue is closed.
- **Docs.** Update owner docs in the slice that changes the behavior they
  describe.

Commands, from `projects/Oxygen.Engine` after `vcvars64`:

```text
cmake --build --preset oxygen-ninja-debug --target <targets>
ctest --preset oxygen-ninja-debug -R '^<test>$' --output-on-failure
./tools/cli/oxyrun.ps1 <test> -Preset oxygen-ninja-debug -NoBuild -- --gtest_filter=<case>
./tools/cli/oxyformat.ps1 <files> --fix
./tools/cli/oxytidy.ps1 <files> --configuration Debug --include-tests --build-dir out/build-ninja
python tools/vortex/CheckDocumentation.py
```

## Slices

| Slice | Scope                                      | Depends on                 | Status    |
| ----- | ------------------------------------------ | -------------------------- | --------- |
| S0    | Design and decision approval               | —                          | `planned` |
| S1    | Selection, ABI and contracts               | S0, VX-VIEW-03, VX-POST-02 | `planned` |
| S2    | Physical pool and cache identity           | S1                         | `planned` |
| S3    | Directional CPU setup                      | S2                         | `planned` |
| S4    | GPU page management                        | S3                         | `planned` |
| S5    | Caster raster                              | S4                         | `planned` |
| S6    | Directional consumers and page-state debug | S5                         | `planned` |
| S7    | Spot and point lights                      | S6                         | `planned` |
| S8    | Caster invalidation and caching            | S6                         | `planned` |
| S9    | Static slice, merge, per-page HZB culling  | S8                         | `planned` |
| S10   | Distant local lights                       | S7, S8                     | `planned` |
| S11   | Volumetric fog froxel marking              | S6                         | `planned` |
| S12   | Statistics and tooling                     | S6                         | `planned` |
| S13   | Closeout                                   | S1–S12                     | `planned` |

### S0 — Design and decision approval

- [x] **Owner:** answer VSM-D1–D12 (2026-10-12).
- [x] Apply the answers: the LLD, this plan, review.md, ARCHITECTURE,
      depth-prepass, shadow-service, lighting-gpu-abi, occlusion, OPEN_ITEMS,
      PLAN, PROJECT-LAYOUT, STATUS, capabilities, ED-M08 deferred
      capabilities and VTX-FUTURE; retire the three
      `design/renderer-shadows/virtual-shadow-map-*.md` documents.
- [x] Independent review (2026-10-12) applied: coarse marking, clipmap
      radius, receiver-bit mechanism, normal bias, dilation, page-table
      sizes, gates, conventional regression, tooling.
- [x] **Owner:** answer VSM-D13 (numeric domain): camera-relative, on the
      renderer convention of VX-VIEW-03.
- [x] Re-review against the landed VX-GPU-01 code applied: generated
      contracts, retirement and buffer APIs, device-local epoch buffers,
      translucent-bounds marking, volumetric-fog froxel marking (S11,
      [abdes/DroidNet#57](https://github.com/abdes/DroidNet/issues/57)),
      inventory and line references.
- [ ] **Owner:** review [virtual-shadow-maps.md](../../lld/virtual-shadow-maps.md),
      [review.md](review.md) and this plan.
- [ ] **Owner:** approve committing the documents; one commit including the
      retirement of `design/renderer-shadows/virtual-shadow-map-*.md`.

### S1 — Selection, ABI and contracts

LLD §2 (D2, D8), §3, §4.3–4.7. Findings A12, A14, B9, B11, B13, B14.

Files: `Config/RendererConfig.h`; from `projects/Oxygen.Engine`,
`Examples/RenderScene/main_impl.cpp` and
`Examples/DemoShell/Runtime/DemoAppContext.h`;
`Vortex/Meta/ShaderContracts.yaml` (new `vsm` entry; `shadows` entry gains
`kShadowProjectionVirtual = 4`) and its generated outputs; remove
`Vortex/Types/VsmFrameBindings.h`, `Contracts/Shadows/VsmFrameBindings.hlsli`
and their `Vortex/CMakeLists.txt` entry;
`Lighting/Types/LightingPreparationFailure.h`;
`Services/Shadows/Vsm/{VsmCommon,VsmPageTable,VsmPhysicalPageMeta,VsmPageFlags,VsmProjectionData}.hlsli`,
`VsmContracts_CS.hlsl`; `Graphics/Direct3D12/Shaders/EngineShaderCatalog.h`;
the never-set `directional_shadow_vsm_active` flag and its sites
(`LightingService`, `DeferredLightPass.h`, `SceneRenderer`).

- [ ] Replace `DirectionalShadowImplementationPolicy` with
      `ShadowMethod { kConventional, kVirtual }`; RenderScene option
      `--shadow-method conventional|vsm`.
- [ ] Until S6 lands, `kVirtual` fails each scene view with the new
      `LightingPreparationError::kShadowStrategyUnavailable`.
- [ ] Page flags include `kDetailGeometry`; pool, flags and raster records
      carry the static slice from the start (D6, D8).
- [ ] Records and encodings per LLD §4 as schema entries; the generator
      emits the size/offset parity checks and the domain-checked
      `TryLoadVsmFrameBindings`; `kShadowProjectionVirtual = 4`.
- [ ] Catalog `VsmContracts_CS`; extend it to every record. Every retained
      VSM shader moves off `ViewConstants.hlsli` and `ConstantBuffer` pass
      constants to one `StructuredBuffer` inputs record (shader-contracts
      §4.9) as it is rewritten.
- [ ] Tests: ABI parity in `LightEvaluationAbi_test.cpp` and the GPU probe
      (G2), `LightingGpuAbiProbe.hlsl` updated; config parsing; the
      conventional path is unchanged: the existing `ShadowService` and
      `SceneRendererDeferredCore` tests (their `HasVsm` cases updated) and a
      conventional `VsmTwoCubes` image pass.
- [ ] Docs: `Examples/RenderScene/README.md` documents
      `--shadow-method`.

### S2 — Physical pool and cache identity

LLD §4.2, §4.5, §9; D4, D7.

Files: `Vortex/Shadows/Vsm/Internal/VsmPhysicalPool.{h,cpp}`,
`VsmCacheEntries.{h,cpp}`; `Vortex/Shadows/Vsm/VsmShadowStrategy.{h,cpp}`;
`Vortex/Shadows/ShadowService.{h,cpp}`.

- [ ] `VsmShadowStrategy` owned by `ShadowService`; `HasVsm()` reports the
      configured method.
- [ ] Pool: `R32_UINT` array, metadata, available lists, counters; budget per
      tier; recreation on budget change retires all entries; retirement after
      the last submitted epoch.
- [ ] Cache entries: `VsmCacheId`/`VsmMapId` through Nexus `IndexReuse` with
      generations; per-entry last-epoch page-table regions; aging
      (`kVsmMaxLightAgeEpochs`).
- [ ] Tests: entry lifecycle, generation reuse, aging, pool recreation (G1).

### S3 — Directional CPU setup

LLD §4.1, §4.6, §5.1, §8.1. Findings A2, A11.

Files: `Vortex/Shadows/Vsm/Internal/VsmClipmap.{h,cpp}`;
`VsmShadowStrategy.cpp`.

- [ ] Shadow origins and projections built in double on the renderer's
      camera-relative convention (D13); test that fine-level marking is
      stable at 100 km from the origin.
- [ ] Clipmap construction: levels 6–22 in metres, per-level snap, corner
      and relative corner offsets, depth range, authored maximum distance.
- [ ] Reuse rules and page offsets per level; light-level invalidation.
- [ ] Epoch slot packing, `map_id → slot` table, projection upload.
- [ ] Tests: snapping stability under sub-page motion, pan offsets, reuse
      rejection cases, light rotation, maximum-distance level drop (G1).

### S4 — GPU page management

LLD §4.3–4.4, §5.3–5.8. Findings A1, A3, A4, B7, B8, B12.

Files: `Services/Shadows/Vsm/VsmPageMarking.hlsl`,
`VsmPageManagement.hlsl` (from `VsmPageRequestGenerator`, `VsmPageReuse`,
`VsmPackAvailablePages`, `VsmAllocateNewPages`,
`VsmGenerateHierarchicalFlags`, `VsmPropagateMappedMips`);
`Vortex/Shadows/Vsm/Passes/VsmPageManagementPass.{h,cpp}`.

- [ ] Stage 3 receiver bit: the depth prepass splits its raster segments by
      resolved Receive Shadows and sets the stencil reference per segment
      (`Vortex/SceneRenderer/Stages/DepthPrepass/`, the occlusion list
      builder's segment key); test that the bit is set exactly on receiver
      pixels.
- [ ] Marking: one level per pixel, two-sided dilation, UE coarse pass
      (2×2 origin pages per coarse level, last local mip); pixel marking
      sets `kDetailGeometry`, coarse marking does not; receiver stencil bit
      from Stage 3 skips non-receivers (owner of the bit:
      [depth-prepass.md](../../lld/depth-prepass.md)).
- [ ] Translucent-bounds marking (LLD §5.3) from the receiver fit's
      translucent draw boxes, with a per-draw page budget. The existing
      `OcclusionCullPassConstants.receiver_mask_srv` hook is where caster
      culling tests `HPageFlags`.
- [ ] Update physical pages, parallel pack with LRU, GPU allocation with
      overflow count, `HPageFlags`, uncached rects, mapped-mip propagation
      for clipmaps, page initialization.
- [ ] CPU contract checks before every dispatch.
- [ ] Tests on synthetic depth (G3): one level per pixel; every request
      mapped in the same epoch; cached pages kept across a static epoch;
      pan by whole pages keeps content; overflow falls back to a coarser
      level; fallback entries never valid for rendering.

### S5 — Caster raster

LLD §5.9; D5. Findings A5, A9, B10.

Files: `Services/Shadows/Vsm/VsmRaster.hlsl`;
`Vortex/Shadows/Vsm/Passes/VsmRasterPass.{h,cpp}`; the occlusion list
builder (`Vortex/SceneRenderer/Stages/Occlusion/`) for raster views and
`VsmDrawRecord` output; [occlusion.md](../../lld/occlusion.md) §6.4.

- [ ] Raster views per (slot, mip) with uncached pages; candidates from the
      light's caster dependencies, never camera visibility.
- [ ] List builder: frustum and `HPageFlags` page test per raster view;
      small casters only into `kDetailGeometry` pages; `VsmDrawRecord`
      emission.
- [ ] Render-target-less draw, mip scale/bias, clip planes, pancaking;
      opaque and masked pixel shaders with `InterlockedMax`.
- [ ] Tests (G4): known caster depth in its pages only; nothing outside the
      uncached rect; masked alpha test; cached pages untouched.

### S6 — Directional SMRT, consumers and page-state debug

LLD §4.7, §5.11, §6, §10; D3, D11, D12. Findings A7, A8, B5.

Files: `Services/Shadows/Vsm/{VsmSmrt.hlsli,VsmProjection.hlsl,VsmSampling.hlsli}`;
`Vortex/Shadows/Vsm/Passes/VsmProjectionPass.{h,cpp}`; deferred, forward,
translucency and volumetric-fog shadow evaluation; `ShadowReferenceBuilder`;
diagnostics registry; `Stages/Translucency/ForwardDebug_PS.hlsl` and its
eight catalogued `DEBUG_VIRTUAL_SHADOW_MASK` permutations
(`EngineShaderCatalog.h`). The debug modes need
[abdes/DroidNet#54](https://github.com/abdes/DroidNet/issues/54) fixed first.

- [ ] Publish `VsmFrameBindings`, `VsmLightRecord`s and `Virtual`
      references; lift the S1 failure for directional lights.
- [ ] SMRT projection into per-light directional masks with normal offset,
      screen-ray bias, extrapolation and the D11 noise policy; forward and
      deferred opaque lighting read the masks.
- [ ] Shared direct sampling with LOD fallback, normal offset and seam-free
      PCF for translucency and fog.
- [ ] Editor: the directional angular-size control's label and tooltip say it
      sets the sun's size for both the sky disk and VSM shadow softness
      (VSM-D12); the owner builds the editor.
- [ ] Page-state, level, pool and per-light mask debug modes; the old
      multiplied-mask debug mode and passes are replaced.
- [ ] Tests: `VsmTwoCubes` and `physics_domains` directional images (G5);
      multi-frame camera rotation and pan with no unmapped opaque samples
      (G6); consumer agreement (G9); directional penumbra behaviour (G11).
- [ ] Debug layer clean; one analysed RenderDoc epoch (G10).
- [ ] **Owner:** visual check in the editor with `vsm` selected.

### S7 — Spot and point lights

LLD §4.1, §5.3 step 3, §6.2. Finding B6.

Files: `Vortex/Shadows/Vsm/Internal/VsmLocalMaps.{h,cpp}`; marking and
sampling shaders.

- [ ] Spot maps and six point-face maps with mip chains and min mip from
      `ShadowResolutionHint`.
- [ ] Light-grid marking with footprint mips; coarse mip marking with
      `ForceCacheDynamicCoarse`.
- [ ] Face selection; packed local SMRT mask (16 lights per pixel) with
      direct-sampling overflow; local direct sampling.
- [ ] Tests: spot and point images (G5); two views sharing a spot map in one
      frame (G8); a 20-light cluster packs 16 and samples the rest (G11).
- [ ] **Owner:** visual check of local lights.

### S8 — Caster invalidation and caching

LLD §5.2, §8.2–8.3. Findings A13, B4.

Files: `Vortex/Shadows/Vsm/Internal/VsmInvalidationQueue.{h,cpp}`;
`Services/Shadows/Vsm/VsmInvalidation.hlsl`.

- [ ] Fingerprint diff from `ShadowCasterDependencies`; non-reusable casters
      every frame; per-entry pending lists with light-influence culling and
      overflow to `Uncached`.
- [ ] GPU invalidation through last-epoch page tables with the owner check;
      dynamic invalidation of local coarse pages limited to
      `kDetailGeometry` pages.
- [ ] Tests (G7): static camera converges to zero rendered pages; a moving
      caster re-renders only overlapping pages; removed caster clears its
      shadow; LOD switch re-renders.

### S9 — Static slice, merge and per-page HZB culling

LLD §4.5, §5.8, §5.10, §8.2; D6. Findings A6, A15, B1, B2, B3.

Files: `Services/Shadows/Vsm/VsmPageMaintenance.hlsl` (from
`VsmStaticDynamicMerge`, `VsmHzbBuild`);
`Vortex/Shadows/Vsm/Passes/VsmPageMaintenancePass.{h,cpp}`.

- [ ] Two-slice pool, static classification by frames-static threshold,
      static-to-dynamic initialization copy.
- [ ] In-place `max` merge of selected pages; per-page furthest (`min`) HZB
      with page-local top mips; HZB culling of dynamic casters.
- [ ] Tests: merged depth equals a single-slice render of the same scene;
      a moving dynamic caster leaves static pages cached; HZB never culls a
      visible caster.

### S10 — Distant local lights

LLD §2 (D9). UE `VSM_MAX_SINGLE_PAGE_SHADOW_MAPS` and distant-light
refresh budget.

- [ ] Single-page maps for local lights below a footprint threshold and a
      per-epoch refresh budget ordered by age and invalidation.
- [ ] LLD section for the policy written before code.
- [ ] Tests: many-light scene keeps every light shadowed with a bounded
      number of re-rendered single pages per epoch.

### S11 — Volumetric fog froxel marking

LLD §1.2.1, §5.3. Resolves
[abdes/DroidNet#57](https://github.com/abdes/DroidNet/issues/57).

Files: `Vortex/SceneRenderer/Stages/Hzb/` (occupied-froxel list),
`Services/Shadows/Vsm/VsmPageMarking.hlsl`,
`Vortex/Shadows/Vsm/Passes/VsmPageManagementPass.{h,cpp}`.

- [ ] Stage 5 builds the occupied-froxel list from scene depth on the
      volumetric fog grid; empty when the view renders no volumetric fog.
- [ ] Froxel marking pass for directional and local VSM pages.
- [ ] Tests: synthetic depth with a fog volume marks the expected pages and
      none behind the closest depth; a fog view samples fine pages in front
      of the camera (`CityEnvironmentValidation` shaft image).
- [ ] Close #57 with the evidence.

### S12 — Statistics and tooling

LLD §5.11, §10. Finding D9.

- [ ] Page statistics and overflow through GPU feedback into the diagnostics
      panel; rate-limited overflow warning.
- [ ] Replace `tools/RunVsmTests.ps1`, the `tools/vsm/` analyzers
      (`AnalyzeRenderDocCapture.py`, `AnalyzeRenderDocStage15Masks.py`) and
      `tools/directional_vsm/compare_bmp.py` with Vortex test and capture
      entry points, or remove them; the same for the `vsm-bug1-flat` and
      `physics_domains_vsm_benchmark` scenes and `prepare_showcase.py`.
- [ ] **Owner:** decide editor exposure of `shadow_method` (LLD §13).

### S13 — Closeout

- [ ] Conventional shadow method rechecked: existing tests and the
      conventional `VsmTwoCubes` image.
- [ ] LLD status, [shadow-service.md](../../lld/shadow-service.md) and
      [OPEN_ITEMS](../../OPEN_ITEMS.md) updated; `STATUS.md` regenerated.
- [ ] All gates G1–G11 recorded in this plan.
- [ ] **Owner:** final visual approval.
