# VTX-M04D.4 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated`

Stage-14 compute volumetric fog allocates and publishes `IntegratedLightScattering`, Stage 15 composes it, and proof covers captured fog payload/SRV/grid state, volume min/max/slices, directional CSM shadowed-light sampling, local-fog participating-media injection, Oxygen distant-SkyLight volumetric ambient, temporal jitter/reset/history-miss reprojection, city-scale `CityEnvironmentValidation`, and D3D12 debug-layer audit. Directional CSM projected-shadow blocker was closed with RenderDoc proof and manual visual confirmation.

**Remaining work:** Accepted Oxygen divergence: single integrated-scattering temporal product without UE conservative-depth history fixup or pre-exposure transfer. Real SkyLight cubemap capture/filtering remains outside this milestone.
