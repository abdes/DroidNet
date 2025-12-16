# FilteredObservableCollection — Delta Builder Design (Strict, No Fallbacks)

**Short summary:** This document specifies a strict, no‑fallback design for a delta-capable, builder-driven `FilteredObservableCollection<T>`. This design enforces `T : class, IEquatable<T>`, accepts per-trigger builder lists (adds or removes only), manages per-item refcounts, validates builders strictly (throws on any contract violation), applies deltas efficiently without full collection scans, and emits a single minimal atomic change notification per processed batch.

---

## 1) Non-negotiable constraints

- The collection type must be declared as:

```csharp
public sealed class FilteredObservableCollection<T>
    where T : class, IEquatable<T>
{ ... }
```

- Builders must implement the required signature (section 2).
- Any builder contract violation or internal invariant failure MUST throw `InvalidOperationException` and leave the collection state unchanged (no silent fallback, no Reset, no options).
- Exactly one atomic change notification is emitted per processed batch (no Reset spam).

---

## 2) Builder contract and signature

BREAKING CHANGE — replace the builder interface

This design requires a single, well-defined builder contract and does not permit gradual API accretion for backward compatibility. The existing builder interface(s) and implementations MUST be updated to the new single-method contract; legacy overloads or additional convenience methods must be removed. This is an intentional, breaking API change: callers and implementers must be updated accordingly.

Required method on builders (single-method contract):

```csharp
IReadOnlySet<T> BuildForChangedItem(T changedItem, bool becameIncluded, IReadOnlyList<T> source);
```

Semantics:

- `becameIncluded == true` → returned set contains items to include (**increment** (+1)).
- `becameIncluded == false` → returned set contains items to exclude (**decrement** (-1)).

Returned-set rules and validation (strict):

- The builder returns an `IReadOnlySet<T>` containing *exact* source instances (reference equality). Returning equal-but-distinct instances is a contract violation and will cause the operation to throw and abort.
- The set must not contain duplicates (the set type enforces uniqueness); duplicate entries are a contract violation and will throw.
- The set may include any additional source items (lineage, dependents) required to preserve structural correctness.
- If any rule is violated the collection throws `InvalidOperationException` and the operation is aborted (state unchanged).
- The filter implementation will always honor the predicate result for `changedItem` regardless of whether `changedItem` appears in the returned set.

Rationale:

- Avoids indefinite API surface growth and ambiguity about which builder method is authoritative.
- Forces implementers to move to the new, single, strictly-validated contract so the collection can maintain simple, predictable apply semantics.

Migration notes:

- Update all builder interfaces and implementations to the new method and remove legacy entry points.
- Update any consumers or factory code that construct builders.
- Add tests that exercise the strict-reference validation to catch accidental use of value-equal but distinct instances.

---

## 3) Core data structures

This design uses an order-statistic sequence to represent the *source order* and to support fast, incremental index queries/updates. The minimal public change set (added/removed items + indices) is computed efficiently from that sequence without full scans.

## 3) Core data structures

This design uses an order-statistic sequence to represent the *source order* and to support fast, incremental index queries/updates. The minimal public change set (added/removed items + indices) is computed efficiently from that sequence without full scans.

- **Sequence (`FilteredTree`)** — We will use a **private specialized subclass** of `OrderStatisticTreeCollection<T>`. This requires enhancing the existing `OrderStatisticTreeCollection` class to support extension (unsealing, virtual hooks).
  - **Upstream Changes Required**:
    - Unseal `OrderStatisticTreeCollection<T>`.
    - Expose `Node` as `protected` (or `public`).
    - Add virtual `CreateNode(T value)` to allow custom node types.
    - Add virtual `OnNodeUpdated(Node node)` hook called during rotations and size maintenance to update custom augmentation data.

  - **Internal Implementation**:
    - `FilteredObservableCollection` will contain a `private class FilteredTree : OrderStatisticTreeCollection<T>`.
    - `FilteredTree` will use a custom node type `FilteredNode : Node`.
    - `FilteredNode` adds:
      - `int RefCount` (0 = excluded)
      - `bool Included`
      - `int SubtreeIncludedCount` (augmented metric for filtered indexing)
    - `FilteredTree` overrides `OnNodeUpdated` to maintain `SubtreeIncludedCount` = `Included ? 1 : 0` + sum of children's counts.

- **Dictionary<T, Node> instanceMap** — maps each exact source instance to its node in the tree. **This dictionary MUST use reference equality** (e.g., `ReferenceEqualityComparer<T>.Instance`). This enforces that builders must return the *exact* source instances.
  - *Note:* This map provides the O(1) link between the "Domain Object" and the "Tree Node".

- **Hierarchical Flattening Support** — While the collection presents a flat `IList` interface, the builder pattern supports hierarchical data flattening:
  - Builders simply toggle inclusion of descendants based on parent state.
  - The `OrderStatisticTree` inherently maintains the relative order of items based on their addition order (or source order), which allows the filtered view to respect the topological/visual structure of the source.

- **No standalone `filteredList`** — Filtered indices are derived via `SubtreeIncludedCount` queries on the tree (O(log n)).

- **Synchronization** — Follows existing collection conventions (not thread-safe).

---

## 4) Atomic apply algorithm (Interleaved Execution)

To ensure compatibility with UI controls (e.g., `ItemsRepeater`), the collection state (`Count`, Indexer) must always match the cumulative events received by the view. Therefore, we **cannot** apply all changes first and emit events later. We must use a **Two-Phase Strategy**:

### Phase 1: Calculation & Validation (The "Plan")

1.  **Collect Triggers**: Gather `(changedItem, becameIncluded)`.
2.  **Query Builders**: Call `BuildForChangedItem` for each trigger.
3.  **Accumulate Deltas**:
    - Use a temporary map to track provisional `RefCount` changes for all involved items.
    - Validate invariants (e.g., no RefCount underflow).
    - If any validation fails, throw and abort (state remains untouched).
4.  **Compute Transitions**:
    - Identify items transitioning to **Excluded** (`Old > 0` && `New == 0`).
    - Identify items transitioning to **Included** (`Old == 0` && `New > 0`).
    - Identify "Silent Updates" (Included -> Included, but RefCount changes).

### Phase 2: Execution (The "Do")

Apply changes in a strict order to maintain valid indices for pending operations.

**Step A: Apply Silent Updates**
- For items remaining included (e.g., RefCount 1->2), update their counts in the Tree immediately. No events needed.

**Step B: Execute Removals (Descending Order)**
- Sort items to be excluded by their *current filtered index* (descending).
- **Group into contiguous runs** (e.g., if removing at indices 5, 4, and 3, treat as one block).
- For each run:
  1.  **Update Tree**: Set `Included = false` and `RefCount = 0` for the items in this run. Update tree metrics.
  2.  **Emit Event**: `CollectionChanged(Remove, items, index)`.
  *Result*: The collection size shrinks immediately, matching the event. Lower indices remain valid for subsequent iterations.

**Step C: Execute Additions (Ascending Order)**
- Sort items to be included by their *source index* (ascending).
- **Group into contiguous runs** (items adjacent in source order).
- For each run:
  1.  **Calculate Index**: Query the Tree for the current insertion index of the *first* item in the run (this index reflects the state after all Step B removals and previous Step C additions).
  2.  **Update Tree**: Set `Included = true` and `NewRefCount` for the items in this run. Update tree metrics.
  3.  **Emit Event**: `CollectionChanged(Add, items, index)`.
  *Result*: The collection grows immediately, matching the event.

This interleaved approach guarantees that at every generic `CollectionChanged` event, `this.Count` equals the expected value, preventing UI "Index Out of Range" crashes.

---

## 5) Notifications — Minimal & Consistent

- **Removals**: Emitted as contiguous batches (Descending).
- **Additions**: Emitted as contiguous batches (Ascending).
- **Moves**: Handled purely by Source Collection `Move` events (not Builders). when Source moves an item, the Tree updates its structure and emits a corresponding logical Move if the item is currently Included.
- **Resets**: Never emitted by the Delta Builder logic.

**Example: Interleaved Flow**
*Batch: Add Item A, Remove Item B*
1.  **Calc**: A will be added, B will be removed.
2.  **Exec Remove**:
    - Find B's index (say 10).
    - Tree: Remove B. Count becomes N-1.
    - Event: `Remove(B, index 10)`.
3.  **Exec Add**:
    - Find A's insert index (say 2).
    - Tree: Add A. Count becomes N.
    - Event: `Add(A, index 2)`.
Final State: Correct. UI happy.

---

## Reference example — Detailed walkthrough (include then exclude)

This step-by-step example shows precise source indices, state changes, and `CollectionChanged` events using the **Interleaved Execution** strategy.

Initial state (explicit source indices)

- Source (index:Item): 0:Item1, 1:Item2, 2:Item3, 3:Item4, 4:Item5
- Initial refCounts: Item2 = 1, Item4 = 1, others = 0
- Initial filtered view (by source order): [Item2 (filtered index 0), Item4 (filtered index 1)]
- Count: 2

Example A — Item3 becomes included

- Trigger: changedItem = Item3 (source index 2), becameIncluded = true
- Builder returns set: `{ Item3 }`

**Phase 1: Calculation**
- Item3: Old=0 -> New=1 (Add Candidate)

**Phase 2: Execution**
- Step A (Silent): None.
- Step B (Removals): None.
- Step C (Additions):
  - Processing Item3.
  - Calculate Index: Item2 is included (src 1 < 2). Index = 1.
  - **Tree Update**: Item3 becomes Included. Count -> 3.
  - **Emit Event**: `NotifyCollectionChangedAction.Add` (items=[Item3], index=1).

Resulting filtered view: [Item2 (0), Item3 (1), Item4 (2)]

Example B — Item2 becomes excluded (full exclusion)

- Trigger: changedItem = Item2 (source index 1), becameIncluded = false
- Builder returns set: `{ Item2, Item3 }` (Item3 must be removed because it depended on Item2)

**Phase 1: Calculation**
- Item2: Old=1 -> New=0 (Remove Candidate)
- Item3: Old=1 -> New=0 (Remove Candidate)

**Phase 2: Execution**
- Step A (Silent): None.
- Step B (Removals):
  - Sort Candidates Descending: Item3 (index 1), Item2 (index 0).
  - Group: [Item2, Item3] is a contiguous run starting at index 0.
  - **Tree Update**: Item2 and Item3 become Excluded. Count -> 1.
  - **Emit Event**: `NotifyCollectionChangedAction.Remove` (items=[Item2, Item3], index=0).
- Step C (Additions): None.

Resulting filtered view: [Item4 (0)]

Extra scenario — adding Item4 when it is not included

- Same logic: Calculate insertion index based on current included count of prior items, update tree, then emit add.

Batching note

- In a larger batch, all removals happen first (shrinking the collection from right to left or in chunks), followed by all additions (expanding the collection). Each step keeps `Count` consistent with the event stream.

---

## 6) Validation & error behavior (NO FALLBACKS)

Always throw `InvalidOperationException` (with a clear diagnostic message) for:

- Builder returned item not present in `source` (by equality).

- Builder contradicted the trigger for the triggering item.

- Refcount underflow (any `newCount < 0`).

- Internal invariants mismatch between sequence tree (`SubtreeIncludedCount`/node `Included`) and stored refcounts when applying changes.

On exception, abort without mutating the collection state.

---

## 7) Defer, debounce, batching

- While `DeferNotifications()` is active: buffer triggers and builder outputs; on resume apply them atomically and emit one `ChangesApplied` event.
- Debounced property changes: coalesce triggers during the debounce window into a single batch and apply once.
- If any builder throws during resume processing, throw and leave collection unchanged.

---

## 8) Duplicates & equality

- `IEquatable<T>` is required for the collection and remains important for other equality semantics, but **builder outputs are validated by reference identity**: builders must return the actual source instances (object identity) when indicating items to include/exclude. The collection enforces this using `instanceMap` with a reference-equality comparer; if a builder returns an equal-but-distinct instance the validation will fail and throw. Users who need logical identity must ensure they return the correct source instances when writing builders.

---

## 9) Test matrix (must pass)

- Single include: builder returns lineage → `CollectionChanged` (Add) with correct indices.
- Single exclude: builder returns lineage → `CollectionChanged` (Remove) with correct indices.
- Overlapping includes/excludes: counters accumulate correctly; correct sequence of `CollectionChanged` events emitted.
- Invalid builder outputs (out-of-source, contradict trigger) → throw and no state mutation.
- `DeferNotifications()` buffering multiple triggers → aggregated `CollectionChanged` events on resume.
- Source Move: reorders included items; Move logic from source handler executes (builders not involved).
- Performance test: 10k source items, small per-item delta operations remain fast.

---

## 10) Implementation checklist

- Refactor `OrderStatisticTreeCollection` to be extensible: unseal, expose `Node` (protected), add `CreateNode` and `OnNodeUpdated` virtual hooks.
- Update `FilteredObservableCollection<T>` generic constraint to `where T : class, IEquatable<T>`.
- Replace the builder interface: remove legacy builder methods and adopt the new single-method `BuildForChangedItem(T changedItem, bool becameIncluded, IReadOnlyList<T> source)` contract.
- Implement the internal `FilteredTree` subclass and `FilteredNode`.
- Add `refCounts`, `ChangesApplied` event, and the batch apply algorithm using the new tree structure.
- Add tests in test suite per matrix, including strict reference-identity validation tests and migration tests for updated builders.
- Add diagnostic messages for thrown exceptions and clear migration guidance in the CHANGELOG.

## 11) Expand/Collapse and Filtering

This section documents a **tree-control-specific** pattern for combining:

- A *strict* `FilteredObservableCollection<T>` whose **source is the visual/realized list** (e.g., `ShownItems`).
- A dynamic tree control where **collapse removes descendants from the visual tree** (for scalability), and expansion may lazily load children.

### 11.1 Problem statement

When a filtered view is built over `ShownItems`, collapsing a parent removes descendants from `ShownItems`. Under the strict delta-builder semantics, those descendants are no longer in the filter universe, so:

- Ancestors that were only included because of matching descendants become no longer justified and are correctly removed.

However, in a tree UI, collapsing a parent is expected to **hide** descendants but keep the parent visible so the user can re-expand and reveal (matching) descendants again.

### 11.2 Non-negotiable constraints (tree control)

- Collapse **must** remove descendants from the visual/realized list (no “keep hidden nodes in `ShownItems`”).
- Filtering must not force-load children; the control may provide a separate **Expand All** command that loads everything.
- The `FilteredObservableCollection<T>` contract remains unchanged: builders may only return **exact source instances**, and the collection only reasons about items that are present in its source.

### 11.3 Agreed solution (7C): loaded-only subtree match via the predicate

We solve the collapse/lineage mismatch by making the **effective predicate** tree-aware:

- `userMatches(node)` is the user’s intended match predicate (e.g., label contains text).
- `subtreeHasLoadedMatch(node)` is a cached computation that returns `true` if the node has at least one **already-loaded** descendant (direct or indirect) for which `userMatches(descendant)` is `true`.

The effective predicate used by the filtered view becomes:

```csharp
effectiveMatches(node) = userMatches(node) || subtreeHasLoadedMatch(node)
```

This keeps ancestors visible when any loaded descendant matches, **even if** that descendant is currently collapsed (and therefore absent from `ShownItems`).

**Strict loaded-only rule:** If a subtree is not loaded, then `subtreeHasLoadedMatch(node)` is `false` for that subtree until it becomes loaded.

### 11.4 Required adapter surface (no forced loads)

The view model (or adapter layer) must provide a way to enumerate *loaded* children without triggering loading.

Minimum required capability per node:

- `AreChildrenLoaded` (boolean): indicates whether children have been loaded.
- `TryGetLoadedChildren(out IReadOnlyList<T> children)` (or equivalent): returns currently loaded children **without** touching the lazy `Children`/`LoadChildren` path.

Notes:

- Do **not** infer “loaded” from `children.Count == 0` because empty can mean either “not loaded yet” or “loaded and empty”.
- Prefer using the lazy state (e.g., `childrenLazy.IsValueCreated` and task completion state) to distinguish not-loaded vs loaded.

### 11.5 Cache computation (single-pass per filter revision)

The effective predicate must not recursively scan descendants per-item (that becomes quadratic). Instead, compute a cache once per “filter revision”.

Maintain (keyed by reference identity):

- `selfMatch[node]` — `userMatches(node)`
- `subtreeMatch[node]` — `subtreeHasLoadedMatch(node)`
- `filterRevision` — incremented whenever the answer could change
- `computedRevision` — the revision the caches correspond to

Compute caches using a post-order traversal over **loaded edges only**:

$$
subtreeMatch(n) = selfMatch(n) \lor \bigvee_{c \in loadedChildren(n)} subtreeMatch(c)
$$

Implementation guidelines:

- Use an explicit stack (iterative DFS) to avoid stack overflows on deep trees.
- Traverse only when `AreChildrenLoaded == true`; otherwise treat `loadedChildren(n)` as empty.
- Defensively guard against cycles (should not happen in a tree, but adapters can be buggy).

### 11.6 Integration with `FilteredObservableCollection<T>`

- The filtered view is still created over `ShownItems`.
- The builder may still return lineage/ancestors for strict structural closure of currently-included items.
- The view model must own refresh timing. When `filterRevision` changes, the view model recomputes caches and calls `ReevaluatePredicate()` on the filtered view.

The predicate delegate passed into the filtered view must read the *current* caches (no stale capture).

### 11.7 Robust cache invalidation (loaded-only)

Invalidate by incrementing `filterRevision` and scheduling a refresh (debounced) whenever any of the following occurs:

- **User predicate changed** (example: filter text changed) → `filterRevision++` and refresh.
- **Relevant property changed on a loaded node** (example: `Label` changed and the predicate depends on `Label`) → `filterRevision++` and refresh. Prefer a “relevant properties” list to avoid invalidating on unrelated property changes.
- **Loaded children changed** (structure changes within the loaded universe; Add/Remove/Reset/Move) → `filterRevision++`, update subscriptions for newly added/removed nodes, and refresh.

What must **not** invalidate by itself:

- Collapse/expand toggles (as UI state) do not change the loaded-universe truth. They only change `ShownItems`. The filtered view will naturally react to `ShownItems` changes; the cache only changes when the loaded universe or match predicate changes.

### 11.8 Subscription strategy (so invalidation stays correct as loading occurs)

To support invalidation without forcing loads, the view model maintains a set of subscriptions for nodes currently in the loaded universe.

Maintain a reference-identity set `observedNodes` and for each observed node subscribe to:

- `PropertyChanged` (if implemented) to catch relevant property updates.
- `LoadedChildrenCollectionChanged` (or equivalent) that fires when the backing loaded-children collection changes.

When a node transitions to “children loaded” and its children list is populated, the adapter should raise a `Reset` (or equivalent). The view model then:

- Observes newly loaded children.
- Marks the cache dirty (`filterRevision++`).
- Schedules a refresh.

Unsubscription policy:

- When nodes are removed from the loaded universe, unsubscribe to avoid leaks.
- For simplicity, it is acceptable to unsubscribe only for directly-removed nodes and rely on reachability for descendants, but the preferred approach is to unobserve the removed subtree when it can be traversed without loading.

### 11.9 “Expand All” behavior

An explicit “Expand All” command may force-load the entire tree (or a large portion of it). As loads complete:

- New children become part of the loaded universe.
- The adapter raises collection change events.
- The view model invalidates caches and refreshes the filtered view.

This yields progressive discovery of matches without breaking the default “filtering does not force-load” contract.

### 11.10 Correctness and UX properties

With this design:

- Collapsing a parent does not permanently hide matching descendants: the parent can remain visible (and expandable) because `subtreeHasLoadedMatch(parent)` stays true based on loaded descendants.
- Filtering remains incremental and does not require `Reset`.
- Filtering does not violate the strict `FilteredObservableCollection<T>` builder validation rules because the filtered collection still operates purely on `ShownItems`; the subtree logic lives entirely in the predicate/caching layer.
