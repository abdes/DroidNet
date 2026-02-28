<!--
Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
copy at https://opensource.org/licenses/BSD-3-Clause.
SPDX-License-Identifier: BSD-3-Clause
-->

# Runtime Patch Resolution Contract

This document is normative for Content runtime patch lookup behavior.

## Precedence Policy

- Resolution policy is fixed to `last-mounted wins`.
- Mount traversal order is highest-precedence to lowest-precedence.
- A mount-level tombstone is terminal:
  - if a mount tombstones a key, lookup returns `not found` immediately;
  - lower-priority mounts are not consulted.

## AssetKey Resolution

Asset-key lookup must use the shared `PatchResolutionPolicy` helper:

1. Traverse mounted sources from highest precedence to lowest.
2. For each source:
   - if the source tombstones the key, return `not found` (terminal);
   - else if the source contains the key, return `found(source_id)`;
   - else continue.
3. If no source returns a terminal result, return `not found`.

## VirtualPath Resolution

Virtual-path lookup must route through the same policy surface:

1. Resolve the virtual path in precedence order to a candidate key.
2. Re-run the shared asset-key resolution policy for that candidate key.
3. Return the final `found/not-found` result from step 2.

This guarantees parity between virtual-path and direct asset-key lookups.

## Compatibility Validation

Patch application must validate `data::PatchManifest::compatibility_envelope`
against the currently mounted base set before mounting patch tombstones.

Validation uses:

- mounted runtime source keys,
- mounted base `data::PakCatalog` snapshots,
- policy switches from `PatchCompatibilityPolicySnapshot`.

Validation failures are hard errors and patch application must be rejected.

## Diagnostics

- When a lower-priority virtual-path mapping is masked by a higher-priority
  mapping, runtime emits a collision warning with winner/masked source IDs and
  keys.
- Compatibility validation failures emit one diagnostic per violated rule.
