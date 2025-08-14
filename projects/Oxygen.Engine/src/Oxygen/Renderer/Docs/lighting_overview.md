# Lighting Overview

Purpose: define minimal lighting concepts, data formats, and integration points
that align with Oxygen's bindless rendering design.

Principles

- Bindless-first: all GPU buffers (lights, per-draw ranges, indices) are
  accessed via global bindless table indices; no new root-signature bindings
  beyond the existing draw index constant.
- Start simple: begin with a single directional light via constants, then move
  to a single bindless lights buffer for multiple lights; culling/clustering
  comes later.
- Stable, aligned layouts: use 32-byte aligned elements for structured buffers.

Light Types (initial)

- Directional: direction_ws (float3), color_rgb (float3), intensity (float),
  enabled (bool/uint)
- Point: position_ws (float3), radius (float), color_rgb (float3), intensity
  (float), enabled (bool/uint)
- Spot [deferred]

Renderer Integration

- Extraction collects enabled lights each frame.
- Phase 5B: SceneConstants carries one directional light and ambient.
- Phase 5C: Lights structured buffer SRV uploaded each frame; `num_lights` in
  SceneConstants; bindless slot documented.
- Phase 5D (optional): Per-draw ranges + indices buffers, both bindless SRVs.

Caps & Limits

- MAX_LIGHTS_UPLOADED: implementation-defined soft cap (e.g., 1024) for CPU
  array and GPU buffer size.
- MAX_LIGHTS_PER_PIXEL (shader loop cap): e.g., 64 for naive multi-light path.
- MAX_LIGHTS_PER_DRAW (optional per-draw clamp): e.g., 8–16.

Spaces & Units

- World-space inputs; shading in view or world as appropriate.
- Colors are linear; tone-mapped in post.
- Intensities are unitless initially; treat as scaling of radiance.

Testing

- Unit tests for extraction counts, buffer packing sizes/strides, and clamping.
- Visual sanity via example scene with 1 directional + a few point lights.
