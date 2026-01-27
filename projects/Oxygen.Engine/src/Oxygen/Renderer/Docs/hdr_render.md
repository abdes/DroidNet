# HDR, Lux, and Sun Intensity Notes (January 27, 2026)

## Context

We discussed why changing sun intensity (lux) did not visibly affect scene
brightness and why a zero sun intensity produced a white sky. The current
renderer is effectively LDR and does not apply tone mapping.

## Why the Scene Clips White

- The forward mesh pass applies exposure and immediately converts to sRGB, with
 no HDR render target or tone-mapping pass. Once values exceed 1.0 after
 exposure, they clamp and appear white.
- With manual EV at -2, moderate lux values still produce HDR radiance that
 exceeds 1.0 in linear space. Without tone mapping, objects saturate.

Key file references:

- [src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Forward/ForwardMesh_PS.hlsl]
 (linear lighting, then `LinearToSrgb`)
- [src/Oxygen/Renderer/Renderer.cpp]
 (exposure multiplier set per view)

## Real Engine Solution (No Demo Hacks)

To make lux meaningful and avoid clipping, the renderer must be HDR end-to-end:

1. Render scene lighting into an HDR render target (e.g., RGBA16F).
2. Apply exposure in linear HDR space.
3. Apply tone mapping (ACES/Reinhard) in a post-process pass.
4. Convert to sRGB only in the final output pass.
5. Route sky rendering through the same HDR + tone-map pipeline.

Until these steps are implemented, intensity changes will quickly clamp to
white once values exceed ~1.0 in linear space.

## Short-Term Fix Applied

We kept the renderer stable and **explicitly limited exposure support to
manual EV only**.

- Auto-exposure is disabled at runtime; it is not yet supported without HDR
 and tone mapping.
- Manual exposure compensation is applied only when explicitly enabled.

Relevant change:

- [src/Oxygen/Renderer/Renderer.cpp]: `UpdateViewExposure` now uses manual EV
 only and ignores auto mode.
- [src/Oxygen/Renderer/Renderer.h]: doc comment updated to state manual-only
 exposure support.

## Fix for White Sky at Zero Sun Intensity

When no sun is present, the sky shader previously used a white fallback sun
luminance, producing a white background even when sun intensity was zero.

Short-term fix:

- [src/Oxygen/Graphics/Direct3D12/Shaders/Passes/Sky/SkySphereCommon.hlsli]
 now sets fallback sun luminance to zero when no sun is active.

This avoids white sky output while keeping the rest of the pipeline unchanged.

## Notes

- This document captures the intent and the current limitations so we can
 revisit a full HDR + tone-mapping solution later.
