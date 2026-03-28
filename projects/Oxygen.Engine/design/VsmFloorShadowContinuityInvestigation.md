# VSM Floor-Shadow Continuity Investigation

Status: `in_progress`

Date: 2026-03-28

## Problem Summary

The remaining real VSM runtime defect is the directional floor-shadow continuity bug visible in the live renderer capture. The rendered floor shadow is still broken into discontinuous, page-aligned slabs instead of producing a stable continuous shadow silhouette from the two boxes.

This is not a completed investigation. The bug is still open.

## User-Visible Symptom

- In the live renderer capture, the floor shadow is visibly discontinuous and page-like.
- The shadow mask shows blocky regions and continuity loss across the receiver floor.
- This is inconsistent with the expected shadow cast by the two-box scene under the directional sun.

## Current Evidence

### Focused Stage 15 Test Results

These focused tests were rerun in `out/build-ninja` during this investigation:

```powershell
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe --gtest_filter=VsmShadowProjectionLiveSceneTest.DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes --gtest_color=no
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe --gtest_filter=VsmShadowProjectionLiveSceneTest.DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes --gtest_color=no
```

Observed result:

- `DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes`: passed
- `DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes`: failed

This is important because it means the remaining bug is strongly tied to the multi-cascade directional path rather than the whole Stage 15 path being uniformly broken.

### Representative Failure Pattern

The four-cascade failure currently shows two classes of bad probes:

1. Analytically shadowed floor probes that stay too bright.
2. Analytically lit floor probes that are darkened.

Representative failing routes:

- Level 0, page `(8, 7)`, `page_table_index=120`, `physical_page=20`
- Level 1, page `(8, 7)`, `page_table_index=376`, `physical_page=50`
- Level 1, page `(8, 8)`, `page_table_index=392`, `physical_page=53`

Representative observations from the current failure:

- Shadowed probes on page `120` often produce `0.5` or `0.75` visibility instead of a clearly dark result.
- Lit probes on page `392` sometimes produce `0.25` or `0.5` visibility.
- The page `392` failures include tap depths like `[0.354933, 1, 0.354706, 1]` and `[0.355512, 0.355409, 0.355285, 1]` for receiver depths around `0.42-0.43`.
- The page `120` failures often show receiver depth and stored depths that are very close together, which suggests either shadow-edge sampling or floor self-shadow interactions.

## Important Truths Established So Far

- A real Stage 12-related engine bug was already found and fixed earlier: offscreen `ResolvedView` lifetime in `Renderer::OffscreenFrameSession`.
- A real readback deadlock bug was found and fixed earlier in the readback tracker/manager path.
- The current floor-shadow continuity bug is not resolved by those fixes.
- The current floor-shadow continuity bug is not resolved by the directional clipmap resolution fix described below.
- The current floor-shadow continuity bug is not resolved by the Stage 15 PCF anchor fix described below.

## Engine Changes Already Attempted In This Investigation

These changes were already made in the worktree before this handoff document was written.

### 1. Directional clipmap snapping/resolution fix

File:

- [src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp)

What was changed:

- Directional clipmap snapping and padded half-extent logic were switched to use the real VSM virtual clipmap resolution:
  - `virtual_resolution = pages_per_axis * page_size_texels`
- A shared VSM page-size constant was introduced:
  - `kVsmShadowPoolPageSizeTexels = 128U`

Why:

- The VSM path had copied conventional shadow logic that used a conventional shadow resolution like `3072`.
- The actual VSM clipmap resolution at `kHigh` is `16 * 128 = 2048`.
- That mismatch could have caused wrong cascade snapping and page alignment.

Outcome:

- Build succeeded.
- Single-cascade test still passed.
- Four-cascade failure remained.

Conclusion:

- This was a plausible real bug candidate, but it did not fix the remaining floor-shadow continuity defect.

### 2. Stage 15 2x2 PCF anchor fix

Files:

- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli)
- [src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp)

What was changed:

- The 2x2 PCF kernel anchor changed from:
  - `center = int2(pixel)`
- to:
  - `base = int2(floor(pixel - 0.5.xx))`

The CPU oracle in the test was updated to mirror the same sampling convention.

Why:

- The previous logic anchored the 2x2 taps at `floor(pixel)` instead of centering them around the sample.
- That is a real half-texel error in the Stage 15 PCF path.

Outcome:

- Build succeeded.
- Single-cascade test still passed.
- Four-cascade failure remained, but some failing samples shifted from `0.75` to `0.5`.

Conclusion:

- This was a real Stage 15 bug.
- It improved the failure profile but did not close the multi-cascade continuity bug.

## UE5 Reference Comparison Already Done

An existing subagent was reused during this investigation to compare Oxygen’s directional VSM path with the local UE5 reference analysis:

- `src/Oxygen/Renderer/Test/VirtualShadow/UE5-VSM-Source-Analysis.md`

The strongest mismatches it reported were:

1. Missing or insufficient explicit handling of UE5-style directional clipmap `relative corner offset`.
2. Remaining sub-page texel-center mismatch between Stage 12 crop and Stage 15 sampling.
3. Potential page-border filtering mismatch because Oxygen clamps Stage 15 PCF taps within the current physical page.

The subagent also concluded that a pure “wrong page index” bug is less likely than a sub-page phase or border-filtering mismatch.

## Current Hypotheses

These hypotheses are evidence-backed enough to continue, but none is proven yet.

### Hypothesis 1: Remaining multi-cascade directional alignment bug

This is currently the strongest engine-side hypothesis.

Why:

- The single-cascade case passes.
- The four-cascade case fails.
- The failures span more than one cascade level.
- The failure is visible in the isolated Stage 15 pipeline too, not only the live shell.

Most likely files:

- [src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp)
- [src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmDirectionalProjection.hlsl](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmDirectionalProjection.hlsl)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli)

### Hypothesis 2: The dense four-cascade analytic oracle is partially selecting edge probes

This hypothesis became stronger late in the session.

Why:

- Some “lit” failing probes have world positions around:
  - `x ~= 2.2 to 2.4`
  - `z ~= -4.0 to -4.2`
- Under the sun direction used by the scene:
  - `sun_direction_ws = normalize(0.40558, -0.40558, -0.819152)`
- The tall box shadow footprint plausibly extends toward positive `x` and negative `z`.
- That means some probes currently classified as “lit” may actually lie on or near the true shadow edge.

This does not prove the runtime is correct. It only means the current dense analytic classifier may still contain edge-selection mistakes.

### Hypothesis 3: Border sampling / page-local filtering mismatch

This remains plausible.

Why:

- Stage 12 rasterizes page-local depth.
- Stage 15 clamps the 2x2 PCF footprint inside the current page.
- If adjacent pages do not carry consistent border texels, visible page seams are expected.

### Hypothesis 4: The bug is partly floor self-shadow / receiver-depth edge behavior

Some shadowed probes on page `120` have stored depths very close to receiver depth. That pattern could indicate:

- page-edge shadow transitions
- floor self-shadow interactions
- or a combination of both

This hypothesis is weaker than the multi-cascade alignment hypothesis, but it should not be discarded.

## Why The Bug Is Still Not Claimed Fixed

No truthful completion claim is possible yet because:

1. The user-visible continuity bug is still open.
2. The dense four-cascade directional floor-classification test is still failing.
3. No new engine change has yet made the multi-cascade case pass.
4. No new visual proof has been produced that the live renderer floor shadow is now continuous.

## Analysis That Should Continue Later

### 1. Verify whether the “lit” failing probes are actually analytically shadowed

This should be the first next step.

Goal:

- For the exact failing “lit” probes on page `392`, compute and print analytic shadow penetration length directly.
- Confirm whether those probes are truly lit, truly shadowed, or on the shadow edge.

If those probes are actually shadowed, then:

- the dense test oracle needs tightening
- and the remaining real engine bug may be smaller or different than the test currently suggests

If they are truly lit, then:

- Oxygen still has a real projection/raster alignment bug on page `392`

The relevant test code is in:

- [src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp)

Key functions to inspect:

- `AnalyticShadowPenetrationLength(...)`
- `IsShadowedByAnalyticBoxes(...)`
- `SelectStableAnalyticFloorProbes(...)`

### 2. Compute and log per-cascade relative corner phase

The next engine-side analysis should compute and log:

- exact `clip_min_corner_ls / page_world_size`
- integer `page_grid_origin`
- residual fractional phase

for the failing cascade levels.

Goal:

- prove or disprove the missing `relative corner offset` hypothesis

Most relevant code:

- [src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp)

### 3. Compare rasterized page coverage against expected world footprint

For the failing pages:

- `120`
- `376`
- `392`

the next analysis should inspect:

- page-local texel coverage
- projected world footprint
- sample texel location
- whether the observed shadow texels correspond to the expected box projection

Goal:

- decide whether the page contents are wrong because Stage 12 rasterization wrote the wrong place
- or because Stage 15 sampling reads the wrong place

Most relevant code:

- [src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli)
- [src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp)

### 4. Recheck whether zero-bias experiments are still appropriate

Current state in the worktree:

- directional raster depth bias is zero
- directional compare bias is zero

That was evidence-backed for the earlier single-cascade improvement, but it is not proven globally correct.

This should be revisited only after the probe classification and cascade-alignment questions above are settled.

Relevant files:

- [src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli)

## Recommended Resume Commands

Resume with these focused commands first:

```powershell
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe --gtest_filter=VsmShadowProjectionLiveSceneTest.DirectionalSingleCascadeLiveShellDarkensStableFarAnalyticShadowProbes --gtest_color=no
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe --gtest_filter=VsmShadowProjectionLiveSceneTest.DirectionalFourCascadeLiveShellMatchesAnalyticFloorClassificationAcrossDenseVisibleProbes --gtest_color=no
```

If a new engine-side fix is attempted, rerun:

```powershell
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowProjection.Tests.exe --gtest_color=no
out\build-ninja\bin\Debug\Oxygen.Renderer.VsmShadowRasterization.Tests.exe --gtest_color=no
out\build-ninja\bin\Debug\Oxygen.Renderer.VirtualShadowGpuLifecycle.Tests.exe --gtest_color=no
```

## Files Already Touched During This Investigation

These files were already modified in the worktree during the floor-shadow investigation:

- [src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmShadowRenderer.cpp)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmShadowHelpers.hlsli)
- [src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VsmShadowProjection_test.cpp)

Other VSM-related files were already dirty in the worktree before or during the broader refactor and should be handled carefully:

- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmDirectionalProjection.hlsl](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmDirectionalProjection.hlsl)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmLocalLightProjectionPerLight.hlsl](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmLocalLightProjectionPerLight.hlsl)
- [src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmProjectionData.hlsli](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Graphics/Direct3D12/Shaders/Renderer/Vsm/VsmProjectionData.hlsli)
- [src/Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmProjectionPass.cpp)
- [src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Passes/Vsm/VsmShadowRasterizerPass.cpp)
- [src/Oxygen/Renderer/Test/VirtualShadow/VirtualShadowLiveSceneHarness.h](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VirtualShadowLiveSceneHarness.h)
- [src/Oxygen/Renderer/Test/VirtualShadow/VsmProjectionRecordPublication_test.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/Test/VirtualShadow/VsmProjectionRecordPublication_test.cpp)
- [src/Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationPlanner.cpp](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmPageAllocationPlanner.cpp)
- [src/Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h](/H:/projects/DroidNet/projects/Oxygen.Engine/src/Oxygen/Renderer/VirtualShadowMaps/VsmProjectionTypes.h)

## Exit Gate

This investigation should not be marked complete until all of the following are true:

1. The real floor-shadow continuity bug is explained and fixed in code.
2. The dense four-cascade directional probe test is green for the right reason.
3. Broader VSM validation is rerun in `build-ninja`.
4. A real rendered floor shadow from the live directional scene no longer shows the page-aligned discontinuity.
