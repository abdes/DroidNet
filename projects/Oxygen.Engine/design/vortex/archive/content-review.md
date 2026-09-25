# Content review — 2026-09-25

Audience: engine implementers, maintainers and reviewers. The review replaces
session instructions with implementation behavior, decisions, measurements and
named remaining work. The original documents remain in the legacy backup and
Git revision recorded in [README.md](README.md).

## Source corrections

| Previous statement                                                      | Corrected record                                                                              | Source inspected                                                                                                                                                                                                                                                                      |
| ----------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| ED-M08 planned, implementation not started                              | In progress; native work started, remaining field/rendered gates open                         | [Editor progress](../../../../../design/editor/IMPLEMENTATION_STATUS.md#ed-m08---runtime-parity-and-standalone-validation)                                                                                                                                                            |
| Height/local/volumetric fog still needs implementation                  | Implemented paths and recorded qualification; cubemap fog and reflection AP remain open       | [Environment publication](../../../src/Oxygen/Vortex/Environment/EnvironmentLightingService.cpp), [volumetric shader](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/VolumetricFog.hlsl)                                                                 |
| Runtime WPO bridge missing                                              | WPO history exists; skinned/morph streams are empty                                           | [InitViews](../../../src/Oxygen/Vortex/SceneRenderer/Stages/InitViews/InitViewsModule.cpp), [history cache](../../../src/Oxygen/Vortex/Internal/DeformationHistoryCache.h)                                                                                                            |
| Lighting ingress lacks atmosphere roles/RGB transport; old packed sizes | Canonical fields are transported; common/directional/point/spot records are 34/97/50/58 bytes | [Packed records](../../../src/Oxygen/Data/PakFormat_world.h), [Lua](../../../src/Oxygen/Scripting/Bindings/Packs/Scene/SceneNodeLightBindings.cpp), [hydration](../../../Examples/DemoShell/Services/SceneLoaderService.cpp); managed/editor DTOs and property applier also inspected |
| Exposure requires a current scene-v6 cutover                            | Scene version 7 is current; the earlier exposure-prefix migration is historical               | [Version and layout](../../../src/Oxygen/Data/PakFormat_world.h)                                                                                                                                                                                                                      |
| Backend headless support unknown                                        | Swapchain-free D3D12 fixtures exist; full-engine headless lifecycle qualification remains     | [Offscreen fixture](../../../src/Oxygen/Graphics/Direct3D12/Test/Fixtures/OffscreenTestFixture.h)                                                                                                                                                                                     |
| Forward and mixed-mode views are future work                            | Current per-view dispatch supports deferred and solid forward paths                           | [SceneRenderer](../../../src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp)                                                                                                                                                                                                           |
| Local shadows and translucent sidedness remain deferred                 | Both implemented; optimization/extended shading work tracked separately                       | [ShadowService](../../../src/Oxygen/Vortex/Shadows/ShadowService.cpp), [MeshRasterState](../../../src/Oxygen/Vortex/Internal/MeshRasterState.h), [translucency](../../../src/Oxygen/Vortex/SceneRenderer/Stages/Translucency/TranslucencyModule.cpp)                                  |
| All IBL debug modes unsupported                                         | Service-path IBL modes exist; specific probe and culling modes remain disabled                | [Debug registry](../../../src/Oxygen/Vortex/Diagnostics/ShaderDebugModeRegistry.cpp)                                                                                                                                                                                                  |
| Bloom and temporal-color bindings imply implemented features            | Bloom forwards an external texture; temporal-color handoff remains unused                     | [BloomChain](../../../src/Oxygen/Vortex/PostProcess/Internal/BloomChain.cpp), [PostProcessService](../../../src/Oxygen/Vortex/PostProcess/PostProcessService.cpp)                                                                                                                     |

The point-PCF decision was rechecked against local UE5.7
`ShadowProjectionCommon.ush` (comparison sampling and 1/5/12/29 kernels),
`ShadowFilteringCommon.ush` (projected filtering), and `BaseScalability.ini`
(High/Epic select quality 5). The rewritten decision distinguishes Oxygen's
1/5/29/29 profiles from the rejected gather and nine-load alternatives.

## Progress and remaining work

- All 47 milestone/package READMEs lead with status, outcome, remaining work and
  evidence. [STATUS.md](../STATUS.md) is generated from those blocks.
- [OPEN_ITEMS.md](../OPEN_ITEMS.md) contains 44 identified items: 3 incomplete,
  1 pending, 33 deferred, 4 design decisions and 3 verification gaps.
- The tracker separates completed-but-stale entries from genuine gaps. It also
  retains the CPU-budget shortfall, the registration-construction OOM coverage
  limit and the [104 unavailable local run artifacts](2026-09-25-unavailable-run-artifacts.json).

## Preservation and checks

`CheckDocumentation.py --migration` checks retained/reviewed source content,
links and all 2,048 captured-evidence hashes. The 177 stable work-item/property/
deferred IDs remain present. The old `EX01-06` text was a slice-range abbreviation,
not a work-item ID; the six slices have individual homes.

The [migration map](migration-map.json) records each rewrite and its rationale.
[Source-review records](content-review.json) retain the initial targeted review
inputs and before/after hashes. Engine runtime tests were not rerun for this
documentation review; their recorded dates and qualification limits remain in
the milestone evidence.
