# ED-M08: Explicit celestial-light authoring

Status: **replacement recommendation pending approval**. The user approved the
visibility/contribution/shadow contract except its single scene-sun selector.
Two-body scope and sun-role changes must not be treated as approved yet.

## 1. Answer to the challenge

A single Sun selector is not inherently better or more industry-aligned than
explicit per-directional-light designation. Its real advantages are a compact
central UI, one unambiguous reference and easy exclusivity for a deliberately
single-atmospheric-light product. Those do not justify preventing two suns or a
sun and moon from contributing simultaneously.

The earlier proposal imposed a one-light limit without a sufficient product
reason. It is withdrawn. Switching one reference from Sun to Moon would also
fail at twilight, when both should contribute. Leaving the second light as an
ordinary directional supplies no second atmospheric disk/scattering by itself.

Two independent design questions were conflated: **where the assignment lives**
and **how many atmospheric lights are supported**. A scene-level list of two
references could support the same rendering as two per-light roles. There is no
image-quality benefit inherent in either storage location. Prefer per-light
designation here because the contribution is visible while authoring that light,
travels with the light's definition, and fits Oxygen's existing native component
model. Keep one authoritative assignment source; a read-only scene summary may
display it without introducing a competing editable Sun pointer.

## 2. Industry evidence

| System | Actual authoring model | Relevance and limit |
| --- | --- | --- |
| Unreal Sky Atmosphere | Each Directional Light enables Atmosphere Sun Light and selects index 0 or 1. | Epic explicitly supports two atmospheric directionals and sun/moon setup. Index 1 is not intrinsically a moon. Some second-light features differ: documentation excludes its multiple scattering and certain cloud shadows. |
| Unity HDRP Physically Based Sky | Directional lights opt into Affect Physically Based Sky and expose celestial-body controls. | Documentation discusses multiple sun disks and reflected/manual/emissive body shading. This is not the same system as Unity's single procedural-skybox Sun reference. |
| Unity procedural skybox / URP main light | One `RenderSettings.sun` reference; procedural sky falls back to the brightest directional when unset. | Supports the simplicity argument for a single-source sky, not a general multiple-body design. URP main-light selection and additional-light capability are separate pipeline issues. |
| Godot | Each DirectionalLight3D chooses Light and Sky, Light Only or Sky Only. | Sky shaders expose four directional inputs. This supports explicit per-light contribution; it does not imply unlimited bodies or require Oxygen to expose every mode in V0.1. |

Sources: [Unreal Sky Atmosphere](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine),
[Unreal directional-light settings](https://dev.epicgames.com/documentation/unreal-engine/directional-lights-in-unreal-engine),
[HDRP sky setup](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/create-a-physically-based-sky.html),
[HDRP celestial-body controls](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.0/manual/reference-light-component.html),
[Unity RenderSettings.sun](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/RenderSettings-sun.html),
[Godot directional sky modes](https://docs.godotengine.org/en/4.6/classes/class_directionallight3d.html),
[Godot sky-shader inputs](https://docs.godotengine.org/en/4.6/tutorials/shaders/shader_reference/sky_shader.html).

The industry-aligned principle is explicit atmospheric participation with
separate ordinary illumination, not universal single-sun exclusivity. Oxygen
should retain useful capabilities rather than reproduce another renderer's
specific second-light limitations merely to claim parity.

## 3. Oxygen's current implementation

- The native component already has None, Primary and Secondary atmosphere slots.
- The scattering integrator handles both slots: independent directions,
  illuminance, Rayleigh/Mie terms, transmittance, planet occlusion and
  multiple-scattering terms. A secondary-only contributor is representable.
- Both visible disks use the same analytic circular-disk helper. There is one
  global disk-enable switch; reflection captures suppress both explicit disks.
- `ResolveMoon()` simply aliases secondary-sun resolution. It is not an
  astronomical Moon type and does not establish phase, texture or orbit behavior.
- Current direct PBR lighting and directional CSM selection publish only slot 0.
  Both atmospheric lights can illuminate fog, but only slot 0 receives the
  current directional shadow visibility there.
- Managed authoring, packed scene data, cooking and the editor bridge do not
  preserve the native explicit secondary-slot model end to end.
- `IsSunLight`, `EnvironmentContribution`, explicit slots and the scene Sun
  pointer overlap. Current validation permits only one tagged sun; primary slot
  fallback and duplicate-slot first-wins behavior add implicit selection.

Therefore **two native atmosphere slots do not mean Oxygen already provides two
complete suns illuminating and shadowing geometry**. The atmosphere is ahead of
the direct-light and authoring pipeline.

Sources: [native roles](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLight.h),
[resolver and Moon alias](../../../projects/Oxygen.Engine/src/Oxygen/Scene/Light/DirectionalLightResolver.cpp),
[two-light scattering](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/AtmosphereUeMirrorCommon.hlsli),
[disk rendering](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/Sky.hlsl),
[fog lighting](../../../projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/Environment/VolumetricFog.hlsl),
[direct-light selection](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/SceneRenderer/SceneRenderer.cpp),
[single directional packet](../../../projects/Oxygen.Engine/src/Oxygen/Vortex/Types/FrameLightSelection.h),
[packed light record](../../../projects/Oxygen.Engine/src/Oxygen/Data/PakFormat_world.h),
[editor light adapter](../../../projects/Oxygen.Editor.Interop/src/Commands/DirectionalLightPropertyApplier.h).

## 4. Complete replacement proposal

On **each Directional Light**, expose one field:

**Atmosphere Role: None / Light A / Light B**

This is explicit per-light designation, not one scene-wide selector. A and B
identify stable atmosphere assignments; neither means a particular celestial
kind, brightness or priority. They map to native slots 0 and 1 internally.

| World | Assignments | Required result |
| --- | --- | --- |
| One sun | Sun → A; B empty | Sun affects geometry and atmosphere when participating. |
| Two suns | Sun One → A; Sun Two → B | Both can be visible and contribute simultaneously, with independent direction, colour, intensity and source angle. |
| Sun and moon | Sun → A; Moon → B | Both can contribute at twilight. At night the Sun can be disabled without moving Moon to A. |
| Directional fill | Fill → None; Affects Scene on | Illuminates geometry without an atmosphere disk or scattering contribution. |

Rules are explicit:

1. At most one stored assignment per role, checked even if a light is temporarily
   hidden or off. Conflicting edits/imports/cooks report the occupying light;
   no traversal-order winner, brightness-based promotion or silent reassignment.
2. Light Affects Scene and effective node visibility retain their approved
   meanings. Disabling a source stops its contribution but retains its role.
   Re-enabling restores it; the other role never changes to compensate.
3. Removing an atmosphere role leaves ordinary directional illumination intact.
4. Each participating directional light honors its approved shadow controls.
   **Qualify two simultaneous shadowed directional contributors** in both surface
   paths and applicable fog shadowing; do not call two disks with only one real
   scene light dual-sun support. This is a V0.1 qualification requirement, not an
   unlimited-light performance guarantee.
5. Sky scattering and the approved captured-sky diffuse/specular lighting respond
   to both roles, with correct product invalidation when either changes. Keep
   reflection-capture disk policy explicit to avoid counting direct source energy
   twice; it must not silently omit the second source's atmospheric contribution.
6. Remove duplicate authoring `IsSunLight`/Contributes/Sun-pointer state after
   migration to the role. A scene summary is read-only. The native canonical
   contract has explicit participation/assignment without old fallback readers.
7. V0.1 supports two atmospheric directional contributors. The Moon use case
   means moonlight and an analytic sky disk; lunar surface textures, phases and
   orbital simulation are not implied. No body-kind checkbox is exposed without
   a renderer effect. Sky-only authoring is not added by this proposal: the
   already approved Affects Scene gate continues to govern all light contribution.

This settles the celestial assignment, conflict, disable/re-enable and two-body
behaviors. It does not pretend that naming a directional light Moon implements
an astronomical body renderer.

## 5. Implementation and qualification

Persist the canonical role across managed model, commands/copy/Undo/Redo,
Save/reopen, engine schema and packed records, cooker, native loader and interop.
Migrate useful existing explicit slots and unambiguous sun-role combinations;
report conflicts instead of collapsing a two-body scene to one light.

Extend the current single-directional direct-light packet and shadow authority
under their engine owners. Do not encode scene lighting policy in an editor shim
or keep slot B decorative. Reuse the existing two-source atmosphere integration.
Update documentation alongside the implementation, including neutral slot names
and the limited analytic-disk Moon representation.

Native rendered cases precede editor cases: A only, B only, A+B with distinct
directions/colours, both requested shadows, ordinary role-None fill, disable and
re-enable each source, conflicts, atmosphere-off, capture invalidation and
Save/cook/load preservation. Test dawn/dusk coexistence, not just switching one
active reference. Use development-only qualification targets and retained images;
no qualification workflow ships in normal editor Debug/Release builds.

Research in this turn is source/documentation review. No renderer implementation,
build or new dual-light rendered validation was performed.
