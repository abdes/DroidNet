# VirtualShadowMaps

This folder contains the greenfield low-level VSM module. It is intentionally separate from the active renderer shadow backend, which remains the conventional directional shadow path.

## Ownership Split

- `VsmPhysicalPagePool*`: persistent physical pool state, compatibility, snapshots, and GPU resource lifetime.
- `VsmVirtualAddressSpace*`: frame-local virtual layouts, clipmaps, remap products, and layout math.
- `VsmCacheManager*`: cross-frame cache state, planner orchestration, retained-entry continuity publication, targeted invalidation intake, and current-frame working-set publication through `VsmCacheManagerSeam`.
- `VsmCacheManagerTypes.*`: shared cache-manager state/config enums, allocation/invalidation contracts, and explicit initialization-work products.
- `VsmCacheManagerSeam.h`: the stable package a future cache manager will consume.

## Current Status

- The greenfield VSM cache/page-allocation slice is implemented through the current hardening phase:
  - explicit cache/build state machines
  - deterministic CPU allocation planning
  - backend-backed working-set resource publication
  - retained unreferenced-entry continuity publication
  - scoped targeted invalidation and explicit initialization work
- Frequently run coverage lives under `Oxygen.Renderer.VirtualShadows.Tests`.
- Backend-backed dedicated coverage lives under `Oxygen.Renderer.VirtualShadows.GpuLifecycle.Tests`.

## Known Forward Gaps

- Targeted invalidation currently queues CPU-side invalidation records and applies them to a planning copy of the previous extracted snapshot. This is intentionally shaped to stay compatible with the later dedicated GPU invalidation stage, but that GPU stage is not implemented yet.
- Projection-data publication is still missing. The full architecture expects cache-manager-owned projection products for current and retained previous-frame continuity; those renderer-integration products remain future work.
- Scene-mutation invalidation workloads are not implemented yet. Current invalidation is remap-key targeted only.

## Helper Policy

- `VsmPhysicalPageAddressing.*` and `VsmPhysicalPoolCompatibility.*` exist because they carry reusable contract logic.
- `VsmVirtualClipmapHelpers.*` and `VsmVirtualRemapBuilder.*` exist because clipmap reuse and remap construction are separate policy-free helpers.
- New files should only be added when they introduce a clear ownership or dependency boundary.

## Troubleshooting

- Invalid public configs fail fast.
- Reuse rejection reasons are explicit and test-covered.
- Strategic warnings are emitted for malformed frames, malformed layouts, duplicate remap keys, missing remap keys, incompatible pool/snapshot reuse, and rejected targeted invalidation inputs.
