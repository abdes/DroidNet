# EX07C — Correctness repair

Status: `validated`

| Field     | Summary                                                                      |
| --------- | ---------------------------------------------------------------------------- |
| Outcome   | Lighting/transport/importer correctness and conventional-shadow integration. |
| Remaining | None in the recorded scope.                                                  |
| Evidence  | [Validation record](validation.md)                                           |

[Roadmap](../../../../PLAN.md) · [Design index](../../../../lld/README.md)

## Conventional-shadow integration

The conventional-shadow prerequisite consists of the following five
steps below, including the related selection, publication, lifetime, diagnostics,
documentation and focused-test changes needed by each step. Its implementation
also supplies portions of D/E; stage labels do not create a second implementation.

| Implementation step | Completed work                                                                                                                                     | Automated acceptance                                                                                               | EX07 items credited |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | ------------------- |
| 1                   | Consistent eligibility, conservative light relevance, per-face/spot/cascade caster lists and off-screen caster preservation                        | **Complete** — culling, coverage and scene-prep tests                                                              | 08, 11              |
| 2                   | D32 targets, per-light buckets, Nexus ownership, stable layers, bounded growth and deferred retirement including empty local selections            | **Complete** — native allocator textures/byte charges, ownership and removal tests                                 | 10, 11              |
| 3                   | Persistent local depths, per-caster geometry-content/material invalidation, entering/leaving membership and successful-submission/fence dependency | **Complete** — cache isolation, hidden-light reload, unavailable revision and submission/fence tests               | 10, 11              |
| 4                   | GPU spatial lists, bounded demand-based compact capacity, complete-list fallback and per-view/frame-slot ownership                                 | **Complete** — 4K/1,024-light sharing, native boundary, fallback and demand-growth tests                           | 08, 09, 13          |
| 5                   | Projected resolution, authored ceiling, hysteresis/fading and cache invalidation on bucket changes                                                 | **Complete** — orthographic invariance and warmed point/spot caches through 1024 -> 512 -> 1024, redraw then reuse | 11, 13              |

- [x] All five implementation steps and automated acceptance checks.
- [x] Follow-up defects: geometry hot reload with unchanged handles/SRVs;
      last-local-light ownership reconciliation before per-view rendering.
- [x] Native D32 allocation acceptance uses real allocator-produced chunks and
      verifies resource descriptors, byte charges, backing reuse and retirement.
- [x] Warmed point/spot cache resolution transitions verify nonempty depth
      rendering followed by unchanged-frame reuse in both directions.
- [x] Manual visual acceptance of the conventional-shadow changes and quality
      policy — approved on 2026-09-24 ("I visually approved.").

Follow-up validation (2026-09-24): **75/75 in Release and 75/75 in Debug**:
`Oxygen.Vortex.GeometryUploader.Tests` (30), `Oxygen.Vortex.ShadowService.Tests`
(30), `Oxygen.Vortex.DrawMetadataEmitter.Tests` (14), and
`LightingGpuAbiTest.NativeShadowAllocationRequirements` (1). RenderScene builds
in Release. The native allocator test creates six D32 allocations totaling
306,184,192 bytes (292 MiB); backend allocation sizes match budget charges, and
retirement restores the starting budget. The follow-up fixes and previously
missing automated checks are complete. The preceding broader suite and static
Tracy results below retain their original measurement scope.

Conventional repair validation (2026-09-24): 377 passing cases across the affected
Release suites and focused Debug shadow/lighting/readback suites. Tests cover
per-light cache isolation, entering/leaving casters, producer-fence waits,
submission rejection, Nexus slot recycling, membership changes, orthographic
quality invariance, 4K/1,024-light compact allocation across views/frame slots,
GPU boundary contributors, fallback and demand growth. The existing native
allocation and image suites also pass. No new test framework was introduced.

Pre-follow-up static RTX 3080 Tracy comparison, 2560x1400 with the original New Sponza
camera/settings and `--fps 0 --vsync false`: mean frame interval 168.99 -> 23.35 ms
(5.92 -> 42.82 FPS), p95 170.33 -> 23.56 ms, shadow depths 152.71 -> 3.80 ms.
The steady interval contains no local depth redraws. Deferred lighting remains
9.21 ms, versus 7.64 ms in the baseline; this repair does not close that residual
optimization. Shadow-enabled Instancing measured 43.84 FPS / 0.237 ms shadow
depths. Captures followed implementation and focused tests. Manual visual approval was
received on 2026-09-24, closing the remaining acceptance item. These results close the
bounded static conventional-shadow repair, not the wider EX07 qualification.

**C final follow-up closed (2026-09-25):** both omitted caller targets build in
Release and Debug. EnvironmentComponents passes 5/5 in each configuration; the
Release exposure I02 scenario executes all 16 events, including both `EditLight`
intensity changes, and passes its existing acceptance gates. Importer validation
found and repaired a repeat-run event-loop work-guard defect (`3fb0a8b17`); all
374 importer cases pass per configuration. **759 fresh cases pass with no skips.**
Current Sponza has 72 BC7 textures with full 13-level mip chains and unchanged
accepted scene/container hashes. The stale uncompressed-recook note is resolved.
The [durable C closure report](validation.md) preserves fresh and
credited evidence, including the rejected short-window exposure attempt.

| Area                            | Current behavior                                                                                                                                                                                                                                                                                                          |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Light data (12)                 | Scene v7 carries every retained shadow/CSM/atmosphere field through native, script, editor, live Interop and source/cook/load. Removed attenuation/decay and duplicate sun controls are rejected. Explicit glTF ranges survive import; omitted ranges use the configurable 4,096 m fallback.                              |
| Atomic edits (12)               | Whole candidates are validated before mutation. Native value edits preserve component identity without heap replacement; failed editor edits preserve source, dirty state and history. Hydration rejects conflicting stored assignments before replacing a scene.                                                         |
| Directional/atmosphere (01, 12) | Explicit None/Primary/Secondary ownership includes hidden/inactive lights. Shown children survive hidden parents. Visibility/hierarchy edits invalidate resolution, and direct/atmosphere paths share Core photometry and EV conversion.                                                                                  |
| Receive Shadows (11, 12)        | Per-instance metadata and forward/GBuffer consumers gate conventional and contact visibility without changing illumination or caster eligibility.                                                                                                                                                                         |
| Contact Shadows (11, 12)        | Conditional, budgeted camera-space D32 caster depth uses a retained bindless texture SRV and the shared 0.25 m/16-sample trace. Rendered tests verify occlusion, caster exclusion and receiver bypass in both shading paths.                                                                                              |
| View failure (04, 06, 10, 14)   | Owned per-view/frame outcomes isolate failed panes, present an error tile and expose diagnostics. GPU lighting failures hold exposure and invalidate radiance history. Capture acceptance requires completed, matching lighting status, including required auxiliary inputs; recovery and last-light removal are covered. |

Validation: **542 native cases in Debug**, with the affected Release suites and
final contact/receiver/failure/ABI cases passing as well; **123 PakGen cases**;
**88 managed/editor/native-bridge cases** across authoring, source generation,
commands/undo, Interop and a running native engine's property observation. Release
and Debug engine SDKs are installed; the WorldEditor UI test project compiles.
D's workload baselines are closed; interactive visual acceptance is recorded in F,
while accepted E scaling results are recorded separately and F owns final combined acceptance. No new reference
framework or repeat BRDF campaign was introduced.

All 16 maintained example scenes and their sidecars were recooked and packaged
as 124 assets / 29 resources, with no packaging warnings or errors. Original
example generations are preserved in `out/analysis/ex07c-completion/content-before-v7`.
Corrected Sponza is a separate cooked source: `HDRI_SKY` remains a 200 cd point
light with a 4,096 m range. Its original renderer/performance baseline is retained.
Details and named proof artifacts: [durable C completion report](validation.md).

## Conventional-shadow implementation sequence

The following implementation sequence, automated acceptance checks and manual visual acceptance are **complete**. Visual approval was received on 2026-09-24.
Evidence is recorded
in [tracker section 3.4](../README.md#stages-and-ownership).
Completed EX07C integration uses these delivered implementations:

1. **Eligibility and spatial caster culling.** Use the same energy, range,
   participation and view-relevance decisions in allocation, setup, recording
   and publication. Cull world-space caster bounds per point face, spot
   projection and extruded directional cascade, retaining contributing
   off-screen casters. Publish empty eligible maps as fully lit and distinguish
   irrelevant lights from failed required allocations.
2. **Per-light allocation and D32.** Group local maps by resolution, retain
   stable scene/light owners through Nexus `FrameDrivenIndexReuse<ShadowSlotIndex>`,
   publish fixed surface/layer associations, reconcile empty local selections, grow
   only affected buckets and retire resources after their consumer fences.
   Migrate conventional textures, views, PSOs and clears to depth-only D32.
   Scene/custom stencil is unchanged.
3. **Cross-frame local depth caching.** Reuse unchanged point/spot map contents
   with complete light-space coverage. Re-evaluate current caster membership so
   entering and leaving casters invalidate the affected map. Track light,
   projection, resolution and each relevant caster's geometry, transform and
   depth-affecting material/texture inputs. Geometry content revisions invalidate
   unchanged handles/SRVs after hot reload, including while a light is out of view. Recorded
   contents become reusable only after successful submission; retain and honor
   the producing queue/fence dependency. Failed/discarded work is never valid
   cache content. Directional cascades remain view-dependent.
4. **Spatial light lists.** Build conservative per-view compact cell membership
   from the shared typed light selection. Reserve the complete-list sentinel for
   actual capacity fallback, publish truthful status and preserve references
   and buffers through their final consumers. Start compact storage from bounded
   occupancy and grow from completed GPU counts, sharing the aggregate budget
   across all active views and frame slots. Never silently truncate lights.
5. **Projected-benefit quality.** Select resolution buckets within the chosen
   quality profile and authored ceiling, with hysteresis and defined fading.
   Resolution changes invalidate cached depths. Keep intentional quality
   choices distinct from allocation failure. Orthographic quality uses projected
   size regardless of camera distance; the user confirms visual tuning.

Repair related correctness defects, duplicated decisions and superseded code
within the implementing step. These changes supply parts of C/D/E; later stages
reuse the implementation rather than creating another allocator, cache or culler.
Current status and remaining integration work live in
[tracker section 3.4](../README.md#stages-and-ownership).

Build affected targets and use existing focused unit/GPU tests for each step.
Use the shadow-enabled Instancing scene for incremental static-scene runtime and
Tracy checks with `--fps 0 --vsync false`, followed by interactive camera/caster
visual checks. Run New Sponza only after all five steps and their unit tests are
complete, then make one comparison against the matching uncapped/VSync-off
baseline. Do not add a validation framework or automate an exhaustive visual
matrix. VSM is excluded from this work.

## Supporting records

- [sponza analysis](sponza-analysis.md)
- [validation](validation.md)
- [evidence](evidence/README.md)
- [Captured evidence](evidence/README.md)
