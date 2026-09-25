# VTX-M04D.6 — validation

This record preserves the conditions and results at each implementation checkpoint.
The milestone README owns the final outcome and remaining work.

## Validation and remaining work

**Qualification:** `validated` for main-view AP

Vortex AP sampling uses camera-volume lookup helpers traced to UE5.7 `SkyAtmosphere` shader behavior, preserves raw camera-volume generation with apply-time strength control, exposes effective DemoShell/VortexBasic AP controls, fixes city-scale reversed-Z far-depth AP composition, and has focused enabled/disabled plus city-scale RenderDoc proof.

**Remaining work:** Reflection/360-view AP is explicitly deferred to the future reflection-capture resource path.

## Current Findings

- Vortex has camera aerial-perspective shader sampling and atmosphere LUT
  infrastructure.
- The first M04D.6 implementation pass removed stale approximation wording,
  shaped the consumer helper after UE5.7 `GetAerialPerspectiveLuminanceTransmittance`,
  and fixed the AP-strength contract so camera-volume generation stores raw
  inscatter/transmittance while the artistic strength control applies only at
  final sampling.
- Focused publication tests now cover AP authored controls, camera-volume
  dimensions/dispatches, resource-slot publication, height-fog contribution,
  and the main-pass apply-time gate.
- RenderScene main-scene visual validation was confirmed on 2026-04-25 after
  DemoShell stopped clamping the UE-style distance-scale control to `16`,
  exposed effective AP controls/CVar state, and routed fullscreen composition
  through the main AP helper/gate.
- Focused VortexBasic enabled/disabled capture proof and city-scale
  `CityEnvironmentValidation` capture proof passed on 2026-04-26. The city
  proof found and fixed an Oxygen fullscreen-compose divergence where AP used
  the height-fog far-background ramp but discarded any nonzero far mask; under
  reverse-Z this rejected distant city geometry with valid small depth values.
  The compose pass now matches the height-fog gate and discards only the
  actual far-clear background (`far_background > 0.999`).
- The city scene previously carried `aerial_perspective_distance_scale=700`
  as a visibility compensation for the broken compose path. After AP became
  active on city geometry, that produced excessive haze; the authored value is
  now restored to the UE-style default `1.0` and the scene was recooked for the
  accepted capture proof.
- VTX-M04D.5 runtime proof must not claim full atmosphere runtime closure until
  this milestone records AP parity evidence.

## Scope and acceptance — results

- VTX-M04D.6 has validated main-view camera AP proof recorded in
  `milestone README`, including focused VortexBasic enabled/disabled
  captures and a city-scale `CityEnvironmentValidation` capture.
- RenderScene visual validation for the main scene AP path was confirmed on
  2026-04-25 after DemoShell exposed effective AP controls and the compose pass
  used the main AP helper/gate.
- Reflection/360-view AP behavior is explicitly deferred and is not claimed by
  the validated main-view AP package.
