# New Sponza regression analysis and UE5.7 comparison

2026-09-24. **Shutdown repair verified; shadow correction validated;
performance remains open.** Correctness fixes and this analysis were committed
in `137b681b2`. The [EX07D baseline register](EX07D-baseline-report.md) now records
the current benchmark and application comparison points. Timings below retain
their original diagnostic scope; they are not substituted for that register.

This follows the user's application regression report and request for analysis
before further performance changes. The many-light benchmark did not cover
window-close lifetime or the newly recooked scene's long-range shadow receivers.
Its earlier results do not establish application regression clearance.

## Findings and evidence

| Issue                          | Finding                                                                                                                                 | Current disposition                                                                                                                                                                                        |
| ------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Device failure on window close | D3D12 reports final release of `DemoRuntime.CompositeColor.1000` while the graphics queue still references it.                          | DemoShell now retires both runtime framebuffers through the existing fenced reclaimer on view removal, replacement and cleanup. The reproduced window-close case now exits 0 with the debug layer enabled. |
| Missing local shadows          | Source and cooked lights cast shadows. Receiver bias incorrectly used the shadow texel footprint at the far plane for nearby receivers. | Candidate scales the footprint to the actual receiver depth for point and spot lights. The failing long-range image test now passes in both rendering families.                                            |
| Approximately 15 FPS           | Reproduced without Tracy. GPU local-light evaluation and translucency dominate; static local shadow caching still works.                | No performance fix is claimed. Preserve current source semantics and investigate the measured GPU costs.                                                                                                   |

Evidence directory: [ex07-regressions-20260924](../../../out/analysis/ex07-regressions-20260924).
The original crash dump is retained by Windows at
`C:/Users/abdes/AppData/Local/CrashDumps/Oxygen.Examples.RenderScene.exe.47520.dmp`.
Its extracted D3D12 error and stack are in `shutdown-before/debug-message.txt`
and `dump-analysis.txt`. The failing run exited 2173; `shutdown-fix` exited 0
after the same window-close action, with no device-removal or corruption report.
No per-frame queue flush was added. User settings and `imgui.ini` were restored.

## Source data and correct import behavior

The inspected source is
`F:/projects/main_sponza/NewSponza_Main_glTF_003.gltf`. It has 23 point lights and
one directional light. Neither the lights nor their owning nodes specify shadow
overrides. The current cooked scene has all 24 `casts_shadows` flags enabled.
Its point-light intensity conversion is correct: 200 cd becomes approximately
2,513.274 lm, with source RGB tint retained. The source directional intensity is
100,000 lux. Runtime preview-sun settings can subsequently override that sun.

`KHR_lights_punctual` specifies point/spot candela, directional lux, and unbounded
inverse-square influence when `range` is absent. It does not define a
`casts_shadows` property. Shadow defaults therefore belong to the importing
engine, while an explicitly supported custom override must retain its meaning.
[Khronos specification](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md).

Oxygen already implements the appropriate policy:

- Preserve explicit positive ranges, photometry, tint and source transforms.
- For omitted ranges, use the configurable **4,096 m** approximation approved
  earlier, not the unrelated 10 m native creation default.
- Default imported lights to casting shadows; honor `extras.oxygen` overrides,
  with node overrides taking precedence over light overrides.
- Interpret the actual source type. For example, the source node named
  `HDRI_SKY` references a point light; its name does not authorize conversion
  into image-based lighting or exclusion from rendering.

No importer behavior or cooked scene was changed to conceal this regression.
The range import test now also asserts that point and spot lights without
overrides retain shadow casting. Existing override and photometry tests pass.

UE5.7.4 provides useful comparison, rather than a universal import policy:
`GLTFNode.h:206` initializes an omitted light range to `1e20`;
`ExtensionsHandler.cpp:782` reads an authored range when present; the point-light
path in `InterchangeGltfTranslator.cpp:823` transfers it with unit conversion.
`LightComponent.cpp:435` defaults `CastShadows` to true. These sources do not
support silently restoring a short radius or disabling Sponza's shadows.

## The earlier performance baseline is different content

The accepted earlier capture used the same light count but **10 m point-light
ranges**. Current recooked content uses **4,096 m**. The scene descriptor hashes
also differ:

- Earlier: `45b4378c92eaa303a3d02f79add654fe67bfc14b9bc4b218f8523fb94de91e53`.
- Current: `3665446b42e4e8b7c8cf770c70bc05283a2726dc82c29c462edb15353eb04397`.

A much larger radius increases screen coverage, receiver/light overlap and
translucent lighting work. The historical 42.82 FPS result remains valid for its
recorded content; comparing it directly with the recooked scene does not isolate
a renderer-code regression. Range is a verified material difference, not a claim
that every other recook difference has been experimentally excluded.

## Measured performance

The diagnostic Tracy run uses Release, 2560x1400, uncapped presentation,
VSync off, debug layer off, the current cooked scene and saved user camera/settings.
It includes the lifetime and receiver-footprint fixes. The capture spans 55 seconds;
analysis uses 413 complete GPU frames between 20 and 50 seconds, after loading.
All 23 point-light draws remain present.

| Measurement                  |                  Mean |
| ---------------------------- | --------------------: |
| Complete GPU frame interval  | 72.406 ms / 13.81 FPS |
| Frame p95                    |             76.834 ms |
| Deferred lighting stage      |             45.568 ms |
| Point-light draws, total     |             38.609 ms |
| Translucency stage           |             14.260 ms |
| Shadow depths                |              4.092 ms |
| Base pass                    |              3.301 ms |
| Spatial light-grid build     |              0.471 ms |
| CPU deferred-light recording |              0.109 ms |

Parent/child scope timings overlap and must not be added. Point-shadow depth
maps are retained in the measured steady interval; the remaining shadow-depth
draw is the directional pass. CPU fence waits are symptoms of GPU completion,
not evidence that CPU shading preparation consumes those intervals.

A separate ordinary Ninja Release run, with Tracy disabled, still measures
**65.477 ms / 15.27 FPS** from 149 steady scene-completion intervals at frames
450–599. This secondary measurement uses 1 ms log timestamps with informational
logging enabled; it is not a GPU stage measurement or a precision microbenchmark.
It confirms that disabling Tracy does not resolve this application's low FPS.
Early empty/loading frames are excluded. The earlier debug-layer reproduction
is not used as a matched performance comparison.

The trace identifies per-pixel lighting as the primary target. It does **not**
yet distinguish shadow-filter texture cost, GBuffer/bindless fetch cost and BRDF
execution/register pressure inside each point draw. No percentage of that draw
is attributed to a particular shader function without further isolation.

Evidence: `sponza-profile/native.tracy`, exported GPU/CPU CSVs,
`sponza-profile-analysis.json`, `native-performance/frame-log-summary.json`,
run arguments/settings, cooked-light inventory and `current-identities.json`.

## Shadow correction: geometric derivation and scope

For a perspective shadow projection with half-angle theta, resolution N and
receiver axial depth z, a texel spans `2*z*tan(theta)/N` world units. The published
record stores the footprint at far distance R, so the correct receiver footprint
is `published_footprint * z/R`. For a cube face, theta is 45 degrees and z is the
largest absolute component of the light-relative receiver position. For a spot,
z is the unbiased light-space clip w.

Previously that far-plane footprint was used unchanged. At R=4,096 m and
N=1,024, it is 8 m for a point face. The existing normal and light-direction
weights then displace the receiver by metres, potentially across walls or past
the light. The candidate changes only the footprint's depth dependence. It
retains authored normal bias, existing filter weights, full light range, map
resolution, caster selection, depth encoding, map cache and shader ABI.

This is a general projective-geometry correction, not a Sponza-specific tuning
rule. The regression puts the light and complete caster outside the camera
while their shadow falls on a visible receiver. It now tests point and spot,
3 m and 4,096 m ranges, forward and deferred, and caster off/on/off restoration.
The original long-range case failed in both families before the correction.

## Comparison with the local UE5.7.4 implementation

Local source root: `F:/Epic Games/UE_5.7/Engine`, version 5.7.4, CL 51494982.

| Concern                       | UE5.7.4 source behavior                                                                                                                                                                           | Oxygen and consequence                                                                                                                                                                                                                                                             |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Light visibility              | `SceneVisibility.cpp:5630-5658` tests light bounds against the frustum, with additional configurable quality/distance policies.                                                                   | Conservative influence-volume culling remains. No energy threshold, origin-only rejection or shadow-caster removal is introduced.                                                                                                                                                  |
| Batched deferred eligibility  | `LightRendering.cpp:1380-1400` excludes unsupported light features. `DeferredShadingRenderer.cpp:2257-2259` requires enabled VSM one-pass projection for shadowed lights in this clustered route. | Moving these conventionally shadowed lights into a clustered path is not a drop-in UE-equivalent fix. VSM is outside the current conventional-shadow scope.                                                                                                                        |
| Conventional shadow filtering | `ShadowProjectionCommon.ush:153-237` uses cube comparison sampling, with quality-dependent tap counts.                                                                                            | Oxygen uses nine manual depth loads/comparisons from an array face. Hardware filtering is a valid investigation target, but a different kernel changes quality and needs explicit qualification.                                                                                   |
| Point depth and bias          | That UE cube path compares projected z/w and scales its comparison bias by clip w. `ShadowRendering.cpp:83-92,1876-1881` derives point bias from a 0.02 default and resolution/user settings.     | Oxygen uses linear reversed depth, receiver offsets, and a different depth-bias derivation; its constant named `kUePointLightShadowDepthBias` is 3.0. This is not quantitative UE point-bias parity. Copying 0.02 into a different depth representation would also be unjustified. |
| Spot depth                    | `ShadowDepthPixelShader.usf:97-103` has a perspective-correct depth path with a linear depth conversion.                                                                                          | Do not generalize UE's cube encoding to every light type. Producer/consumer depth and bias must stay coherent.                                                                                                                                                                     |
| Shader specialization         | `LightRendering.cpp:905-936` defines feature/source-shape permutations for deferred lighting.                                                                                                     | Oxygen has point/spot entry points but retains generic emitter/shadow work. Specialization and uniform-data access deserve investigation before a new lighting architecture.                                                                                                       |

UE's default distance unit is centimetres; Oxygen's lighting contract uses
metres. Numeric constants also require a unit and depth-representation audit,
not just matching names. [Epic's unit documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/units-of-measurement-in-unreal-engine).

The candidate receiver-footprint correction is appropriate for Oxygen's current
projection contract. The comparison does **not** justify calling its complete
point-shadow filtering/bias pipeline UE-equivalent or performance-qualified.
Current conventional targets are D32/R32; this repair does not switch formats.

## Recommendation and next measured work

1. Retain the fenced-target lifetime repair and the general receiver-footprint
   correction, subject to user review. Keep the approved import semantics and
   full shadow contribution. The importer is not the missing-shadow defect.
2. Continue on the existing conventional deferred path. Start with controlled
   shader-cost isolation and compiled-shader inspection for the measured point
   pass: shadow filtering, material loads and BRDF work. Keep camera, content,
   quality and instrumentation matched. Any diagnostic feature suppression is
   an experiment only, never an accepted speedup or shipping workaround.
3. Prototype the lowest-risk proven opportunity first: feature specialization
   and uniform data reuse without changing numerical output. For example, the
   spot entry already specializes its known source kind while the point entry
   leaves it dynamic. Its actual benefit must exceed capture noise before it is
   retained as a performance fix.
4. If filtering dominates, design comparison-sampler support with an explicit
   filter/quality contract. Validate contact, grazing/self-shadow behavior,
   cube seams, both source types, short/long ranges, all quality tiers and both
   rendering families. Separately audit nonzero authored bias against the full
   depth representation and unit contract. Do not patch isolated UE constants
   into the current linear-depth implementation.
5. Reassess larger changes only after those measurements. A clustered/VSM or
   shadow-mask architecture changes bindings, ownership, quality and resource
   cost; it requires a concrete design review. The trace does not currently
   justify that expansion or promise restoration of the old content's FPS.

This is the production-grade direction: conserve source meaning and visible
contributions, repair demonstrable lifetime/geometry defects, then select
performance changes by matched timing and quality evidence. It is not a claim
that the remaining performance work has been completed.

## Validation performed

- Debug and Release RenderScene builds succeed.
- All 17 native lighting/material/grid/shadow tests pass in each configuration,
  including the expanded off-screen point/spot, short/long-range regression.
- Three focused glTF range, photometry and shadow-override tests pass in each
  configuration. No content was recooked for these repairs.
- Release and Debug shader archives rebuild successfully, 218 modules each.
- Actual New Sponza window close succeeds with the D3D12 debug layer; the traced
  close and the non-Tracy frame-limit exit also return 0.
- Final visual capture shows restored local cast shadows. The user still owns
  final interactive visual acceptance; performance remains explicitly open.
