# Vortex Implementation Planning Package

This directory contains detailed implementation-preparation plans for Vortex
milestones. These plans sit between the milestone roadmap in
[../PLAN.md](../PLAN.md) and the subsystem LLDs under [../lld/](../lld/README.md).

Use this package when a milestone is ready to move from roadmap scope to an
implementation-preparation workstream.

## Documents

| Document | Purpose |
| --- | --- |
| [milestone-planning-workflow.md](milestone-planning-workflow.md) | Standard workflow for preparing any Vortex milestone for implementation. |
| [VTX-M04D.1-environment-publication-truth.md](VTX-M04D.1-environment-publication-truth.md) | Detailed implementation plan for the next environment work package. |
| [VTX-M04D.2-exponential-height-fog-parity.md](VTX-M04D.2-exponential-height-fog-parity.md) | Detailed implementation plan for UE5.7 exponential height fog parity. |
| [VTX-M04D.3-local-fog-volume-parity.md](VTX-M04D.3-local-fog-volume-parity.md) | Detailed implementation plan for UE5.7 local fog volume parity. |
| [VTX-M04D.4-directional-csm-blocker.md](VTX-M04D.4-directional-csm-blocker.md) | Corrective prerequisite plan for conventional directional CSM projection proof before VTX-M04D.4 shadowed-light injection. |
| [VTX-M04D.6-aerial-perspective-parity.md](VTX-M04D.6-aerial-perspective-parity.md) | Detailed remediation plan for UE5.7 aerial perspective parity proof. |
| [VTX-M05A-diagnostics-product-service.md](VTX-M05A-diagnostics-product-service.md) | Detailed implementation plan for the diagnostics product-service milestone and its runtime/tooling boundary. |
| [VTX-M05D-conventional-shadow-parity.md](VTX-M05D-conventional-shadow-parity.md) | Detailed implementation plan for CSM parity/stability remediation followed by local-light conventional shadow expansion. |
| [VTX-M06A-multi-view-proof-closeout.md](VTX-M06A-multi-view-proof-closeout.md) | Detailed implementation plan for multi-view proof closeout: per-view plans, state handles, serialized view-family execution, scene-texture leases, data-driven surface composition, auxiliary views, overlays, and runtime/capture proof. |
| [VTX-M06B-offscreen-proof-closeout.md](VTX-M06B-offscreen-proof-closeout.md) | Detailed implementation plan for offscreen proof closeout: `ForOffscreenScene` execution, deferred/forward coverage, offscreen product handoff, runtime/capture proof, and allocation-churn validation. |
| [VTX-M06C-feature-gated-runtime-variants.md](VTX-M06C-feature-gated-runtime-variants.md) | Detailed implementation plan for feature-gated runtime variants: depth-only, shadow-only, no-environment, no-shadowing, no-volumetrics, diagnostics-only, and runtime/capture proof. |
| [VTX-M07-production-readiness-legacy-retirement.md](VTX-M07-production-readiness-legacy-retirement.md) | Detailed implementation plan for production readiness and legacy retirement: seam inventory, stale demo/doc cleanup, demo refresh/testing, proof-suite consolidation, build-graph dependency audit, and closure evidence. |
| [VTX-M08-skybox-static-skylight.md](VTX-M08-skybox-static-skylight.md) | Validated plan/evidence file for skybox and static specified-cubemap SkyLight. |

## Planning Rule

No detailed milestone plan may claim a milestone is complete. Completion lives
only in [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) and requires
implementation evidence, documentation updates, and validation evidence.
