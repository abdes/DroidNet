# EX03 — Metering and adaptation

Status: `validated`

| Field     | Summary                                                               |
| --------- | --------------------------------------------------------------------- |
| Outcome   | Histogram metering, masks, compensation curves and hybrid adaptation. |
| Remaining | None in the recorded scope.                                           |
| Evidence  | [Record below](#ex03--metering-and-adaptation)                        |

[Roadmap](../../../PLAN.md) · [Design index](../../../lld/README.md)

## Delivery scope

- Implement bounded stratified sampling, conserved two-bin weights, percentiles,
  finite/black/coverage rules and overflow-safe counts.
- Implement mask sampling and exact piecewise-linear compensation curves.
- Implement the analytic hybrid log-gain update, clamped targets, zero speeds,
  equal EV bounds, zero target and positive-target restoration.
- Add independent histogram and adaptation reference tests plus native GPU
  captures using existing fixtures; vary frame schedules at equal elapsed time.

**Gate:** weighted distributions, targets and adaptation trajectories match
independent expectations; 8K output cannot overflow histogram accumulation.

## Recorded qualification

| Slice | Status | Boundary | Evidence |
| ----- | ------ | -------- | -------- |

| 3 — Metering and adaptation | validated | Controlled-input histogram, curve, masks and hybrid adaptation; scene acceptance remains slice 5. | [Metering](../../../../../out/build-ninja/analysis/vortex/exposure-lightbench/metering/evidence-manifest.json) |
