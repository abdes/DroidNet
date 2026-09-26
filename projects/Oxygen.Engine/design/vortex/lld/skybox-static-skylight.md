# Skybox and specified-cubemap lighting

A SkySphere draws the visible background. A SkyLight illuminates surfaces.
They can share a cubemap asset, but their controls and renderer products are
independent. Enabling either does not enable the other.

Read: [authoring](#authoring), [background selection](#background-selection),
[rendering](#rendering), [follow-ups](#follow-up-capabilities).
[Captured-sky IBL](captured-sky-ibl.md) owns lighting math, capture and scheduling;
[cubemap processing](cubemap-processing.md) owns specified-source conventions.
[VTX-M08](../milestones/VTX-M08/README.md) records the original static baseline;
[VX-IBL-01](../milestones/VX-IBL-01/README.md) owns complete sky lighting.

## Authoring

`SkySphere` selects a cubemap or solid color. Cubemap sampling applies its own
+Z yaw, tint and intensity; solid color applies tint and intensity. An unresolved
cubemap has explicit unavailable status. It is not substituted as a valid black
texture.

`SkyLight` selects a specified cube or captured atmosphere/height fog. Its
intensity, tint, diffuse/specular multipliers, reflection participation and
volumetric contribution are independent of the background controls. Specified
sources use their own `source_cubemap_angle_radians`. The default lower hemisphere
is solid black with blend alpha 1; disabling replacement leaves source radiance
unchanged. Rotation defaults to zero.

The obsolete SkyLight capture-mode boolean is removed. Scene v8 and settings v6
use automatic source-change scheduling; see the
[migration contract](captured-sky-ibl.md#11-automatic-scheduling-and-canonical-migration)
for migration and recooking. Fog's capture-visibility flag remains separate.

## Background selection

The per-view frame plan resolves one visible background:

1. Environment disabled: none.
2. Enabled atmosphere and view atmosphere feature: procedural sky.
3. Otherwise, enabled valid SkySphere: cubemap or solid color.
4. Otherwise, none.

A selected SkySphere suppresses the procedural full-background draw. Atmosphere
LUTs can still support transmittance, aerial perspective and fog where enabled.
The selection is immutable for that view's render. Auxiliary/offscreen views
use their own feature masks and published environment slots.

Analytic height fog is evaluated on the distant ray in the sky shader, exactly
once. It applies over procedural sky, cubemap or solid color, and can render a
fog-only background without atmosphere. Main-pass and capture visibility remain
independent. Opaque fog still uses the existing depth-based path; local and
volumetric media keep their own composition rules.

## Directional-light interaction

Directional lights independently provide direct surface lighting, atmosphere
scattering inputs and shadows. Direction/intensity changes on enabled
atmosphere-role contributors change captured-sky lighting. Role-None/direct-only
edits leave capture identity unchanged. Specified cubemap products remain
independent of those light edits.
Static cubemap texels are authored radiance: any sun in the image is part of the
texture, not a procedural disk or a light source tied to the scene light.

SkySphere intensity is a scene-linear multiplier. A value of 1 preserves the
source texels; normalized HDRIs may need larger authored intensity under daylight
exposure. No hidden normalization or reciprocal-exposure brightening is applied.

## Rendering

[`VortexSkyPassPS`](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl)
uses the existing Stage 15 fullscreen sky pass: SceneColor only, read-only depth,
no depth writes, and the view's viewport/scissor. It samples the selected source,
composes distant height fog and writes pre-exposed HDR radiance. Tone mapping
remains in PostProcess. It writes no material, normal, velocity or shadow data.

Environment publishes dedicated typed slots for processed radiance, diffuse SH,
specular prefilter and metadata, with the BRDF lookup in frame bindings. No slot
aliases SH data as a TextureCube. SkyLight enabled means the view may consume the
complete product set, not merely that an authored flag is true.

Stage 13 adds diffuse/specular lighting to deferred surfaces. Forward/translucent
surfaces use the same evaluator in their own pass. Stage 12 remains direct only.
Fog consumers use the published SH contract where supported; the visible
SkySphere is never a fallback lighting source.

Products are shared per scene across views. Environment-disabled views publish
lighting disabled and cannot inherit another view's background or products.
Changing camera/exposure/background controls does not regenerate the scene's
lighting radiance. Source replacement and authoring follow the common cache and
lifetime contract.

## Diagnostics and validation

DemoShell shows source readiness/failure and keeps valid lighting visible while
runtime updates are pending. Generation, age, timing and GPU metadata belong in
Frame Diagnostics. Existing `ibl-only` and `direct-plus-ibl` modes separate the
indirect/direct contributions. Raw-sky, irradiance and specular diagnostic views
remain tracked by [VX-DIAG-02](../OPEN_ITEMS.md#p2--engineering-follow-ups); product
availability alone does not implement a diagnostic view.

[VTX-M08 validation](../milestones/VTX-M08/validation.md) retains the original
skybox, SH, ABI and allocation proofs. Current product, capture, forward/deferred,
native/editor and lifecycle checks are in
[VX-IBL-01 validation](../milestones/VX-IBL-01/validation.md). Paired captures show
that direct lighting remains unchanged while previously black exterior walls
gain sky lighting and visible material detail.

## Follow-up capabilities

Cubemap blending, SkyLight AO/shadowing, baked integration and broader probes
remain VX-SKY-01. A procedural disk over a static cubemap is VX-SKY-02.
[OPEN_ITEMS.md](../OPEN_ITEMS.md) owns their priority and next actions.
