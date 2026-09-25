# Exposure and LightBench

Status: `validated`

| Field     | Summary                                                                         |
| --------- | ------------------------------------------------------------------------------- |
| Outcome   | EX01–EX10 delivered, including performance, quality, console and widget slices. |
| Remaining | Extensions: [VX-CPU-01](../../OPEN_ITEMS.md#p2--engineering-follow-ups).        |
| Evidence  | [Validation record](validation.md)                                              |

The package is validated through EX10 (2026-09-25). Each slice below owns its
scope, task status and acceptance record. The delivered production precision
policy is FP32/P=1; checked half storage remains an explicit diagnostic.

| Slice                      | Delivery                                |
| -------------------------- | --------------------------------------- |
| [EX01](EX01/README.md)     | Contracts                               |
| [EX02](EX02/README.md)     | Fixed exposure                          |
| [EX03](EX03/README.md)     | Metering and adaptation                 |
| [EX04](EX04/README.md)     | Lifecycle and sharing                   |
| [EX05](EX05/README.md)     | HDR migration                           |
| [EX05.1](EX05.1/README.md) | Performance                             |
| [EX05.2](EX05.2/README.md) | Code quality                            |
| [EX06](EX06/README.md)     | Persistence                             |
| [EX07](EX07/README.md)     | Lighting and scalability                |
| [EX08](EX08/README.md)     | Calibrated reference                    |
| [EX08.1](EX08.1/README.md) | Console                                 |
| [EX09](EX09/README.md)     | Controls and renderer qualification     |
| [EX08.2](EX08.2/README.md) | Widget automation, delivered after EX09 |
| [EX10](EX10/README.md)     | Final acceptance                        |

The accepted CPU optimization follow-up is in [EX05.1](EX05.1/README.md).
The approved scope changes are in [EX08](EX08/README.md).

## Source entry points

### File entry points

The shader root below is
`src/Oxygen/Graphics/Direct3D12/Shaders/Vortex`. Keep unit/oracle tests next to
the owning module; use the existing native fixture and capture tools for GPU tests.

| Slice    | Start in these files                                                                                                                                                                                 | First observable check                                                                        |
| -------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- |
| 2        | `Core/Types/PostProcess.h`, `Vortex/PostProcess/Passes/ExposurePass.cpp`, `Vortex/Test/PostProcessService_test.cpp` under `src/Oxygen/`                                                              | EV14 reaches the tonemap constant unchanged                                                   |
| 3        | `Vortex/PostProcess/Types/PostProcessConfig.h`, `PostProcess/Passes/ExposurePass.*`; shader `Services/PostProcess/Exposure.hlsl`                                                                     | Known two-bin distribution and hybrid trajectory                                              |
| 4        | `Vortex/CompositionView.h`, `Internal/ViewLifecycleService.*`, `PostProcess/PostProcessService.*`, `ExposurePass.*`                                                                                  | One source update and idempotent event generation                                             |
| 5        | `Vortex/Types/ExposureStateData.h`, `Types/ViewFrameBindings.h`, `SceneRenderer/SceneTextures.*`, `Stages/InitViews/InitViewsModule.cpp`; shader families in section 4.4                             | Opaque/emissive output invariant under a change of P                                          |
| 6        | `Scene/Environment/PostProcessVolume.h`, `Data/PakFormat_world.h`, `Data/PakFormatSerioLoaders.h`, cooker schemas, `Scripting/Bindings/Packs/Scene/SceneEnvironmentBindings.cpp`, DemoShell services | Current records round-trip; obsolete records are rejected                                     |
| 7        | `Vortex/Lighting/Internal/DeferredLightPacketBuilder.cpp`; shader `Services/Lighting/DeferredLightingCommon.hlsli` and `ForwardDirectLighting.hlsli`                                                 | Point flux and spot angular integral match independent values                                 |
| 8        | `Examples/LightBench/LightScene.*`, `MainModule.*`, `LightBenchPanel.*`; existing native reference fixtures                                                                                          | Calibrated reference resets correctly and passes a focused independent check                  |
| 9A-E, 10 | Existing LightBench/MultiView controls and native tests                                                                                                                                              | Useful presets/controls and retained renderer requirements covered without new infrastructure |

Unqualified paths in the table's engine rows are under `src/Oxygen/`; shortened
paths after a `Vortex/` entry stay within that module. Avoid creating duplicate
helpers under LightBench for renderer-owned behavior.

## Design and source references

Update the owning documents in each implementation slice. Keep a single
mathematical specification in the PBR document, one exposure-runtime design
in the PostProcessService LLD, and one calibration-demo specification for LightBench.

- [PLAN.md](../../PLAN.md), [Milestone roadmap](../../PLAN.md),
  and relevant indexes: execution order, slice results and remaining failures.
- [Exposure reference companion](../../lld/exposure.md): source
  pointers and deliberate Oxygen choices; keep algorithms in this plan/their LLDs.
- [Diagnostics LLD](../../lld/diagnostics-service.md): retain existing diagnostics; optional future measurement infrastructure is outside this package.
- `ARCHITECTURE.md`, `init-views.md`, shader/environment/lighting LLDs: update
  affected ownership, domains and publications.
- [Existing LightBench specification](../../../renderer-core/lightbench.md):
  reference presets, reset/save/load and acceptance. Create and maintain
  `Examples/LightBench/README.md` from EX08 onward.
- `Examples/MultiView/README.md` and multiview validation tools: visual exposure
  scenarios, per-view comparisons and reproducible interaction sequences.
- Repository-root editor authoring/API documents: new authored fields and
  versioned round-trip behavior; no general UI redesign.
- Reports/captures: `out/build-ninja/analysis/vortex/exposure-lightbench/`.

UE5.7 source reference root: `F:/Epic Games/UE_5.7/Engine`.

| Reference                                                                             | Use                                                                              |
| ------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------- |
| `Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp`            | Target/current exposure, force-target conditions, normalization and pre-exposure |
| `Source/Runtime/Renderer/Private/SceneRendering.cpp::ShouldUpdateEyeAdaptationBuffer` | Owner/borrower update boundary                                                   |
| `Shaders/Private/PostProcessHistogramCommon.ush`                                      | Log histogram reduction and hybrid EV response                                   |
| `Shaders/Private/Histogram.usf`                                                       | Interpolated bins and black weighting                                            |
| `Shaders/Private/PostProcessTonemap.usf`                                              | Pre-exposure removal and final gain application                                  |
| `Source/Runtime/Engine/Private/Components/LocalLightComponent.cpp`                    | Explicit distance and lumen/candela conventions                                  |

Oxygen retains its calibration key, analytic metering profiles and
previous-frame sharing. It uses a compact curve-key buffer, deterministic bounded
sampling, exact hybrid integration, and a targeted FP32 bootstrap. These choices
serve the specified behavior without adopting UE's legacy compatibility paths.

| Plan section 8 requirements                                                                                                                      | Tracking items                                                                                     |
| ------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------- |
| Slice 5: early P; full 4.4 migration; S/P; formats; range/recovery; scene lifecycle; MultiView; gate                                             | EX05-01; EX05-04–09/18; EX05-03; EX05-02/17; EX05-10–16/19–21; EX05-22–25; EX05-26–30; EX05-GATE   |
| Slice 5.1: native profiling; workloads/FP32-only baseline; format decision; precision policy; SceneColor ownership; final acceptance             | EX051-01–07; EX051-09/10/10A/11; EX051-12–14; EX051-GATE; EX051-08 merged into 09                  |
| Slice 5.2: residual fix agreement; bounded fixes; affected validation; closure; reused decomposition                                             | EX052-02; EX052-04; EX052-10/12/GATE; EX052-05/06 already validated; other original IDs superseded |
| Slice 6: camera persistence; authoring surfaces; full round-trip; mask runtime; DemoShell; activation policy; gate                               | EX06-01; EX06-02–05; EX06-06; EX06-07; EX06-08; EX06-09; EX06-GATE                                 |
| Slice 7: physical/dual-directional calibration; baseline; culling; shaders/resources/shadows; native/editor correctness; performance and closure | EX07-01–06; EX07-07; EX07-08; EX07-09–11; EX07-12; EX07-13–14/GATE                                 |
| Slice 8: calibrated reference, local reset/save/load, native check, acceptance                                                                   | EX08-07–11/GATE; EX09-02–03/10–11/14; EX10-06; EX08-01–06 and EX09-01 removed                      |
| Slice 8.1: console commands and async outcomes                                                                                                   | EX081-01–04/GATE                                                                                   |
| Slice 8.2: reactivated automated widget suite after EX09                                                                                         | EX082-01–04/GATE validated; Debug/Release automation and build isolation                           |
| Slice 9A-E: point/spot controls; fixed/adaptation; native lifecycle/HDR; existing MultiView                                                      | EX09-04–15/GATE; EX10-03–05                                                                        |
| Slice 10: concise final checks, evidence, acceptance and docs                                                                                    | EX10-03–08/GATE; EX10-01–02 removed                                                                |

## Supporting records

- [validation](validation.md)
- [EX01](EX01/README.md)
- [EX02](EX02/README.md)
- [EX03](EX03/README.md)
- [EX04](EX04/README.md)
- [EX05](EX05/README.md)
- [EX05.1](EX05.1/README.md)
- [EX05.2](EX05.2/README.md)
- [EX06](EX06/README.md)
- [EX07](EX07/README.md)
- [EX08](EX08/README.md)
- [EX08.1](EX08.1/README.md)
- [EX08.2](EX08.2/README.md)
- [EX09](EX09/README.md)
- [EX10](EX10/README.md)
