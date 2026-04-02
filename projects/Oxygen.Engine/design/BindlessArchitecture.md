# Bindless Architecture

## Purpose

Oxygen uses a domain-driven bindless model. The design goal is simple:

- semantic bindless resources are allocated by **domain**
- the returned shader-visible index always lies inside that domain's generated range
- raw descriptors remain explicit and separate from bindless ownership

This document describes the current architecture, contracts, API shapes, and developer guidance. It does **not** describe migration steps or implementation phases.

## Scope

This architecture covers:

- the authored bindless metadata model
- generated ABI and backend-realization artifacts
- runtime allocation APIs
- resource/view registration contracts
- backend realization rules for D3D12 and Vulkan
- renderer and Nexus integration points

It does not cover:

- feature-roadmap planning
- implementation sequencing
- temporary migration compatibility layers

## Architectural Model

The system is intentionally split into three layers:

1. **Meta** — authored source of truth in `src/Oxygen/Core/Meta/Bindless.yaml`
2. **Generated** — machine-generated ABI and backend layout artifacts
3. **Runtime** — allocator, registry, renderer, and backend code that consume only generated outputs

Runtime code must not invent bindless domains, reinterpret ranges, or derive semantic layout from ad-hoc allocator state.

## Core Design Rules

1. **Domain ownership is explicit.** Bindless allocation starts with `DomainToken`.
2. **Raw descriptors are explicit.** RTV/DSV and pass-local descriptors use the raw path.
3. **Shader-visible indices are semantic outputs.** They are not backend slot ids.
4. **Backend storage and shader indexing share one local slot.** This is what makes domain correctness hold by construction.
5. **Generated metadata owns layout.** Runtime consumes it; it does not redefine it.

## Identity Model

Three identities must remain distinct.

### `bindless::DomainToken`

The semantic owner.

- backend-neutral
- generated from bindless metadata
- used to select the bindless domain for allocation and registration

### `bindless::HeapIndex`

The stable backend slot identity.

- used for descriptor ownership and release
- used by allocator/backend lifetime tracking
- not directly exposed to shaders as a semantic contract

### `bindless::ShaderVisibleIndex`

The shader access identity.

- consumed by HLSL / GLSL / generated helpers
- must lie inside the generated range of the owning domain
- derived from the domain-local slot, not from arbitrary heap math at call sites

## Public Allocation Surfaces

The runtime surface is intentionally split in two.

### Semantic bindless allocation

```cpp
auto AllocateBindless(bindless::DomainToken domain,
  ResourceViewType view_type) -> BindlessHandle;
```

Use this when the descriptor becomes part of the engine's semantic bindless ABI.

Examples:

- global structured-buffer SRVs consumed through generated global-domain helpers
- material/metadata buffer SRVs in the materials domain
- texture SRVs in the textures domain
- global samplers in the samplers domain

### Explicit raw allocation

```cpp
auto AllocateRaw(ResourceViewType view_type,
  DescriptorVisibility visibility) -> RawDescriptorHandle;
```

Use this when the descriptor is not semantically owned by a generated bindless domain.

Examples:

- RTV / DSV
- pass-local CBV / SRV / UAV descriptors
- temporary post-processing descriptors
- explicit absolute-index descriptors that are not guarded by generated domain macros

### Shared handle type

The backend slot carrier is `DescriptorAllocationHandle`, with semantic aliases:

- `BindlessHandle`
- `RawDescriptorHandle`

The handle carries:

- allocator back-reference
- stable backend slot (`HeapIndex`)
- view type
- visibility when relevant for raw descriptors
- domain ownership when the handle is bindless

The important rule is not the typedef; it is the semantic split between bindless and raw allocation.

## Current Domain Model

The authoritative layout lives in `Bindless.yaml`. The current production-oriented authored domains are:

- `srv_global` — global structured-buffer SRVs
- `materials` — material and metadata SRVs
- `textures` — texture SRVs
- `samplers` — sampler table

The source of truth for capacities, shader index bases, and backend realizations is the generated metadata pipeline, not hand-maintained prose in this document.

## Source of Truth and Generated Outputs

### Authored source

`src/Oxygen/Core/Meta/Bindless.yaml` defines:

- index spaces
- domains
- capacities and shader index bases
- allowed view types per domain
- backend realization data for D3D12 and Vulkan

### Generated outputs

The generator produces the contract consumed by runtime:

- `Generated.BindlessAbi.h`
- `Generated.BindlessAbi.hlsl`
- `Generated.Meta.h`
- backend strategy JSON files
- backend helper headers such as generated D3D12/Vulkan layout artifacts

These generated outputs define:

- `DomainToken` constants
- generated domain descriptors
- shader-visible bases and capacities
- HLSL validation helpers
- backend realization bases for D3D12 and Vulkan

## Backend Realization Contract

The authored ABI is backend-neutral. Backend code realizes it differently, but must preserve the same semantic ranges.

### Shared invariant

For a domain-local slot `n` in domain `D`:

- backend storage location is derived from `D`'s backend realization plus `n`
- shader-visible index is derived from `D`'s shader index base plus `n`

Both mappings use the same local slot `n`.

That invariant is the reason domain-correct indexing holds.

### D3D12 realization

D3D12 maps domains into descriptor tables backed by shader-visible heaps.

For a local slot `n`:

- backend heap location = `domain_realization.heap_local_base + n`
- shader-visible index = `domain.shader_index_base + n`

D3D12-specific details such as root-signature tables, heap-local bases, and descriptor-heap types are realization details generated from metadata. They do not redefine semantic ownership.

### Vulkan realization

Vulkan maps domains into descriptor-set bindings and array-element bases.

For a local slot `n`:

- realized binding element = `domain_realization.array_element_base + n`
- shader-visible index = `domain.shader_index_base + n`

Again, semantic ownership comes from the authored domain, not from backend-specific binding numbers.

## Resource Registration Contract

`ResourceRegistry` is the central resource/view registration layer.

Its job is to:

- hold strong references to registered resources
- own registered descriptor handles for views
- cache view objects by resource + description
- preserve stable bindless indices during supported replace/update flows

### Important registry rules

- a registered view must be backed by a valid descriptor handle
- duplicate resource registration is a contract violation
- duplicate view registration is a contract violation
- `Replace(...)` preserves descriptor identity when the view is recreated in place
- unregistering a resource releases its registered view descriptors

This is why resize and replacement flows can keep shader-visible indices stable when the semantic identity is unchanged.

## Nexus Contract

Nexus remains aligned with the same identity split.

- semantic ownership is keyed by generated domain identity
- lifetime/versioning is keyed by stable backend slot identity

In current code, `nexus::DomainKey` remains as a thin wrapper surface over generated `DomainToken`. The architectural rule is still the same:

- domain selection is semantic
- slot reuse/versioning is backend-slot-based

Nexus must not infer semantic ownership from `(ResourceViewType, DescriptorVisibility)`.

## Renderer Contract

Renderer systems must classify descriptor allocations by shader contract, not by convenience.

### Use bindless allocation when

- the shader expects the index to belong to a generated semantic domain
- the slot is published into frame/global/material structures that are interpreted through generated bindless helpers
- stability of the published semantic slot matters across resource replacement

### Use raw allocation when

- the descriptor is pass-local
- the descriptor is an RTV or DSV
- the descriptor exists only to support a temporary render path or compute pass
- the shader consumes the descriptor as an explicit absolute index that is not validated as part of a generated domain contract

## Raw vs Bindless: Practical Guidance

A useful test is:

> Does the shader-side contract care which generated domain this descriptor belongs to?

If yes, use `AllocateBindless(...)`.

If no, and the descriptor is just an explicit rendering primitive, use `AllocateRaw(...)`.

### Typical bindless cases

- texture atlas / texture binder outputs in the textures domain
- material and draw metadata buffers in the materials domain
- global structured buffers published for frame-wide shader access
- semantic texture SRVs that shaders validate through generated global-domain helpers

### Typical raw cases

- shadow-map DSVs
- HDR/LDR post-process RTVs
- pass constants CBVs
- pass-local source/destination SRV/UAV pairs
- one-off debug or scratch descriptors

## Shader Contract

Shaders consume generated bindless metadata, not allocator implementation details.

That means:

- generated HLSL constants and helpers are authoritative
- domain-guard macros and helper functions define whether an index belongs to a semantic domain
- shader-visible indices written into frame/material structures must already be in the correct semantic range

The CPU side must therefore publish the **shader-visible index**, not the backend slot id.

## Stable Residency and Replacement

Bindless indexing only works if semantic identities remain stable across replacement.

Required behavior:

- resizing or replacing a resource for the same semantic identity should preserve the descriptor slot when the architecture supports it
- descriptor release must happen when the semantic owner is unregistered or evicted
- allocators must fail fast on true exhaustion
- large-scene capacity sizing must be treated as a content-scale contract, not an incidental implementation detail

## Capacity Guidance

Bindless domain capacities are part of the authored metadata contract.

Developer guidance:

- size domains for **resident semantic demand**, not only for per-frame transient demand
- the global SRV domain must account for geometry, transforms, metadata, environment/lighting buffers, and other engine-published structured SRVs
- if a scene legitimately contains more unique semantic resources than a domain can hold, the correct fix is to adjust authored capacity or change the semantic ownership model — not to bypass domain ownership

## Anti-Patterns

The following are architectural errors:

- allocating semantic bindless resources through the raw API
- deriving semantic ownership from `ResourceViewType + DescriptorVisibility`
- treating shader-visible index and backend slot as interchangeable
- publishing backend slot ids into shader-facing structures
- inventing domain ranges in runtime code instead of consuming generated metadata
- keeping implementation-plan or migration content in architecture docs

## Developer Checklist

When adding or reviewing a descriptor allocation site:

1. Identify the shader consumer.
2. Determine whether the consumer expects a generated semantic domain.
3. Use `AllocateBindless(domain, view_type)` for semantic bindless ownership.
4. Use `AllocateRaw(view_type, visibility)` for explicit raw descriptors.
5. Publish `ShaderVisibleIndex` to shader-facing structures.
6. Keep replacement flows index-stable when semantic identity is unchanged.
7. Update authored metadata if legitimate resident demand changes domain sizing.

## Related Documents

- `design/BindlessRenderingDesign.md` — renderer-facing bindless usage and shader contracts
- `src/Oxygen/Graphics/Common/README.md` — common allocator and registry usage notes
- `src/Oxygen/Nexus/Docs/design.md` — Nexus domain/range/reuse model
- `src/Oxygen/Renderer/Docs/bindless_conventions.md` — renderer allocation-site conventions
