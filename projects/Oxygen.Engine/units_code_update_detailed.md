Units Code Update - Detailed Findings
=====================================

This file lists all code and shader files discovered (via repo grep) that reference ambiguous `intensity` / `exposure` or use unitful values without the required suffixes. For each file I give a precise, minimal-safe recommended change to bring it into compliance with the Physical Lighting spec (use `_lx`, `_lm`, `_cd`, `_nit`, `_ev` where appropriate).

Notes
-----
- I focused on source files (`src/**`) and shaders (`*.hlsl`, `*.hlsli`).
-- The recommended approach is a global, in-place rename to canonical suffixes (no duplicated fields).

Approach (preferred)
--------------------
- Use an automated codemod to rename symbols in-place to the canonical suffixes (`_lx`, `_lm`, `_cd`, `_nit`, `_ev`).
- Update corresponding HLSL/CB definitions, serialization code, tests, examples, and any tooling that depends on the exact symbol names.
- Run the codemod against a precomputed symbol map, review changes, then land the single comprehensive patch. This avoids duplicating thousands of fields and is the recommended professional path.

Files and recommended changes
-----------------------------

1) `src/Oxygen/Scene/Environment/Sun.h` / `Sun.cpp`
   - Found: public `SetIntensity`/`GetIntensity` documented as lux in comments but underlying fields/methods use ambiguous names.
    - Change: Rename the existing `intensity` symbol in-place to `sun_illuminance_lx` when it stores physical illuminance, or to `intensity_multiplier` when unitless. Use codemod to update definitions/usages, Doxygen, and tests.

2) `src/Oxygen/Scene/Environment/SkySphere.h`
   - Found: `SetIntensity(const float intensity)` / `GetIntensity()` used as multiplier.
   - Change: Rename API to `SetIntensityMultiplier`/`GetIntensityMultiplier` (or add them), and add `sky_radiance_nit_` + `SetRadianceNits`/`GetRadianceNits` for absolute radiance storage.

3) `src/Oxygen/Scene/Environment/SkyLight.h`
   - Found: `SetIntensity`, `SetDiffuseIntensity`, `SetSpecularIntensity` ambiguous.
   - Change: Provide `SetDiffuseRadianceNits(float)`/`SetSpecularRadianceNits(float)` for absolute IBL radiance; keep multipliers named `*Multiplier`.

4) `src/Oxygen/Scene/Environment/PostProcessVolume.h`
    - Found: Exposure APIs present; some comments use EV but ToneMap pass uses scalar.
    - Change: Ensure fields are `manual_exposure_ev` (already present) and add or document `exposure_scalar` for shader binding.

5) `src/Oxygen/Scene/Environment/Fog.h`
   - Found: `SetScatteringIntensity` (multiplier) exists.
   - Change: Keep as multiplier but clarify Doxygen: "unitless multiplier" to prevent confusion with lux/nit.

6) `src/Oxygen/Scene/Light/LightCommon.h` (+ `PointLight.h`, `SpotLight.h`, `DirectionalLight.h`)
   - Found: `CommonLightProperties` references and prior generic `intensity` removed but docs still refer to "intensity" in places.
   - Change: Verify all light classes expose explicit fields: `intensity_lx` (directional), `luminous_flux_lm` (point/spot) or `intensity_cd` if using candela. Replace any `intensity` usages in code/comments with unitized names.

7) `src/Oxygen/Renderer/Types/SunState.h`
    - Found: `float intensity {1.0F};` (comment: "sun intensity multiplier"), and methods using `intensity`.
    - Change: Rename `intensity` in-place to `sun_illuminance_lx` when intended as illuminance, or to `intensity_multiplier` when unitless. Adjust equality, constructors, and packing via codemod and update callers/tests.

8) `src/Oxygen/Renderer/Types/EnvironmentStaticData.h`
    - Found: `float intensity` fields in environment static payload.
    - Change: Rename `intensity` in-place to the canonical name appropriate for its semantics: `sky_radiance_nit` for absolute radiance or `sky_intensity_multiplier` for unitless multiplier. Update the HLSL CB layout and CPU-side struct names via codemod.

9) `src/Oxygen/Renderer/Types/EnvironmentDynamicData.h`
    - Found: `float exposure {1.0F};` (stores scalar) and comments mentioning exposure.
    - Change: Rename `exposure` in-place to `exposure_scalar` when it stores the linear shader scalar; if there is an authored EV field, rename it to `exposure_ev`. Update consumers, CBs and serialization to use the canonical names via codemod.

10) `src/Oxygen/Renderer/Renderer.cpp` / `Renderer.h`
    - Found: computes `ev` and writes `exposure` scalar to env manager; logs ambiguous "exposure".
    - Change: Rename manager methods/fields in-place to use `exposure_ev` and `exposure_scalar` canonical names, and update all call sites and logs via codemod.

11) `src/Oxygen/Renderer/Passes/ToneMapPass.h` / `ToneMapPass.cpp`
    - Found: `manual_exposure` field present as linear scalar in `ToneMapPass` config.
    - Change: Rename `manual_exposure` in-place to the canonical name appropriate for its semantics (`manual_exposure_ev` or `manual_exposure_scalar`) and update UI/serialization and shaders via codemod.

12) `src/Oxygen/Renderer/Passes/AutoExposurePass.h` / `AutoExposurePass.cpp`
    - Found: comments mention `offset 8 = ev` in exposure state; buffer layout and consumers sometimes read scalars.
    - Change: Rename buffer field identifiers in-place to `exposure_ev` and `exposure_scalar` and update HLSL binding comments and consumers using the codemod.

13) `src/Oxygen/Renderer/Passes/WireframePass.h`, `TransparentPass.cpp`, `ShaderPass.cpp`, `SkyCapturePass.cpp`
    - Found: these passes bind `EnvironmentDynamicData` for exposure and lighting.
    - Change: Update bindings to new explicit fields and CB packing as needed.

14) `src/Oxygen/Renderer/Internal/EnvironmentDynamicDataManager.cpp`
    - Found: writes `glm::vec4(sun.color_rgb, sun.intensity)` into `state.data.sun_color_rgb_intensity`
    - Change: Rename `sun.intensity` in-place to `sun_illuminance_lx` (or `intensity_multiplier` if unitless) and rename the CB field `sun_color_rgb_intensity` to `sun_color_rgb_illuminance_lx` via codemod; update all consumers.

15) `src/Oxygen/Renderer/Internal/EnvironmentStaticDataManager.cpp` / `.h`
    - Found: frequently references `sky_light.intensity` and `sky_sphere.intensity`, and applies a unit bridge comment "1.0 = 5000 Nits".
    - Change: Rename `sky_light.intensity`/`sky_sphere.intensity` in-place to `sky_radiance_nit` when absolute radiance is intended, or to `sky_intensity_multiplier` when unitless; make conversions explicit and update CBs/serializers via codemod.

16) `src/Oxygen/Renderer/Internal/SunResolver.cpp`
    - Found: builds temporary `intensity` floats and populates sun selection using `selection.intensity`.
    - Change: Rename local temporaries and output fields in-place to `sun_illuminance_lx` (or `intensity_multiplier` where appropriate) and update callers/tests using codemod.

17) `src/Oxygen/Renderer/Test/SunResolver_basic_test.cpp`
    - Found: asserts on `resolved.intensity`.
    - Change: Update tests in-place to reference the renamed symbol `resolved.sun_illuminance_lx` or `resolved.intensity_multiplier` and adjust fixtures via automated edits.

18) `src/Oxygen/Renderer/Docs/*` and `src/Oxygen/Scene/Docs/*` (various docs)
    - Found: many docs reference `intensity` without suffix; examples: `light-deisgn.md`, `environment_tests.md`, `hdr_render.md`, `sun.md`.
    - Change: Update documentation to show the new explicit unitized field names and authoring guidance.

19) Shader files (HLSL/HLSLI):
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentStaticData.hlsli`
      - Found: `float intensity;` in CB.
      - Change: Rename to `sky_radiance_nit` or `intensity_multiplier`, and update CPU-side struct layout.
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/EnvironmentDynamicData.hlsli`
      - Found: comments: `xyz = color_rgb, w = intensity`.
      - Change: update comment to `w = sun_illuminance_lx` or `w = intensity_multiplier` after CPU change; update consumers.
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Forward/ForwardMesh_PS.hlsl`, `ForwardDebug_PS.hlsl`
      - Found: use `env_data.sky_light.intensity` in lighting/ibl calculations.
      - Change: read `env_data.sky_light.radiance_nit` or multiply by `intensity_multiplier` depending on design; ensure naming matches CPU-side.
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Atmosphere/AtmosphereSampling.hlsli` / `AtmosphereConstants.hlsli` / `SkyColor.hlsli`
      - Found: comments mention `sun_illuminance` but some callers pass `color * intensity` in ambiguous fields.
      - Change: Use `sun_illuminance_lx` everywhere and update function params to require `_lx` naming.
    - `src/Oxygen/Graphics/Direct3D12/Shaders/Common/Lighting.hlsli`
      - Found: conversion helpers already exist; ensure they take typed variables (e.g., `float luminous_flux_lm`).

20) `src/Oxygen/Data/PakFormat.h` and `PakFormatSerioLoaders.h`
    - Found: legacy `intensity` fields in pak format structures and serialization code.
    - Change: Map legacy `intensity` read into new per-light unit fields (e.g., for directional: `intensity_lx`; for positional: `luminous_flux_lm`) in the loader; update pak format notes.

21) Examples tree (exhaustive audit)
        - Overview: The Examples tree contains many demo and sample sources that call engine APIs and therefore commonly use `SetIntensity`/`GetIntensity`, `manual_exposure`, and content files that author light properties. These files must be included in the audit and migrated to use the new unitized accessors or explicitly-labeled multipliers.

        - Representative files and recommended changes:
            - `Examples/DemoShell/Services/EnvironmentSettingsService.cpp`
                - Found: UI-driven setters calling `SetIntensity` and serializing `sky_intensity` values.
                - Change: Call `SetRadianceNits` or `SetIntensityMultiplier` depending on user input; update UI labels to show units and add a toggle for interpreting the numeric value as `nits` vs multiplier.

            - `Examples/DemoShell/Services/SkyboxService.cpp`
                - Found: passes `sky_color * intensity` into environment setup.
                - Change: Supply `sky_radiance_nit` explicitly and drop ambiguous multiplies; if multiplier UI remains, keep `intensity_multiplier` separate.

            - `Examples/Async/*` (panels: `AsyncDemoPanel.cpp`, `AsyncDemoSettingsService.cpp`)
                - Found: demo panels expose sliders named "Intensity" for demo lights.
                - Change: Rename slider labels to indicate unit (e.g., "Intensity Multiplier") or expose a unit selector; update event handlers to call new accessors.

            - `Examples/D3D12-Renderer/main.cpp` and `Examples/D3D12-Renderer/MainModule.cpp`
                - Found: sample renderer pipelines that initialize scene/environment with default `intensity` values.
                - Change: Initialize with `sun_illuminance_lx` / `sky_radiance_nit` if sample values are absolute. If values are meant as artistic multipliers, rename to `*_multiplier`.

            - `Examples/Content/*` (scene specs in YAML under `Examples/Content`, e.g. `cube_scene_spec.yaml`, `emissive_scene_spec.yaml`, `instancing_test_spec.yaml`)
                - Found: authored light entries that include `intensity: 1.0` or similar.
                - Change: Update schema/examples to support `intensity_multiplier` and `radiance_nit`/`luminous_flux_lm` fields; add migration notes in comments.

            - `Examples/TexturedCube/*`, `Examples/RenderScene/*`, `Examples/MultiView/*`
                - Found: demo scenes and initialization code calling engine light APIs with ambiguous names.
                - Change: Migrate calls to the new unitized field names; flag these as low-risk, high-value updates to ensure examples demonstrate correct authoring workflows.

        - Why examples matter: Examples are the canonical learning material for engine users; leaving ambiguous `intensity` in demos will propagate incorrect authoring practices. Make examples show both absolute unit fields and unitless multipliers where appropriate.

        - Migration approach for Examples:
            1. Run the codemod to rename fields in-place in YAML/scene specs and demo C++ to the canonical names.
            2. Update UI labels and documentation to reference the new names.
            3. Run demo smoke tests to verify parity and adjust example numeric values if absolute units (nits/lux) were intended.

22) Misc content & tools referencing `intensity`
    - Files: `src/Oxygen/Content/Test/TestData/scene_with_lights_and_environment.yaml`, `src/Oxygen/Content/Tools/PakGen` validator
    - Change: Update content schema and validator to prefer unitized fields and flag legacy `intensity` as deprecated (with migration guidance).
