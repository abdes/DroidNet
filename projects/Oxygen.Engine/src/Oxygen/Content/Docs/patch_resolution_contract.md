<!--
Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
copy at https://opensource.org/licenses/BSD-3-Clause.
SPDX-License-Identifier: BSD-3-Clause
-->

# Runtime Patch Resolution Contract

Read [precedence](#precedence-policy), [path lookup](#virtualpath-resolution)
and [patch admission](#compatibility-validation).

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

`AddPakFile` reads the archive's embedded `Data::PakCatalog`; callers do not pass
an external manifest. Declared base records must match the contiguous suffix of
layers immediately below the patch, comparing SourceKey, content version and
catalog digest in order. Additional lower-priority layers are allowed. A
cumulative patch replaces earlier patches built against the same original base.
There are no compatibility opt-outs.

Validate before changing the active mount set. Install source identity and
embedded deletion records together. Refreshing a lower layer must also preserve
the requirements of any patch above it. Reject incompatible asset-type overrides.

New asset graphs capture one immutable ordered layer view; all dependencies share
that view. Existing objects retain their exact bindings. Cache identity includes
the binding view, while physical resource identity remains source-local. See
[published generations](loose_cooked_content.md#published-generations).

## Diagnostics

- When a lower-priority virtual-path mapping is masked by a higher-priority
  mapping, runtime emits a collision warning with winner/masked source IDs and
  keys.
- Invalid patch baselines are errors; mounting leaves the previous view usable.
