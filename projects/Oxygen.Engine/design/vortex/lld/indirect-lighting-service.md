# Indirect lighting

Stage 13 adds environment diffuse and specular lighting to deferred surfaces.
Environment owns the source and processed products; IndirectLighting owns their
surface evaluation. Forward and translucent passes use the same evaluator.

Read: [execution](#execution), [resources](#resources),
[future families](#future-families). Algorithms and product contracts belong to
[captured-sky IBL](captured-sky-ibl.md); delivery and validation belong to
[VX-IBL-01](../milestones/VX-IBL-01/README.md).

## Execution

[`IndirectLightingService::Record`](../../../src/Oxygen/Vortex/IndirectLighting/IndirectLightingService.cpp)
receives `RenderContext`, the existing command recorder, `SceneTextures` and
published `EnvironmentFrameBindings`. It records one additive fullscreen draw
when authored lighting and complete IBL bindings are available. It preserves
SceneColor alpha, clamps the viewport/scissor to the view, and reads depth and
G-buffers without writing them.

[`DeferredIbl.hlsl`](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/IndirectLighting/DeferredIbl.hlsl)
skips background and non-default-lit surfaces. It reconstructs position, reads
the material/world normal, evaluates diffuse plus specular IBL and applies the
view's pre-exposure once. Products publish before both the forward base pass
and Stage 13. Stage 12 contains direct lighting only.

[`IblEvaluation.hlsli`](../../../src/Oxygen/Graphics/Direct3D12/Shaders/Vortex/Services/IndirectLighting/IblEvaluation.hlsli)
is shared by deferred, forward and translucent shaders. Independent diffuse,
specular and reflection controls apply there; incomplete or GPU-invalid metadata
produces zero IBL. Forward surfaces receive lighting in their own surface pass
and are not shaded again by the deferred apply.

The captured-sky contract owns SH normalization, GGX roughness mapping, BRDF
lookup, HDR scale and native-filtering qualification. No separate fallback
material model or visual-sky sampling path exists in this service.

## Resources

The C++ family lives under `src/Oxygen/Vortex/IndirectLighting/`. It owns the
apply framebuffer and pipeline description. Graphics caches pipeline state;
replaced framebuffers use Graphics deferred release. Record establishes the
SceneColor, depth, G-buffer and exposure-status resource states through the
existing recorder.

Environment owns per-scene product generations, source snapshots, scheduling,
BRDF lookup and capture leases. Existing publication and Graphics submission
lifetimes keep those resources alive for all views. IndirectLighting adds no
per-view sky history, product queue or duplicate resource pool.

## Validation

The [acceptance record](../milestones/VX-IBL-01/README.md#acceptance) links the
product, material, render-path and native/editor image checks. RenderDoc verifies
one Stage 13 apply and no Stage 12 ambient draw. Tests cover independent controls,
normal maps, sidedness, pre-exposure, valid/invalid metadata and view isolation.
Matched direct-light buffers remain unchanged when IBL is enabled.

## Future families

Broader reflections, GI, AO production and subsurface-adjacent indirect work
retain Stage 13 ownership. They require separate increments, resources and
qualification; they are not dependencies of sky IBL. Future AO is produced
before indirect apply, and reflection/history processing retains its own
internal owner. Temporal histories remain distinct by technique and view.

[VX-INDIRECT-01](../OPEN_ITEMS.md#p3--unscheduled-capabilities) owns two decisions:
where ScreenSpaceAO is published, and which reflection increment follows sky
lighting (SSR, captures or another bounded capability). Full GI, denoisers,
hair-specific branches and specialized reflection permutations remain within
[VX-FAMILY-01](../OPEN_ITEMS.md#p3--unscheduled-capabilities).
