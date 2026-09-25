# VTX-M04D.5 — Environment runtime proof and Async preparation

Status: `validated`

| Field     | Summary                                          |
| --------- | ------------------------------------------------ |
| Outcome   | Combined environment runtime and analyzer proof. |
| Remaining | None in the recorded scope.                      |
| Evidence  | [Validation record](validation.md)               |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M04D.2, VTX-M04D.3, VTX-M04D.4, VTX-M04D.6

## Delivered scope

`Run-VortexBasicRuntimeValidation.ps1` now builds, runs a CDB/D3D12 audit, captures RenderDoc frame 5, and asserts one runtime path exercising atmosphere, main-view AP, height fog, local fog, volumetric fog, and authored SkyLight unavailable/volumetric state. Real SkyLight cubemap capture/filtering remains a separate IBL/indirect-lighting resource implementation before usable IBL output can be claimed.

## Scope and acceptance

Required work:

- Add one repeatable runtime path that exercises atmosphere, height fog, local
  fog, volumetric fog, aerial perspective, and SkyLight together.
- Prefer `Examples/Async` when the runtime seam is ready, because the PRD names
  it as the canonical runtime validation example.
- Add analyzer/probe coverage for environment bindings, relevant resources,
  pass names, dispatch/draw counts, and expected outputs.

Status: `validated` on 2026-04-26. Evidence is recorded in
`milestone README`.

Exit gate:

- The runtime proof command, analyzer output, and assertion result are recorded
  in `milestone README`.
- This milestone must not claim atmosphere runtime closure until VTX-M04D.6
  records aerial-perspective parity evidence. VTX-M04D.6 has validated
  main-view AP proof; reflection/360 AP remains deferred.

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
