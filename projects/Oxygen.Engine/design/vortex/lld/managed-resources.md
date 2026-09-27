# Renderer managed resources and views

Renderer Resources assembles managed registrations and typed views for
caller-created allocations. Graphics ResourceRegistry remains the authority for
registration identity, descriptor caching and GPU-use retention.

The capability below is planned; [VX-IBL-01.S7](../milestones/VX-IBL-01/README.md#s7--reusable-infrastructure)
owns adoption, implementation state and integration qualification.

Read: [contract](#managed-resources-and-views), [C++ guidance](../RULES.md#c-for-shared-infrastructure).

## Managed resources and views

**Implementation home:** small typed helpers in `Vortex/Resources/ManagedGpuResource.*`.
First adopters are IBL's texture/buffer allocations, captured-sky target setup and
[`ConventionalShadowTargetAllocator`](../../../src/Oxygen/Vortex/Shadows/Internal/ConventionalShadowTargetAllocator.cpp).
S7.1 uses the same helpers for both LUT destinations and staging resources.

- Accept a caller-created `Texture` or `Buffer` and an explicit list of typed
  view descriptions with the requested bindless domain. Return the resource,
  internal `RegistrationOwner` and ordered `ManagedView` results. No dimension
  inference from height, descriptor aliasing, resource cache or new allocator.
- Register and acquire initial views transactionally through `ResourceRegistry`.
  Failure unwinds the candidate through managed retirement and exposes no partial
  result. Existing identities/cache keys remain authoritative. An external
  `RegistrationLease` is acquired explicitly only where the consumer needs it;
  stored allocation bundles must not retain Graphics through that lease.
- Resource creation remains at the caller so allocation-budget exceptions and
  shadow chunk-size fallback retain their exact meaning. Preserve formats,
  typeless depth/SRV compatibility, clear values, subresource ranges, domain and
  visibility. IBL creates its per-mip UAVs; shadows retain lazy CPU-only DSV
  acquisition for each used face rather than allocating all DSVs eagerly.
- GPU state transitions and `RetainRegistration` remain at recording sites;
  constructing a bundle does not establish a GPU use or transition a resource.
  Do not migrate TextureBinder's stable-descriptor replacement or unrelated
  output pools in this slice.

**Checks:** failure at each initial-view index, retry/cleanup, correct domains,
SRV/UAV mip ranges, typeless-depth SRV and lazy DSVs, backend close and resource
retention after the family owner disappears. Registration populations and
Lighting budget fallback match the existing native tests.
