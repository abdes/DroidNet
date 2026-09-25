# EX02 — Settings and fixed exposure

Status: `validated`

| Field     | Summary                                                 |
| --------- | ------------------------------------------------------- |
| Outcome   | Normalized settings and fixed/physical-camera exposure. |
| Remaining | None in the recorded scope.                             |
| Evidence  | [Record below](#ex02--settings-and-fixed-exposure)      |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

- Implement shared validation/resolution for native settings and view overrides,
  including zero target, coupled ranges and representable gain.
- Remove the fixed-exposure floor and propagate the exact resolved multiplier
  through constants. Add scalar, upload and native GPU regressions.
- Introduce explicit frame exposure bindings and common state definitions,
  with compile-time CPU/HLSL size/layout checks and ShaderBake validation.
- Define native mask/curve/new-setting types for the following runtime slices.

**Gate:** EV14/15/16, boundaries, compensation, keys and disabled exposure reach
the GPU correctly. Invalid settings retain the previous valid revision.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 2 — Settings and fixed exposure | validated | Canonical authored input, immutable pass snapshots, fixed/camera gain, per-view settings and public mask acceptance are qualified. | [Fixed gain](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/fixed-gain/evidence-manifest.json), [frame bindings](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/frame-binding/evidence-manifest.json), [configuration and mask acceptance](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/lifecycle/review-r028-manifest.json) |
