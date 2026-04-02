# Domain-Consistent Bindless Allocation Design

## Status

- Status: phases 1-2 implemented; later migration phases pending
- Scope: bindless allocation, generated bindless metadata, backend realization, and Nexus keying
- Compatibility: intentionally breaking; the goal is a cleaner API and a stricter contract, not legacy preservation
- Validation status: authored-model reset plus generated-contract reset validated via BindlessCodeGen pytest, CLI verbose dry-run, the MSVC compile-check behind `oxygen-core_bindless_gen`, runtime consumer builds (`oxygen-core`, `oxygen-graphics-direct3d12`, `oxygen-renderer`), and targeted test execution; runtime API/backend realization migration phases remain pending

## Purpose

This design exists to make one guarantee true across the engine:

- if a runtime system allocates a bindless resource in domain `D`, the shader-visible index returned for that allocation is always inside domain `D`'s generated range within `D`'s generated index space

Everything else in this document is in service of that guarantee.

More concretely, this design enables:

- domain-correct shader-visible indices
- a strict and readable `meta -> generated -> runtime` contract
- one consistent identity model from backend allocation up to Nexus
- domain-first APIs that fit Oxygen's strong-type and boundary-discipline patterns

## Exact Problem Being Fixed

The current failure is precise.

Today the allocator can return:

- a stable backend slot
- in the correct physical heap range
- for the requested view type / visibility

That part is not the bug.

The bug is this:

- some allocations are semantically owned by a generated bindless domain
- the runtime does not constrain those allocations to the owning domain's realized subrange
- the resulting shader-visible index is physically valid for the backend storage, but logically invalid for the domain
- shader-side domain guards reject that index, which is exactly what happened in the lighting flicker captures

The core issue is therefore not "allocation by view type and visibility" in the abstract.

The core issue is:

- bindless-facing stable indices are not guaranteed to lie inside the range of their owning generated domain and generated index space

That is what this design fixes.

## Design Outcome

After this refactor:

- domain ownership is established before a bindless slot is assigned
- backend realization allocates from the owning domain's suballocator, not from a coarse heap bucket
- both the backend slot and the shader-visible index are derived from the same domain-local slot
- no runtime layer invents or infers bindless ranges outside generated metadata

## Architectural Direction

The design is built around three layers with a strict one-way flow:

1. `meta`
2. `generated`
3. `runtime`

Runtime is not allowed to reinterpret authored layout semantics on its own.

### 1. Meta

The authored spec is the only source of truth.

It defines:

- the backend-neutral bindless ABI
- each backend-specific realization of that ABI

### 2. Generated

The generator turns the authored spec into:

- typed generic bindless ABI artifacts for C++ and HLSL
- backend-specific strategy JSON artifacts
- backend-specific typed helper artifacts where useful

Generated artifacts are the only inputs runtime code may use for bindless layout.

### 3. Runtime

Runtime consumes generated artifacts only.

It is responsible for:

- realizing backend storage
- suballocating by generated domain
- creating backend descriptors/views
- producing handles and shader-visible indices

Runtime is not allowed to:

- invent domains
- derive domain ranges from allocator state
- reinterpret backend heap ranges as if they were the semantic bindless ABI

## The Three Identities

The current codebase blurs several different notions of "index". This design separates them explicitly.

### 1. `DomainToken`

This is the semantic owner identity.

- generated from the spec
- backend-neutral
- used by renderer systems, resource registration, and Nexus domain selection

### 2. `BindlessHeapIndex`

This is the stable backend slot identity.

- backend-specific storage slot
- stable for descriptor lifetime
- used for backend ownership, release, and generation tracking

This is not the shader-visible index and not the semantic domain.

### 3. `ShaderVisibleIndex`

This is the shader access identity.

- consumed by shaders
- must always be inside the generated range of the owning domain

This is not the backend slot key.

### Consequence

No layer may conflate these identities.

In particular:

- domain selection must use `DomainToken`
- lifetime/reuse must use `BindlessHeapIndex`
- shader access must use `ShaderVisibleIndex`

## Stable Handle Model

The bindless handle model should make those identities explicit.

```cpp
namespace oxygen::bindless {

namespace detail {
using DomainTokenBase = NamedType<uint16_t, struct DomainTokenTag,
  Comparable, Hashable, Printable>;
} // namespace detail

struct DomainToken : detail::DomainTokenBase {
  using Base = detail::DomainTokenBase;
  using Base::Base;

  static constexpr uint16_t kInvalidValue = 0xFFFFu;

  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool
  {
    return get() != kInvalidValue;
  }
};

static_assert(sizeof(DomainToken) == sizeof(uint16_t));
static_assert(alignof(DomainToken) == alignof(uint16_t));

} // namespace oxygen::bindless
```

The runtime handle should then be domain-first:

```cpp
class BindlessHandle {
public:
  [[nodiscard]] constexpr auto IsValid() const noexcept -> bool;
  [[nodiscard]] constexpr auto GetDomain() const noexcept
    -> bindless::DomainToken;
  [[nodiscard]] constexpr auto GetSlot() const noexcept
    -> bindless::HeapIndex;
  [[nodiscard]] constexpr auto GetViewType() const noexcept
    -> graphics::ResourceViewType;

private:
  BindlessAllocator* allocator_ { nullptr };
  bindless::DomainToken domain_ {};
  bindless::HeapIndex slot_ { kInvalidBindlessHeapIndex };
  graphics::ResourceViewType view_type_ { graphics::ResourceViewType::kNone };
};
```

Why this shape:

- `domain_` carries semantic ownership explicitly
- `slot_` carries the stable backend slot used for release and versioning
- `view_type_` remains valuable for validation and diagnostics
- visibility is intentionally absent because it belongs to backend realization metadata, not to the semantic handle contract

`DescriptorHandle` may still exist for raw/non-bindless descriptor APIs if needed. Bindless code should stop using it as its primary handle type.

## Consistent Keying Up To Nexus

The domain and lifetime model must stay consistent across layers.

### Backend and allocator

- allocate by `DomainToken`
- return `BindlessHandle { domain, slot, view_type }`

### Renderer systems and registry

- request allocation/registration by `DomainToken`
- store or pass `BindlessHandle`
- ask allocator for `ShaderVisibleIndex` only when needed for shader binding

### Nexus

Nexus should stop using allocator-era domain identity.

The clean model is:

- `DomainKey == bindless::DomainToken`
- `VersionedBindlessHandle` continues to version the stable backend slot (`BindlessHeapIndex`)

That gives one consistent story:

- semantic ownership is keyed by generated domain token
- lifetime and reuse are keyed by stable backend slot

This removes the need to reinterpret domains through `(ResourceViewType, DescriptorVisibility)`.

It also means any domain mapper in Nexus should be generated-layout-driven, not allocator-state-driven.

## Meta: What The Spec Must Express

The spec must cleanly separate:

- backend-neutral bindless ABI
- backend-specific realization

### Backend-neutral ABI

This is shared across D3D12 and Vulkan.

It defines:

- domain ids
- generated token order
- index-space id per domain
- shader-visible range base per domain
- capacity per domain
- allowed `ResourceViewType` values per domain
- shader access class per domain

This is the contract the engine and shaders rely on.

### Backend-specific realization

This is how one backend realizes the shared ABI.

It is intentionally different for D3D12 and Vulkan.

- D3D12 realization uses heaps, descriptor tables, heap-local bases, root signature
- Vulkan realization uses descriptor sets, bindings, array-element bases, pipeline layout

The engine must not force one backend's physical model onto the other.

## Proposed YAML Shape

The exact schema spelling can evolve, but the separation should look like this:

```yaml
meta:
  version: "2.0.0"

defaults:
  invalid_index: 4294967295

abi:
  index_spaces:
    - id: srv_uav_cbv
    - id: sampler

  domains:
    - id: lighting_frame
      index_space: srv_uav_cbv
      shader_index_base: 1
      capacity: 2048
      shader_access_class: buffer_srv
      view_types: [StructuredBuffer_SRV]

    - id: materials
      index_space: srv_uav_cbv
      shader_index_base: 2049
      capacity: 3047
      shader_access_class: buffer_srv
      view_types: [StructuredBuffer_SRV, RawBuffer_SRV]

    - id: textures
      index_space: srv_uav_cbv
      shader_index_base: 5096
      capacity: 65536
      shader_access_class: texture_srv
      view_types: [Texture_SRV]

    - id: samplers
      index_space: sampler
      shader_index_base: 0
      capacity: 256
      shader_access_class: sampler
      view_types: [Sampler]

backends:
  d3d12:
    strategy:
      heaps:
        - id: cbv_srv_uav_gpu
          type: CBV_SRV_UAV
          visibility: shader_visible
          base_index: 1000000
          capacity: 1000000

        - id: sampler_gpu
          type: SAMPLER
          visibility: shader_visible
          base_index: 2002048
          capacity: 2048

      tables:
        - id: bindless_srv
          descriptor_kind: SRV
          heap: cbv_srv_uav_gpu
          shader_register: t0
          register_space: space0
          unbounded: true

        - id: bindless_sampler
          descriptor_kind: SAMPLER
          heap: sampler_gpu
          shader_register: s0
          register_space: space0
          descriptor_count: 256

      domain_realizations:
        - domain: lighting_frame
          table: bindless_srv
          heap_local_base: 1

        - domain: materials
          table: bindless_srv
          heap_local_base: 2049

        - domain: textures
          table: bindless_srv
          heap_local_base: 5096

        - domain: samplers
          table: bindless_sampler
          heap_local_base: 0

    root_signature:
      - type: descriptor_table
        id: BindlessSrvTable
        table: bindless_srv
        index: 0
        visibility: ALL

      - type: descriptor_table
        id: SamplerTable
        table: bindless_sampler
        index: 1
        visibility: ALL

      - type: cbv
        id: ViewConstants
        shader_register: b1
        register_space: space0
        index: 2
        visibility: ALL

      - type: root_constants
        id: RootConstants
        shader_register: b2
        register_space: space0
        num_32bit_values: 2
        index: 3
        visibility: ALL

  vulkan:
    strategy:
      descriptor_sets:
        - id: bindless_main
          set: 0

      bindings:
        - id: textures_binding
          set: bindless_main
          binding: 0
          descriptor_type: SAMPLED_IMAGE
          variable_count: true
          update_after_bind: true

        - id: global_buffers_binding
          set: bindless_main
          binding: 1
          descriptor_type: STORAGE_BUFFER
          variable_count: true
          update_after_bind: true

        - id: samplers_binding
          set: bindless_main
          binding: 2
          descriptor_type: SAMPLER

      domain_realizations:
        - domain: textures
          binding: textures_binding
          array_element_base: 5096

        - domain: lighting_frame
          binding: global_buffers_binding
          array_element_base: 1

        - domain: materials
          binding: global_buffers_binding
          array_element_base: 2049

        - domain: samplers
          binding: samplers_binding
          array_element_base: 0

    pipeline_layout:
      - type: descriptor_set
        id: BindlessMain
        set_ref: bindless_main

      - type: push_constants
        id: RootConstants
        size_bytes: 8
        stages: ALL
```

### Important consequences

- root CBV/root constants are no longer modeled as bindless domains
- backend-neutral ABI no longer carries D3D12 placement details
- shader-visible domain overlap is validated per generated index space, not across unrelated spaces such as SRV and SAMPLER
- backend realization carries the physical placement details needed to derive slots
- every bindless-visible domain declares the exact engine view types it accepts

## Generated: What The Generator Must Produce

The generator is the mandatory bridge between authored layout and runtime.

### Backend-neutral generated artifacts

- `Generated.BindlessAbi.h`
- `Generated.BindlessAbi.hlsl`
- `Generated.Meta.h`

These define:

- `DomainToken`
- domain descriptors
- shader-visible range constants
- HLSL guard helpers

### Backend-specific generated artifacts

D3D12:

- generated D3D12 strategy JSON
- `Generated.RootSignature.D3D12.h`
- optional `Generated.BindlessLayout.D3D12.h` if typed helper access is useful

Vulkan:

- generated Vulkan strategy JSON
- `Generated.PipelineLayout.Vulkan.h`
- optional `Generated.BindlessLayout.Vulkan.h` if typed helper access is useful

### Why this matters

The design gets stronger when runtime depends on generated outputs, not on hand-maintained mirror structures.

That is the real `meta -> generated -> runtime` contract:

- meta is authored once
- generated artifacts freeze that contract in machine-consumable forms
- runtime only consumes generated forms

## Runtime: Allocation Model

The bindless allocator must be domain-first.

### Public bindless API

```cpp
class BindlessAllocator {
public:
  virtual ~BindlessAllocator() = default;

  virtual auto Allocate(bindless::DomainToken domain,
    graphics::ResourceViewType view_type) -> BindlessHandle = 0;

  virtual auto Release(BindlessHandle& handle) -> void = 0;

  [[nodiscard]] virtual auto GetShaderVisibleIndex(
    const BindlessHandle& handle) const noexcept
    -> bindless::ShaderVisibleIndex = 0;
};
```

This is intentionally domain-first:

- callers name the semantic domain up front
- `view_type` is validated against the generated allowed set for that domain
- allocation happens inside the backend realization for that same domain

If a caller cannot name a domain, it should not be using the bindless API.

### Resource registration API

Normal resource/view workflows should also be domain-first:

```cpp
auto RegisterBindlessView(bindless::DomainToken domain,
  const NativeResource& resource,
  const ViewDescription& desc) -> BindlessHandle;
```

This is more ergonomic than "allocate a generic descriptor handle first, then register a view, then ask for a shader-visible index" because it makes domain ownership explicit at the call site.

### Raw descriptor API

Raw/non-bindless descriptors should be managed separately.

If the engine still needs generic CPU-only descriptor allocation for RTV/DSV or similar cases, that should stay behind a raw descriptor allocator API, not behind the bindless allocation API.

That separation is both cleaner and more faithful to Oxygen's boundary discipline.

## Backend Realization Rules

The backend owns physical/storage realization, but it must realize the generated ABI without changing it.

### D3D12

For one domain-local slot `n`, D3D12 computes:

- stable backend slot:
  - `heap.base_index + domain_realization.heap_local_base + n`
- shader-visible index:
  - `domain.shader_index_base + n`

Both are derived from the same domain-local slot.

That is the key correctness property. It is what prevents "heap-valid but domain-invalid" indices.

### Vulkan

For one domain-local slot `n`, Vulkan computes:

- realized array element:
  - `binding.array_element_base + n`
- shader-visible index:
  - `domain.shader_index_base + n`

Again, both are derived from the same domain-local slot.

### Shared rule

Regardless of backend:

- the owning domain chooses the local slot
- backend realization chooses how that slot maps into physical storage
- the generated ABI chooses how that slot maps into shader-visible index space

Because both mappings use the same local slot, domain correctness is enforced by construction.

## JSON Strategy Transport

The strategy transport stays JSON.

That is the correct choice here because it already satisfies the architectural requirement:

- the common strategy/provider API stays backend-neutral
- each backend consumes its own generated strategy payload
- the format is generator-friendly, readable, and only parsed once at startup

So the design is:

- keep JSON for backend strategy transport
- keep the provider pattern
- generate backend-specific JSON from the same authored source as the typed ABI artifacts

The design change is not "replace JSON".

The design change is:

- tighten what JSON is responsible for
- ensure it only carries backend realization, never the backend-neutral semantic ABI

## Oxygen-Style API Cleanup

The new APIs should follow existing Oxygen patterns:

- strong identifiers are derived from `NamedType`
- extra API such as `IsValid()` is added on the derived type
- layout equivalence is guarded with `static_assert` when relevant
- semantic/generic surfaces and backend-specific surfaces are kept separate
- bindless APIs are small and explicit rather than overloaded and inference-heavy

This is also why a breaking cleanup is appropriate:

- the old API shape hides the important semantic input, which is domain ownership
- the new API shape makes that semantic input explicit

## What Changes In Existing Components

### `Bindless.yaml` and schema

They must move from a partially D3D12-shaped description to:

- backend-neutral ABI section
- backend-specific realization sections

### Generator

It must become the explicit owner of the contract chain:

- authored spec
- generated ABI
- generated backend strategy JSON

### Backend bindless allocation

It must allocate from domain realizations, not from coarse heap buckets.

### `DescriptorAllocator`

Its current bindless-facing coarse API should not remain the public bindless contract.

If low-level backend internals still use view-type/visibility helpers, that is fine. The public bindless surface should still be domain-first.

### Nexus

It should use:

- `DomainToken` for semantic domain identity
- `VersionedBindlessHandle` over stable backend slots for lifetime/reuse

## Migration Plan

Rules:

1. **Module-boundary contract checks are mandatory from the first implementation slice.**
2. **Use `CHECK_*` for non-recoverable runtime contracts.**
3. **Use `DCHECK_*` for debug-only internal invariants.**
4. **Use always-on `LOG_*` only for actionable diagnostics.**
5. **Use `DLOG_*` / `DLOG_SCOPE_*` for routine success-path flow.**
6. **Do not promote happy-path tracing to always-on logs.**
7. **Scope-log strings must be preformatted at the call site.**
   For `LOG_SCOPE_F` / `DLOG_SCOPE_F`, construct the final string explicitly
   (for example with `fmt::format(...).c_str()`) instead of relying on mixed
   formatting conventions inside the macro.
8. **Warnings must explain deterministic rejection.**
9. **Logs must preserve ownership boundaries.**
   The module that validates a contract is the module that reports its failure;
   downstream stages must not silently reinterpret or repair invalid upstream
   state.
10. **Validation includes verbose-log review.**
    After completing an implementation phase, you must run the test program with
    '-v N', where N is the verbosity level, to validate that logs are correct
    and the flow can be understood from the logs at the proper verbosity level.

### Phase 1: authored model reset

- redesign schema around ABI plus backend realizations
- update `Bindless.yaml`
- add generator validations for ABI and per-backend realization

Exit criterion:

- the authored model can express both D3D12 and Vulkan without forcing one backend's physical model onto the other

### Phase 2: generated contract reset

- generate typed backend-neutral ABI artifacts
- generate backend-specific strategy JSON artifacts
- generate backend-specific helper headers as needed

Exit criterion:

- runtime bindless code can depend only on generated artifacts

### Phase 3: runtime API reset

- introduce `BindlessHandle`
- introduce domain-first `BindlessAllocator`
- move resource/view registration to domain-first APIs
- move Nexus to `DomainToken`

Exit criterion:

- no bindless-facing public runtime API selects ownership domain indirectly through `(ResourceViewType, DescriptorVisibility)`

### Phase 4: backend realization reset

- implement per-domain suballocation in D3D12 realization
- define and later implement the equivalent Vulkan realization model
- ensure both derive backend slot and shader-visible index from the same domain-local slot

Exit criterion:

- domain correctness is enforced by construction in every backend

### Phase 5: deletion pass

- remove coarse bindless-domain helpers
- remove allocator-state-derived domain mapping
- remove compatibility shims that preserve the old bindless public surface

Exit criterion:

- the codebase has one bindless ownership model, not two

## Required Validations

### Generator validations

- ABI domains do not overlap inside the same generated index space
- every domain's allowed `ResourceViewType` set is compatible with its shader access class
- every backend realization covers only declared ABI domains
- backend-specific storage ranges do not overlap within one physical realization space
- root descriptors/constants are not modeled as bindless domains

### Runtime validations

- allocating in domain `D` always yields a shader-visible index inside domain `D`
- backend slots never spill into sibling domain realizations
- reverse mapping remains consistent with generated domain ownership

### Nexus validations

- `DomainToken` is the only semantic domain key
- stale-handle detection remains keyed to stable backend slots
- reuse does not cross domain ownership boundaries

### Renderer regressions

- lighting-frame allocations cannot leave the lighting/global domain range
- churn in D3D12 shared SRV heaps cannot produce cross-domain leakage
- equivalent Vulkan churn cannot produce cross-domain leakage once that backend lands

## Final Recommendation

Adopt a domain-first bindless design with a strict `meta -> generated -> runtime` contract.

In concrete terms:

- author semantic domains and backend realizations separately
- generate both the typed ABI and backend strategy JSON from that single source
- allocate bindless resources by `DomainToken`
- represent runtime ownership with `BindlessHandle { domain, slot, view_type }`
- keep Nexus keyed by generated domain token plus stable backend slot versioning

This is the design that directly addresses the real bug:

- stable backend slots remain valid
- shader-visible indices become domain-correct by construction

That is the enabling outcome the refactor must deliver.
