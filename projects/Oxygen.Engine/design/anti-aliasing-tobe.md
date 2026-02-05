# Anti-Aliasing — To Be

This document captures the AA discussion and a concrete recommendation for Oxygen's renderer: a concise comparison table, architecture principles and readiness, a definitive recommendation with rationale, detailed configurable presets, and a compact implementation-impact checklist.

## Summary comparison

| Technique | Perceived Quality | Performance Cost | Strengths | Weaknesses | Typical Scenarios |
|---|---:|---:|---|---|---|
| SSAA (Supersampling) | Excellent | Very High (O(N^2)) | Removes geometry & texture aliasing, simple concept | Extremely expensive, large memory/bandwidth | Offline renders, screenshots, ultra-high-quality presets |
| MSAA (Multi-Sample AA) | High for geometry edges | Moderate — linear with sample count | Hardware support, good edge AA, cheap pixel shader cost | Doesn't handle shader/texture aliasing, costly with many RTs (deferred/G-buffers) | Forward renderers, alpha-tested geometry, VR (small sample counts) |
| MSAA + HW Resolve | Same as MSAA | Moderate; fast HW resolve | Fast hardware resolve to single-sampled target; minimal shader change | Still doesn't fix shader aliasing; swapchain often single-sampled (requires resolve) | Engines with single-sampled swapchains that want MSAA quality |
| CSAA / Vendor variants | Better than same-count MSAA | Moderate (vendor-dependent) | Better quality/cost tradeoffs on supporting GPUs | Vendor-specific, portability issues | Vendor-optimized paths on PC/console only |
| TAA (Temporal AA) | Very high (stable, fine detail) | Moderate | Handles shader/texture aliasing, good for deferred pipelines | Ghosting, history management complexity, needs motion vectors & jitter | Modern deferred AAA engines; default desktop/console AA |
| SMAA (Subpixel Morphological AA) | Good | Low–Moderate | High-quality post-process, sharper than FXAA | Fails on geometric aliasing; no temporal smoothing alone | When MSAA impractical; post-AA for better quality than FXAA |
| FXAA (Fast Approximate AA) | Low–Moderate | Very Low | Extremely fast, simple integration | Blurs detail, less accurate | Mobile, low-end presets, fallback option |
| Shader/HW custom resolve | Variable | Variable (often higher than HW resolve) | Flexible filters, alpha-aware resolves | More shader cost; slower than HW resolve in many cases | Special effects or custom resolve/filters |
| ML Upscale + AA (DLSS/FSR) | Very high perceived | Variable (can be efficient) | Great quality/cost tradeoffs via upscaling; improves effective resolution | Requires extra infra/vendor tech; potential artifacts | High-performance quality mode; upscaling targets |

## Oxygen rendering architecture — principles & readiness

- Forward-first renderer: rendering pipeline is forward-oriented, which maps well to MSAA usage for geometry and transparent passes.
- AA knobs exposed in core types: `TextureDesc` exposes `sample_count` and `sample_quality` and PSO/framebuffer descriptors include `sample_count` (the pipeline rebuilds when sample_count changes). This indicates first-class support for multisampled resources across passes.
- Backend readiness: the D3D12 backend converts `TextureDesc.sample_count` into `D3D12_RESOURCE_DESC.SampleDesc` and supports MSAA view types (RTV/DSV/SRV for MS textures). Swapchain code currently creates single-sampled backbuffers (SampleDesc {1,0}).
- Composition pipeline: passes (depth, shader, compositing) honor `sample_count` and will rebuild PSOs accordingly; compositor can draw into multisampled or single-sample targets.
- Practical implication: Oxygen is architecturally ready for MSAA as a primary AA method without a major redesign; the main missing piece is wiring a resolve step from MS targets to the single-sampled presentable image.

## Definitive recommendation for Oxygen (concise)

Primary strategy (default):
- Use MSAA 4x on the forward rendering path (color + matching depth) and perform a hardware resolve to the single-sampled swapchain backbuffer before present. MSAA 4x gives correct geometric edge AA and handles alpha-tested geometry well at moderate cost.

Quality overlay (optional):
- Offer an optional TAA overlay for the high-quality preset to reduce shader/texture aliasing and improve temporal stability on high-end targets. TAA should be optional because it adds complexity (history, motion vectors) and can introduce ghosting.

Rationale:
- Matches Oxygen's forward pipeline and existing `sample_count` plumbing, requires only small backend additions (resolve wrapper) and per-view texture allocation changes, and gives predictable, artifact-minimal results for geometry/alpha-tested content. Combined with optional TAA, it covers shader/texture aliasing without converting the renderer to deferred or changing the pipeline architecture.

## Configurable presets (detailed)

- Performance (low-end):
	- MSAA: off
	- Post-AA: FXAA
	- Use: mobile, integrated GPUs, lowest-quality preset

- Balanced (recommended default):
	- MSAA: 4x for forward/color & depth targets
	- Post-AA: off (or SMAA if slight additional sharpening desired)
	- Use: typical desktop & console targets prioritizing visual quality and speed

- Quality (high-quality preset):
	- MSAA: 4x
	- Temporal: TAA enabled (history + jitter) for shader/texture aliasing
	- Post-AA: optional SMAA on top of TAA for sharpening
	- Use: high-end PC, filmic/photoreal presets

- Ultra (very high-end):
	- MSAA: 8x (hardware/perf permitting)
	- Temporal: TAA + history sharpening
	- Optional ML upscaling (DLSS/FSR) for higher effective resolution

- VR / Latency mode:
	- MSAA: 2x–4x on forward path (prefer small MSAA over TAA)
	- TAA: disabled unless latency/ghosting mitigations are validated

- Deferred-fallback (if deferred path added later):
	- MSAA: avoid for main G-buffer (too costly); keep MSAA only on forward/transparent passes
	- Primary AA: TAA + SMAA postfilter

Each preset should be available at runtime via a renderer config and validated by a short quality/perf profile test.

## Implementation impact (very concise)

Minimal code changes required (no architecture rewrite):

1. When allocating per-view render targets, set `TextureDesc.sample_count` to the configured sample count (e.g., 4) for color and depth textures. Ensure `depth.sample_count == color.sample_count`.
2. Add or use a backend D3D12 resolve helper that issues `ResolveSubresource` (or equivalent) from MSAA render target -> single-sampled present target (or an intermediate single-sampled texture). Prefer hardware resolve for performance.
3. Ensure PSO/framebuffer creation uses `FramebufferLayout.sample_count` from attached textures (already present in passes). PSO rebuild logic already checks sample_count — confirm unit tests/logging for rebuild.
4. Update compositing/present path to resolve MSAA targets before Present; compositor currently writes to a target framebuffer — ensure the final step resolves to the swapchain surface if that surface is single-sampled.
5. Add runtime config/presets and a small UI toggle to change sample_count and AA mode; ensure safe dynamic switching by rebuilding framebuffers/PSOs and recreating views when config changes.

Testing & validation notes (concise):
- Visual tests: alpha-tested geometry, thin wireframe-like geometry, moving camera for TAA ghosting checks.
- Perf tests: measure fillrate and memory bandwidth impact when switching MSAA 4x on/off across target GPUs.
- Edge-cases: verify shader SRVs/UAVs and compositing with MSAA textures (some SRV/UAV operations incompatible with MS textures — avoid or use resolves where necessary).

## Next steps (suggested)

1. Instrument a minimal proof-of-concept: allocate a 4x MSAA color+depth per-view, render a simple scene, perform `ResolveSubresource` to the swapchain's single-sample backbuffer, and present. Keep the change isolated to a debug path for validation.
2. Add runtime presets and automated sample_count toggles for quick validation across hardware.
3. If TAA overlay desired, plan a small follow-up: motion-vector output, history buffering, jittered projection, reprojection pass.

---

Document prepared from the codebase review and discussion; if you want, I can create the POC patch (small D3D12 resolve call and sample_count wiring) and tests next.
