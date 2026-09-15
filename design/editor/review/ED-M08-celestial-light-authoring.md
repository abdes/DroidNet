# Celestial-light authoring contract

Status: **final implementation contract**

## 1. Canonical assignment

Each directional light owns one `AtmosphereLightSlot` value: **None / Primary /
Secondary**. Reuse existing enum values and relevant setter/getter names.
Primary/Secondary identify stable atmosphere assignments, not celestial kind,
brightness rank, automatic promotion or reduced feature support.

A scene summary may display the assignments and navigate to their lights. It
stores no competing Sun pointer. Redundant `IsSunLight`/Contributes authoring
state is removed through canonical migration, not kept as another assignment
path. Ordinary light participation remains the independent Affects Scene gate.

| Scene | Assignment | Required behavior |
| --- | --- | --- |
| One sun | Sun→Primary | Sun illuminates geometry and atmosphere when participating |
| Two suns | SunOne→Primary; SunTwo→Secondary | Both contribute simultaneously with independent direction, colour, lux, source angle and shadows |
| Sun and moon | Sun→Primary; Moon→Secondary | Both can contribute at twilight; disabling Sun leaves Moon in Secondary |
| Directional fill | Fill→None | Ordinary illumination/shadows without atmospheric disk/scattering contribution |

## 2. Validation and lifetime

- At most one stored owner per non-None slot. Validate all stored assignments,
  including hidden/off lights, before edit, duplication, import or publication.
- Reject a conflicting assignment atomically and identify the occupying light.
  There is no traversal-order winner, brightness-based fallback or silent steal.
- Effective node visibility and Affects Scene jointly gate participation.
  Disabling a source preserves its assignment, colour/intensity and shadow
  settings. Re-enabling restores it; the other source is never promoted.
- Removing an atmosphere assignment leaves ordinary directional illumination
  intact. Deleting a light removes its assignment; Undo restores its identity
  and role through the same conflict validation.
- Parent transforms and IgnoreParentTransform behavior follow the engine's
  transform contract; atmosphere assignment does not create transform policy.
- Changes to assignment, effective visibility, light parameters or hierarchy
  invalidate affected light selection, sky and captured-lighting products.

## 3. Rendering contract

Both participating slots contribute to sky scattering and analytic disks. Both
can illuminate surfaces and cast requested shadows in forward/deferred rendering
and applicable fog shadowing. Secondary-only operation is fully functional.
The renderer's direct-light list is independent of atmospheric membership.

Captured-sky diffuse irradiance and roughness-dependent specular products reflect
both contributors. Explicit disk suppression during reflection capture remains
a documented energy-accounting policy; it cannot suppress the second source's
atmospheric illumination or leave stale products after a change.

V0.1 qualifies two simultaneous shadowed directional contributors; this is not
an unlimited-light performance guarantee. The Moon representation is directional
moonlight and an analytic disk. Lunar textures/phases/orbits, more than two
atmospheric contributors and authored sky-only contribution are post-V0.1 scope.
A body-kind control is not exposed without corresponding rendering semantics.

## 4. Implementation changes and ownership

The existing atmosphere integrator already evaluates two sources, including
independent transmittance/scattering terms and disks. Current direct surface
packets and directional shadow authority use only Primary, and source/cooked
persistence does not carry the complete explicit-slot model. Extend those paths;
do not treat two visible disks as complete dual-source lighting.

Scene/Data/Cooker own the canonical slot schema, packed records, validation,
provenance and hydration. World/WorldEditor own source values, commands,
copy/Undo/Redo and the inspector. Runtime/Interop adapt the native contract.
Vortex owns surface-light packets, shadow resources, sky/fog consumers and
capture-product invalidation. The resolver remains a selector, not an astronomy
simulator.

Source entry points:

- [DirectionalLight.h](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLight.h)
- [DirectionalLightResolver.cpp](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp)
- [PakFormat_world.h](../../../projects/Oxygen.Engine/src/Oxygen/Data/PakFormat_world.h)
- [FrameLightSelection.h](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Types/FrameLightSelection.h)
- [SceneRenderer.cpp](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp)
- [AtmosphereUeMirrorCommon.hlsli](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli)
- [Sky.hlsl](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl)
- [VolumetricFog.hlsl](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/VolumetricFog.hlsl)

Migrate useful explicit slots and unambiguous old role combinations. Conflicting
or unrepresentable assignments require repair rather than collapsing two bodies
to one. No legacy resolver or cosmetic Primary/Secondary renaming is added.
Source-local deferred notes follow the
[engine capability record](../../../projects/Oxygen.Engine/design/vortex/plan/editor-v01-deferred-capabilities.md).

## 5. Eliminated alternatives and industry basis

A single scene Sun reference is compact and prevents conflicts in a deliberately
single-source sky, as illustrated by
[Unity's procedural skybox Sun](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/RenderSettings-sun.html).
It cannot represent simultaneous dual suns or sun/moon twilight. A scene-level
list could render the same result; per-light ownership makes contribution clear
at the light and avoids a second editable assignment source.

Explicit participation is established practice:
[Unreal](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine)
uses per-directional atmosphere assignment with two indices;
[HDRP](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html)
provides per-light celestial participation and multiple disks;
[Godot](https://docs.godotengine.org/en/4.6/classes/class_directionallight3d.html)
separates light/sky contribution. These systems have different capacity and
second-light details; their UI patterns do not establish identical rendering.

A/B renaming provides no capability improvement over the existing stable slots.
An exclusive IsSun boolean plus independent environment/slot/Sun-pointer fields
creates contradictory assignments. Primary-first fallback hides missing or
conflicting content. Those alternatives are excluded.

## 6. Qualification

Run native rendered cases first, then normal editor edit/history/Save/cook/load:
Primary only; Secondary only; both with distinct directions/colours/intensities/
angles and requested shadows; role-None fill; atmosphere off; hide/disable/
re-enable either source; conflicts including hidden/off occupants; deletion/Undo;
captured diffuse/specular invalidation and migration.

Observe actual surface, shadow and atmospheric contributions and preserve role
identities through the complete path. Tests and images are produced by the
[development-only qualification workflow](../lld/standalone-runtime-validation.md).
