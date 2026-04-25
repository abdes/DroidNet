# Vortex Implementation Status

Status: `active tracking ledger`

This file is the current-state ledger for Vortex. It is intentionally separate
from [PLAN.md](./PLAN.md): the plan defines what should happen next, while this
file records what is known about implementation progress, validation evidence,
and remaining gaps.

## 1. How To Use This File

Before starting Vortex work:

1. Read the `Current Next Step` section.
2. Find the target milestone in the `Milestone Ledger`.
3. Check the `Open Gaps` section for blockers.
4. Define fresh verification before claiming closure.
5. Update this file with changed files, commands run, results, UE5.7 references
   checked, and remaining gaps.

This file does not treat implementation presence as completion. Many Vortex
systems have real code but still need fresh validation and UE5.7 parity proof.

## 2. Status Vocabulary

| Status | Meaning |
| --- | --- |
| `validated` | Implementation exists, docs/status are current, and fresh validation evidence is recorded here. |
| `landed_needs_validation` | Substantial code exists, but this ledger does not contain fresh closure proof. |
| `in_progress` | Implementation exists but known gaps remain, or scope is actively being corrected. |
| `planned` | Scope is defined; implementation has not started for this milestone. |
| `blocked` | Required design, dependency, or proof surface is missing. |
| `future` | Explicitly deferred beyond the production-complete desktop deferred baseline. |

## 3. Current Next Step

**Next milestone:** `VTX-M04D — Environment / Fog Parity Closure`

**Why:** The environment service is active, but fog parity is not satisfactory
and must not be considered complete. This parent milestone owns the full
environment/fog closure required before Async migration and Phase 5 work.

**First implementation work package:** `VTX-M04D.1 — Environment Publication
And Sky/Fog Contract Truth`

**Detailed workstream plan:**
[plan/VTX-M04D.1-environment-publication-truth.md](./plan/VTX-M04D.1-environment-publication-truth.md)

- preserve stable sky/atmosphere and below-horizon behavior
- make SkyLight/IBL publications truthful
- expose Stage 14 environment execution state at the SceneRenderer boundary
- tighten environment binding/static/view/product publication tests
- record validation evidence before any closure claim

## 4. Milestone Ledger

| ID | Milestone | Status | Implementation Evidence | Missing To Close |
| --- | --- | --- | --- | --- |
| VTX-M00 | Planning and status truth surface | `validated` | `PLAN.md` has been rewritten as a milestone-first plan; this ledger exists; restricted doc scans and `git diff --check` passed on 2026-04-25. | No open planning-status gap for this docs-only milestone. |
| VTX-M01 | Renderer Core and SceneRenderer baseline | `landed_needs_validation` | Vortex module, Renderer Core, publication, upload/resource substrate, SceneRenderer shell, SceneTextures, non-runtime facades, resolve/cleanup, and related tests are present in source. | Fresh build/test evidence and any required parity proof are not recorded in this ledger. |
| VTX-M02 | Deferred core visual path | `landed_needs_validation` | InitViews, depth prepass, generic Screen HZB, base pass/GBuffer/velocity, Stage 10 publication, deferred lighting, shader families, debug views, and focused tests/tools are present. | Fresh build, ShaderBake/catalog validation, tests, and capture/analyzer proof are not recorded here. |
| VTX-M03 | Migration-critical non-environment services | `landed_needs_validation` | LightingService, ShadowService directional baseline, PostProcessService, Stage 8/12/22 routing, and focused tests are present. | Service-specific fresh validation, RenderDoc proof, and residual parity review are not recorded here. |
| VTX-M04D | Environment / fog parity closure | `in_progress` | Environment service and many atmosphere/sky/fog surfaces exist. | Parent milestone closes publication truth, height fog, local fog, volumetric fog, and runtime proof. |
| VTX-M04D.1 | Environment publication and sky/fog contract truth | `in_progress` | EnvironmentLightingService and many atmosphere/sky/fog/local-fog passes exist; below-horizon sky invariants are captured in the LLD. | Truthful SkyLight/IBL publication, model-slot truth, Stage 14 renderer-boundary observability, and focused tests. |
| VTX-M04D.2 | UE5.7 exponential height fog parity | `in_progress` | Height fog model/pass code exists. | UE5.7-grade authored parameters, `FogRendering` / `HeightFogCommon` algorithm parity, shader proof, coupling proof. |
| VTX-M04D.3 | UE5.7 local fog volume parity | `in_progress` | Local fog state, Stage 14 tiled culling, Stage 15 compose, shaders, and tests exist. | Full UE5.7 parity for authoring, packing/sorting/capping, HZB use, analytic integral, splat/compose, sky-depth exclusion. |
| VTX-M04D.4 | UE5.7 volumetric fog parity | `in_progress` | Volumetric fog model and publication seams exist. | Runtime froxel/grid passes, media/light injection, history/reprojection, integrated-light-scattering output, tests/capture proof. |
| VTX-M04D.5 | Environment runtime proof and Async preparation | `planned` | Runtime examples and Vortex proof tooling exist. | One runtime path exercising atmosphere, height fog, local fog, volumetric fog, and SkyLight together. |
| VTX-M04E | Async migration parity gate | `planned` | Async example and analyzer/proof tooling exist. | Vortex-backed Async runtime proof with capture/analyzer/assert evidence and no compatibility clutter. |
| VTX-M04F | Single-view composition and presentation closeout | `planned` | ResolveSceneColor, PostRenderCleanup, and post-process routing exist. | End-to-end composition/presentation validation after migration services are proven. |
| VTX-M05A | Diagnostics product service | `planned` | Diagnostics substrate pieces and design exist. | Concrete DiagnosticsService product surface, debug modes, overlays/panels/timeline/debug bindings. |
| VTX-M05B | Occlusion consumer closeout | `planned` | Generic Screen HZB implementation and bindings exist. | Full occlusion query/consumer policy, visibility classifications, batching, temporal handoff. |
| VTX-M05C | Translucency stage | `planned` | Directory placeholders and LLD exist. | Stage 18 implementation consuming lighting/shadow/environment publications. |
| VTX-M05D | Local-light conventional shadow expansion | `planned` | Directional ShadowService baseline exists; local-light LLD exists. | Spot-light shadows, point-light strategy, publication/capture proof. |
| VTX-M06A | Multi-view proof closeout | `planned` | CompositionView/ViewLifecycle/CompositionPlanner substrate exists. | Heterogeneous per-view runtime proof, PiP/multi-surface routing, per-view product isolation. |
| VTX-M06B | Offscreen proof closeout | `planned` | `ForOffscreenScene` API and presets exist. | Focused tests and runtime/capture validation for offscreen deferred/forward scenarios. |
| VTX-M06C | Feature-gated runtime variants | `planned` | Capability vocabulary and many services exist. | Depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only validation. |
| VTX-M07 | Production readiness and legacy retirement | `planned` | Not applicable. | All required examples/tests ported, legacy renderer removal plan and proof. |
| VTX-FUTURE | Reserved post-baseline families | `future` | Some placeholder directories or shader inventory may exist. | Geometry virtualization, material composition, indirect lighting/GI/reflections, VSM, clouds, heterogeneous volumes, water, hair, distortion, light shafts. |

## 5. Achievement Ledger

These entries are achievements visible from source/doc inspection. They are not
fresh closure claims unless the status is `validated`.

| Area | Known Achievement | Status | Notes |
| --- | --- | --- | --- |
| Build/module scaffold | Vortex module and CMake target exist. | `landed_needs_validation` | Older plan text that said the scaffold was not wired was stale and has been removed. |
| Renderer Core substrate | Renderer, render context materialization, upload/resources, publication, composition queue, and facade patterns exist. | `landed_needs_validation` | Needs fresh build/test record before validated status. |
| SceneTextures | Concrete scene texture product, setup mode, bindings, extracts, and tests exist. | `landed_needs_validation` | Future GBufferE/F remain reserved. |
| SceneRenderer shell | SceneRenderer construction, stage ordering, dispatch skeleton, resolve, cleanup, and tests exist. | `landed_needs_validation` | Stage 14 wording must remain aligned with active environment ownership. |
| Deferred core | InitViews, depth prepass, base pass, velocity, GBuffer publication, deferred lighting, and debug visualization have implementation surfaces. | `landed_needs_validation` | Needs fresh ShaderBake/test/capture evidence before parity closure. |
| LightingService | Service exists and Stage 12 routes through it. | `landed_needs_validation` | Validate Stage 6/12 publication and deferred-light ownership. |
| ShadowService | Directional conventional shadow service exists. | `landed_needs_validation` | Local-light shadows remain future VTX-M05D work. |
| PostProcessService | Exposure/bloom/tonemap service exists and Stage 22 is routed. | `landed_needs_validation` | Exposure parity improvement remains a separate tracked risk in the post-process LLD set. |
| Environment sky/atmosphere | Atmosphere state, LUTs, sky-view, aerial perspective, sky compose, dual light state, and stable below-horizon design invariants exist. | `in_progress` | Preserve this work; do not reopen below-horizon remediation as a separate doc. |
| Environment fog | Height fog, local fog, and volumetric fog surfaces exist in varying depth. | `in_progress` | User explicitly rejected current fog as satisfactory; no fog family closure claim is allowed yet. |
| Generic Screen HZB | Stage 5 Screen HZB module, shader, publications, and tests exist. | `landed_needs_validation` | Environment local fog depends on generic HZB publication, not full occlusion closure. |
| Non-runtime facades | Single-pass and render-graph harnesses have APIs and tests; offscreen API exists. | `landed_needs_validation` | Offscreen proof is still a separate milestone. |
| Runtime proof tooling | Vortex runtime/capture/analyzer tooling exists. | `landed_needs_validation` | Tool availability is not proof; wrappers/analyzers/assertions must pass per milestone. |

## 6. Open Gaps

| Gap | Blocks | Required Resolution |
| --- | --- | --- |
| No fresh validation recorded in this new status ledger for existing landed systems. | Any `validated` status for VTX-M01 through VTX-M03. | Run focused builds/tests and record exact command output/results. |
| SkyLight/IBL publication can appear revisioned without real resource publication. | Environment publication truth and downstream shading/proof. | Implement or explicitly invalidate resource slots; test enabled/unavailable/disabled states. |
| Environment model/product slots can remain invalid while authored state exists. | Downstream diagnostics, translucency, runtime proof. | Make each slot truthful and documented. |
| Stage 14 local-fog state is service-local. | Diagnostics and renderer-level validation. | Publish/reflect Stage 14 state through SceneRenderer. |
| Height fog is not UE5.7 parity-closed. | Environment parity and Async migration proof. | Implement parity-grade authored parameters, algorithms, shaders, tests, and capture proof. |
| Local fog is not UE5.7 parity-closed. | Environment parity and no-volumetrics variants. | Complete authoring/runtime/culling/splat/sky-exclusion contract. |
| Volumetric fog has authored/model seams but no accepted runtime parity. | Environment parity and no-volumetrics variants. | Implement froxel/injection/history/integrated-scattering pipeline. |
| Diagnostics product surface is not closed. | M05A and later observability. | Land concrete service surface without confusing proof tooling with diagnostics. |
| Translucency stage is not implemented. | M05C and later multi-view/offscreen feature matrices. | Implement Stage 18 forward-lit translucent rendering. |
| Full occlusion consumer policy is not closed. | M05B and advanced visibility claims. | Build occlusion/query/consumer policy over generic HZB. |

## 7. Environment Detailed Tracker

| Environment Feature | Current State | Target Milestone | Closure Evidence Required |
| --- | --- | --- | --- |
| Below-horizon sky behavior | Stable design invariants captured in `environment-service.md`; separate remediation doc removed. | VTX-M04D.1 preserve | Regression tests/capture proof that raw light directions, LUT domains, horizon seam behavior, and sky-depth rules remain stable. |
| Atmosphere LUT family | Implementation exists for transmittance, multi-scattering, distant sky light, sky view, and camera aerial perspective. | VTX-M04D.1 preserve | Focused environment tests plus capture/resource proof where applicable. |
| SkyLight / IBL | Implementation surface exists; publication truth remains open. | VTX-M04D.1 | Real resource publication or explicit invalid state, tests for all states. |
| Height fog | Implementation surface exists; parity rejected as incomplete. | VTX-M04D.2 | UE5.7 source/shader mapping, authored-parameter tests, shader/capture proof. |
| Local fog volumes | Stage 14 and Stage 15 code exists; parity rejected as incomplete. | VTX-M04D.3 | UE5.7 local fog mapping, HZB culling proof, splat/compose proof, sky-depth exclusion proof. |
| Volumetric fog | Model/publication seams exist; runtime parity missing. | VTX-M04D.4 | Froxel/injection/history/integrated-scattering implementation and proof. |
| Environment runtime proof | Tooling/examples exist but full environment proof is not closed. | VTX-M04D.5 | Repeatable runtime command plus analyzer/assertion evidence. |

## 8. Validation Evidence Log

Append new entries at the top.

| Date | Scope | Commands / Evidence | Result | Residual Gap |
| --- | --- | --- | --- | --- |
| 2026-04-25 | VTX-M04D.1 detailed planning | Added the detailed environment publication truth plan at [plan/VTX-M04D.1-environment-publication-truth.md](./plan/VTX-M04D.1-environment-publication-truth.md), the planning package index at [plan/README.md](./plan/README.md), and the reusable workflow at [plan/milestone-planning-workflow.md](./plan/milestone-planning-workflow.md). | `in_progress` planning handoff for VTX-M04D.1. | No implementation or runtime validation was run; VTX-M04D.1 remains open until code and proof land. |
| 2026-04-25 | Planning/status rewrite | Source/doc inspection; three read-only subagent reviews; restricted markdown scans for stale status links, next milestone visibility, environment/fog false-completion language, and milestone status consistency; `git diff --check` on edited docs. | `validated` for VTX-M00 docs-only planning/status work. | Existing implementation remains `landed_needs_validation` or `in_progress` until fresh build/test/runtime proof is recorded. |
