# Vortex low-level designs

These documents own the renderer's technical contracts. For delivery and results,
use the [milestone roadmap](../PLAN.md). Shared policy and LLD expectations are in
[RULES.md](../RULES.md).

| Topic                                 | Design                                                           |
| ------------------------------------- | ---------------------------------------------------------------- |
| Base pass and GBuffer                 | [base-pass.md](base-pass.md)                                     |
| Captured-sky IBL                      | [captured-sky-ibl.md](captured-sky-ibl.md)                       |
| Conventional shadow sharing           | [conventional-shadow-sharing.md](conventional-shadow-sharing.md) |
| Cubemap processing                    | [cubemap-processing.md](cubemap-processing.md)                   |
| Deferred lighting                     | [deferred-lighting.md](deferred-lighting.md)                     |
| Depth prepass                         | [depth-prepass.md](depth-prepass.md)                             |
| Diagnostics                           | [diagnostics-service.md](diagnostics-service.md)                 |
| Editor rendering extension            | [editor-rendering.md](editor-rendering.md)                       |
| Atmosphere and fog                    | [environment-service.md](environment-service.md)                 |
| Exposure model and integration        | [exposure.md](exposure.md)                                       |
| Hierarchical depth                    | [hzb.md](hzb.md)                                                 |
| Indirect lighting                     | [indirect-lighting-service.md](indirect-lighting-service.md)     |
| View initialization                   | [init-views.md](init-views.md)                                   |
| Lighting model and capacity decisions | [lighting-decisions.md](lighting-decisions.md)                   |
| Lighting GPU ABI                      | [lighting-gpu-abi.md](lighting-gpu-abi.md)                       |
| Light properties and ingress          | [lighting-properties.md](lighting-properties.md)                 |
| Lighting service                      | [lighting-service.md](lighting-service.md)                       |
| Multi-view composition                | [multi-view-composition.md](multi-view-composition.md)           |
| Occlusion                             | [occlusion.md](occlusion.md)                                     |
| Offscreen rendering                   | [offscreen-rendering.md](offscreen-rendering.md)                 |
| Point-shadow filtering                | [point-shadow-filtering.md](point-shadow-filtering.md)           |
| Post-processing runtime               | [post-process-service.md](post-process-service.md)               |
| Runtime motion producers              | [runtime-motion-producers.md](runtime-motion-producers.md)       |
| Scene renderer                        | [scene-renderer-shell.md](scene-renderer-shell.md)               |
| Scene textures and resource lifetime  | [scene-textures.md](scene-textures.md)                           |
| Scene preparation                     | [sceneprep-refactor.md](sceneprep-refactor.md)                   |
| Shader contracts                      | [shader-contracts.md](shader-contracts.md)                       |
| Local-light shadows                   | [shadow-local-lights.md](shadow-local-lights.md)                 |
| Shadow service                        | [shadow-service.md](shadow-service.md)                           |
| Skybox and static SkyLight            | [skybox-static-skylight.md](skybox-static-skylight.md)           |
| Translucency                          | [translucency.md](translucency.md)                               |

The completed [substrate migration](../milestones/VTX-M01/implementation.md) and
[legacy retirement](../milestones/VTX-M07/migration.md) belong to their milestone records.
