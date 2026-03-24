# VirtualShadowMaps

This folder contains the greenfield low-level VSM module. It is intentionally separate from the active renderer shadow backend, which remains the conventional directional shadow path.

## Ownership Split

- `VsmPhysicalPagePool*`: persistent physical pool state, compatibility, snapshots, and GPU resource lifetime.
- `VsmVirtualAddressSpace*`: frame-local virtual layouts, clipmaps, remap products, and layout math.
- `VsmCacheManagerSeam.h`: the stable package a future cache manager will consume.

## Helper Policy

- `VsmPhysicalPageAddressing.*` and `VsmPhysicalPoolCompatibility.*` exist because they carry reusable contract logic.
- `VsmVirtualClipmapHelpers.*` and `VsmVirtualRemapBuilder.*` exist because clipmap reuse and remap construction are separate policy-free helpers.
- New files should only be added when they introduce a clear ownership or dependency boundary.

## Troubleshooting

- Invalid public configs fail fast.
- Reuse rejection reasons are explicit and test-covered.
- Strategic warnings are emitted for malformed frames, malformed layouts, duplicate remap keys, and missing remap keys.
