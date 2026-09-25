# VTX-M04D.4 — UE5.7 volumetric fog parity

Status: `validated`

| Field     | Summary                                                                       |
| --------- | ----------------------------------------------------------------------------- |
| Outcome   | Volumetric media/light injection, integrated scattering and temporal history. |
| Remaining | None in the recorded scope.                                                   |
| Evidence  | [Validation record](validation.md)                                            |

[Roadmap](../../PLAN.md) · [Design index](../../lld/README.md)

Dependencies: VTX-M04D.1, VTX-M04D.2, VTX-M04D.3, VTX-M03 directional CSM proof

## Delivered scope

Integrated-light-scattering runtime product path is implemented and proven with focused Stage-14 RenderDoc proof, captured Stage-15 fog static-data SRV/flag proof, focused integrated-volume sampling proof, and city-scale RenderScene capture proof. Evidence covers log-distributed froxel depth, primary/secondary height-fog spatial media density, primary directional CSM shadowed-light sampling, Oxygen distant-SkyLight volumetric ambient injection, local-fog participating-media injection, Halton jitter, reset, history-miss supersampling, and reprojection for Oxygen's integrated-scattering product. Term-isolated local-fog, SkyLight, directional-shadow, and temporal artifact proof exists from paired VortexBasic RenderDoc captures. Accepted Oxygen divergences are documented for UE conservative-depth history fixup, pre-exposure transfer, and UE's separate light-scattering history product. Real SkyLight cubemap capture/filtering is an IBL/indirect-lighting resource milestone, not a VTX-M04D.4 closure gate.

## Scope and acceptance

Required work:

- Preserve the first Vortex-native integrated-light-scattering runtime path:
  authored/requested volumetric fog allocates a 3D product, dispatches a
  Stage-14 compute pass, publishes SRV/static binding validity, and composes it
  in Stage 15 fog.
- Implement a UE5.7-informed froxel/grid representation.
- Inject participating media from height fog and local fog where applicable.
- Inject direct light, shadowed light, ambient/SkyLight, and atmosphere
  participation according to the selected parity contract.
- Treat conventional directional CSM projection proof as a closed prerequisite,
  not as remaining volumetric-fog parity. VTX-M04D.4 shadowed-light work must
  consume the validated CSM product; the primary directional volumetric CSM
  sampling path now has focused term proof and city-scale capture proof.
- Integrate scattering and transmittance into a published
  integrated-light-scattering product.
- Add temporal reprojection/history, jitter policy, reset conditions, and
  per-view resource ownership. VTX-M04D.4 implements Halton jitter, reset,
  history-miss supersampling, and reprojection for Oxygen's single
  integrated-scattering product; UE conservative-depth history fixup,
  pre-exposure transfer, and separate `LightScattering` history are documented
  accepted divergences.
- Keep CPU/HLSL ABI and publication contracts lockstep.

Exit gate:

- Authored volumetric fog changes runtime output, not just model revisions.
- Integrated light scattering is published truthfully.
- Tests and capture analysis cover enabled, disabled, history-reset, and
  lighting-participation cases.
- City-scale RenderScene proof covers `CityEnvironmentValidation` with local
  fog, directional volumetric shadows, SkyLight, temporal reprojection, and a
  debugger-backed D3D12 debug-layer audit.

Status: `validated` on 2026-04-26. Evidence is recorded in
`milestone README`.

## Supporting records

- [directional csm](directional-csm.md)

## Validation

See the [validation record](validation.md) for commands, conditions, results and remaining work.
