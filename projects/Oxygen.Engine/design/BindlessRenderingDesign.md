# Bindless Rendering Design

## Purpose

This document describes how the renderer uses the bindless architecture in practice.

It focuses on:

- renderer-facing allocation choices
- frame/resource publication rules
- shader-visible index expectations
- stable residency and replacement behavior

It does **not** describe migration steps or implementation phases.

## Renderer-Level Principle

The renderer does not choose between bindless and raw allocation based on the resource class alone. It chooses based on the **shader contract**.

- If shaders treat the value as a member of a generated semantic domain, the renderer must allocate it with `AllocateBindless(...)`.
- If shaders treat the value as an explicit pass-local descriptor index, the renderer must allocate it with `AllocateRaw(...)`.

This is the single most important rule for avoiding black frames, broken overlays, missing shadows, and cross-domain index misuse.

## Renderer Bindless Domains

The renderer currently relies on these semantic domains from the authored bindless metadata:

- **Global SRV domain** — shared structured-buffer SRVs and selected semantic texture SRVs consumed through global-domain shader helpers
- **Materials domain** — material and draw-metadata buffers
- **Textures domain** — texture binder outputs and texture SRVs consumed as texture-domain resources
- **Samplers domain** — global bindless samplers

The exact capacities and bases come from generated metadata. Renderer code must not hardcode or infer them independently.

## Runtime Flow

A typical renderer-side flow is:

1. create or locate a resource
2. allocate a descriptor using the semantic or raw path
3. register the view in `ResourceRegistry`
4. publish the resulting `ShaderVisibleIndex` into renderer-owned structures
5. consume that published index in shaders through generated helpers or explicit pass bindings

```mermaid
flowchart LR
  A[Renderer system] --> B{Semantic domain?}
  B -->|Yes| C[AllocateBindless(domain, view_type)]
  B -->|No| D[AllocateRaw(view_type, visibility)]
  C --> E[ResourceRegistry.RegisterView]
  D --> E
  E --> F[GetShaderVisibleIndex(handle)]
  F --> G[Publish into frame/material/pass data]
  G --> H[Shader consumes published index]
```

## Scene Preparation and Stable Geometry/Material Identity

Scene preparation resolves stable renderer-facing identities before draw emission.

### Geometry

`GeometryUploader` interns geometry by `(AssetKey, lod_index)`.

For each unique geometry identity, the renderer typically publishes:

- one global-domain structured SRV for the vertex buffer
- one global-domain structured SRV for the index buffer when indexed

This is persistent semantic demand, not pass-local scratch demand.

Practical consequence:

- a scene with many unique indexed meshes will consume roughly two global-domain SRVs per unique geometry identity, plus engine overhead
- this is normal behavior under the current design

### Materials

`MaterialBinder` and related metadata publishers allocate semantic material/global descriptors because the published indices are consumed as stable shader-facing resources, not as pass-local scratch descriptors.

### Textures

`TextureBinder` owns semantic texture-domain SRVs. Placeholder, error, and resolved live textures all live in the textures domain because they preserve stable texture-facing shader contracts.

## Bindless Publishing Rules

When renderer code publishes an index into a shader-facing structure, that structure must already contain the final `ShaderVisibleIndex` expected by shaders.

### Publish bindless indices for

- texture binder results used as semantic textures
- structured buffers exposed through frame bindings or global/material buffers
- material atlases and metadata buffers
- semantic resources validated by generated macros such as global-domain checks

### Publish raw indices for

- pass constants
- pass-local source/destination descriptors
- temporary SRV/UAV pairs used only inside a pass contract
- RTV/DSV-backed explicit bindings

## Shader-Side Contract

Shaders consume indices in two distinct ways.

### 1. Semantic bindless domain access

These indices are interpreted as belonging to a generated domain.

Examples:

- helpers/macros that validate membership in the global domain
- arrays or descriptor-heap accesses where the published index is semantically part of the engine's bindless ABI

These indices must come from `AllocateBindless(...)` in the correct domain.

### 2. Explicit pass-local descriptor access

These indices are interpreted only within the pass that authored them.

Examples:

- scene depth SRV for a specific pass
- histogram or exposure buffers for post-processing
- temporary UAV/RTV/DSV descriptors

These indices may come from `AllocateRaw(...)`.

## Classification Guide by Subsystem

### Semantic bindless subsystems

The following renderer patterns are semantic bindless consumers:

- texture binder outputs in the textures domain
- transform/global metadata/material atlas structured buffers
- environment/global structured-buffer publications
- light-culling shared structured SRVs
- VSM projection/request structured SRVs
- semantic shadow resources whose HLSL consumes them through global-domain helpers
- global samplers

### Intentional raw subsystems

The following renderer patterns remain explicitly raw:

- framebuffer RTV/DSV allocation
- shadow-map DSV allocation
- pass constants CBVs
- compositing / tone-map / auto-exposure / HZB temporary SRV/UAV/RTV resources
- atmosphere / sky capture / IBL temporary pass resources when consumed as explicit pass-local bindings
- debug-only scratch descriptors that are not part of a generated semantic domain

## Residency and Replacement Rules

### Stable semantic identity

If the semantic identity is unchanged, replacement should preserve the descriptor slot whenever supported.

This is why `ResourceRegistry::Replace(...)` matters:

- the resource instance can change
- the registered view can be recreated
- the published shader-facing index stays stable

### Publication timing

Publish semantic indices only when the underlying resource/view is ready for shader consumption.

For upload-driven systems such as geometry:

- allocate/register the semantic descriptor once per semantic identity
- keep the slot stable across resize/replacement
- publish readiness only after the upload succeeds

### Release timing

Release semantic bindless descriptors only when the semantic owner is truly gone:

- resource eviction
- explicit unregister
- owner destruction

They are not per-frame descriptors.

## Capacity Guidance for Renderer Authors

Renderer authors must treat bindless domain capacity as a first-class contract.

### What consumes real capacity

The following consume long-lived semantic capacity:

- unique geometry identities
- persistent material/metadata buffers
- resident texture identities
- environment and lighting structured buffers
- semantic shadow and frame-wide publications

### What should not drive bindless capacity

The following should stay on the raw path and therefore should not force semantic domain growth on their own:

- transient post-process descriptors
- one-frame scratch UAVs
- RTV/DSV churn
- pass-local CBV/SRV/UAV descriptors

### Production guidance

If legitimate content scale exceeds a domain:

- increase authored domain capacity in metadata, or
- redesign semantic ownership to reduce persistent demand

Do **not** bypass domain ownership to make the scene fit.

## Common Failure Modes

### Misclassifying a semantic resource as raw

Symptoms:

- scene renders black
- shadows disappear
- generated shader-domain guards reject the published index
- some passes work while others fail because only the semantic consumers break

Root cause:

- the CPU published a raw absolute descriptor index where shaders expected a semantic domain index

### Misclassifying a raw resource as bindless

Symptoms:

- unnecessary persistent semantic pressure on a bindless domain
- domain exhaustion in scenes that should only have transient pass-local demand
- more complicated lifetime management than the pass needs

Root cause:

- a pass-local descriptor was incorrectly promoted into semantic bindless ownership

### Publishing backend slot ids instead of shader-visible indices

Symptoms:

- intermittent or systematic wrong-resource sampling
- domain checks fail even though the allocation looked valid on the CPU side

Root cause:

- runtime confused `HeapIndex` with `ShaderVisibleIndex`

## Review Checklist for Allocation Sites

When reviewing a renderer allocation site, answer these questions in order:

1. What shader reads this descriptor?
2. Does that shader treat the value as belonging to a generated semantic domain?
3. Is the descriptor persistent across frames or tied to a stable semantic identity?
4. Is the resource/view replaced in place and expected to keep the same published slot?
5. Is this descriptor only an implementation detail of one pass?

Decision rule:

- if the answer to 2 is yes, use `AllocateBindless(...)`
- otherwise, use `AllocateRaw(...)`

## Developer Guidance

- Prefer semantic bindless allocation only when the shader contract requires semantic ownership.
- Keep pass-local descriptors raw, even when they are shader-visible.
- Publish `ShaderVisibleIndex`, never allocator-local slot ids.
- Let generated metadata define domains and ranges.
- Treat scene-scale bindless demand as an authored-capacity concern.
- Re-check the HLSL before changing classification at the CPU site.

## Related Documents

- `design/BindlessArchitecture.md` — system-wide architecture and contracts
- `src/Oxygen/Renderer/Docs/bindless_conventions.md` — renderer-specific conventions
- `src/Oxygen/Renderer/Docs/Upload/geometry_uploader.md` — geometry residency and publication behavior
- `src/Oxygen/Renderer/Docs/Upload/material_binder.md` — material-domain publication behavior
- `src/Oxygen/Renderer/Docs/Upload/texture-binder.md` — texture-domain publication behavior
