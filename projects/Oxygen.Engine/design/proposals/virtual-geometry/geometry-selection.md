# Virtual geometry selection and visibility

Status: proposal. Part of [virtual geometry](README.md). Owner module: Vortex.

Last updated: 2026-10-28

Read: [inputs](#inputs-and-outputs), [valid cut](#valid-cut-selection),
[bounded progress](#bounded-progress), [visibility](#visibility-and-history),
[verification](#verification).

## Inputs and outputs

Inputs are admitted [resource snapshots](geometry-resources.md), immutable
Data hierarchy and group records, instance transforms, per-view projection and
error policy, and per-cluster visibility history. Outputs are an immutable
per-view selection snapshot, visible cluster work, logical page demand,
protection inputs, representation-change bounds and bounded statistics.

Shared page residency and per-view selection are different products. Camera,
shadow, reflection and additional views may choose different approximations.
Page demand and protection are aggregated; the main camera's visible clusters
are never substituted for a shadow caster set. The proof workload qualifies
camera and conventional shadow views plus a second camera; other view families
stay gated.

An instance references one resource and manifest plus transform and material
state. Cluster identity resolves geometry; instance identity resolves world
placement and material bindings. There is no duplicate scene node and no CPU
command per cluster.

### Public API and reuse

Selection and its snapshot and work records are private Vortex service and
stage contracts, not an application cluster API. Reuse Data `ClusterId`,
`GroupId`, `TraversalNodeId`, Core `ViewId`, `ResolvedView`, `Frustum` and the
existing instance and resource identity. World-error and projected-pixel-error
parameters are distinct `NamedType`s; loop counters and fixed GPU ABI fields
stay scalars. Generic shader math is reused where dependency direction allows,
but conservative GPU bounds keep their own independent tests. There is no
second public culling framework, event bus or hierarchy container. See the
[shared rules](README.md#shared-rules).

## Valid cut selection

Use the [v1 incidence DAG](geometry-format.md#replacement-profile-v1). For each
admitted instance the selected set `S` contains manifest-local `ClusterId`s;
the view's cut is the union tagged by resource incarnation and instance
generation. Group `g` performs `S' = (S \ C_g) ∪ F_g` only when the whole of
`C_g` is selected and no member of `F_g` already is. Every view update starts
from the certified roots `R`; previous selections only provide hysteresis
hints. Fresh root reconstruction handles coarsening and eviction without a
second reverse-rewrite transaction algorithm. Root geometry, routing, runtime
closure and output and bin capacity must already be admitted.

Immutable group-rank ranges are processed coarse to fine, at most the declared
maximum rank count. Each round reads the previous immutable cut. Candidates
are deduplicated by (resource incarnation, instance generation and index,
`GroupId`) within the view product, and require full coarse membership,
compatible revision and boundaries, error demand, installed fine outputs and
their ongoing runtime-reference closure. Decode-only inputs consumed by
installation are no longer selection prerequisites; reinstalling them goes
through the resource owner. Missing detail emits logical completion demand and
leaves the coarse side intact.

Unique incidence and strict ranks make distinct enabled events in a round
nonconflicting: their consumed and produced sets are disjoint. Malformed
overlapping source events are rejected at cook or load validation, not by GPU
locks. Example: `g:{a,b}→{p,q,r}`, `h:{x}→{a,c}`, `i:{y}→{b,d}`, roots
`{x,y}`. After `h`, `g` stays disabled until `i` has also produced `b`. This
is a DAG, not a requirement that coarse siblings stay together in a tree.

For each candidate and bounded output component `j`, compute
`d(g,j) = max(0, cost_j(F_g) − cost_j(C_g))`. Components are total cluster
work, each resolved instance material bin, distinct command bins and indirect
commands. Candidates order by priority then a stable candidate key, and the
longest prefix whose componentwise deltas sum within the remaining capacities
is admitted. A candidate never spends another's speculative negative delta.
Histogram, sort, frontier and two-cut-buffer scratch are bounded separately;
net output size does not bound temporary memory.

The delta formula covers additive item counts. For distinct material and
command bins, each candidate's bins absent from the whole prior cut `S` are
reserved conservatively, without crediting bins that consuming `C_g` might
remove, and summed even when candidates introduce the same bin. Counting only
`bins(C_g)` against `bins(F_g)` is unsafe because other selected clusters may
keep an old bin alive.

Required protection for the admitted prefix is acquired and validated before a
replacement cut is written. The entire candidate round is built in the second
buffer. Its counts and cut publish only after membership, protection, output,
bin and encoding checks succeed with the required barriers. Any late failure
discards the round and keeps the preceding valid checkpoint; a coarse removal
never becomes visible alone. Exhausted scratch or candidate capacity stops
further refinement. One immutable representation is published for the view's
passes, and visibility culling operates on it. Pages need not arrive
atomically.

The checkpoint selector and commit-success flag stay GPU-owned. Final GPU
compaction writes a stable selection binding and indirect counts from the last
valid checkpoint; the CPU never reads back which working buffer won.

This bounded v1 may skip refinements that would fit after speculative
savings, and scans rank ranges even when some hold little work. Those are
explicit quality and cost tradeoffs, not coverage failures. Spatial
acceleration can reduce candidate discovery without changing the commit rule.

The compiler records geometric error to the finest authored surface, including
quantization. For a perspective view, projected error depends on the focal
scale and the nearest conservative depth of the error-expanded bounds, not the
sphere centre distance. Near-plane intersection, nonfinite input or an
unsupported transform bound prevents any aggressive rejection or refinement
shortcut. Orthographic views use world error times projected scale, and a
conservative transform norm covers nonuniform scale and shear. The exact bound
is owned here and independently tested before error-based pruning is enabled.

The projected-pixel-error threshold is a profile parameter. Error is not
triangle size, but a smaller threshold refines to smaller triangles, and the
hardware rasterizer shades 2×2 pixel quads, so the vertex-pulling profile does
not set its threshold where triangles shrink to a few pixels and quad overdraw
dominates shading. Its value is chosen by measurement at format freeze, with
the scenario's cliff and terrain as the workload. A lower threshold belongs to
a later rasterization profile ([evolution](README.md#rasterization-evolution)).

Upper and lower refinement thresholds with documented hysteresis prevent
thrashing. Error rank and request priority are separate from actual
residency. A threshold change may change representation identity but never
the source surface revision. Previous selections persist only with matching
view and resource identity and valid epoch dependencies.

## Bounded progress

Capacity for the admitted coarse cut is reserved before refinement.
Refinement needs room for its replacement's net output and temporary work. A
full append buffer is never handled by dropping arbitrary clusters: overflow
aborts the pending substitution or ends refinement, and the current valid
coarse cut stays. If the coarse cut itself cannot fit, region admission fails
before rendering.

Traversal depth, frontier, candidate groups, visible clusters, request records
and command bins are bounded independently. A small page can hold many
clusters, so page capacity does not bound every work buffer. Indirect counts
are checked and overflow is an explicit status. No persistent shader loop is
unconstrained.

Selection emits ranked logical group demand; it does not own loading
completion permits. The [resource owner](geometry-resources.md#completion-admission-policy)
alone schedules those permits and their closure and protection lifetimes.
Nexus sees generic page sets and credits, not group topology. Persistent
misses are reissued, and coarse coverage is unaffected by feedback latency.

Multiple legal cuts are qualified against a tiny independent coverage model
before rendered screenshots are trusted. The invariant applies to the selected
surface before visibility culling; proven invisible clusters may then be
omitted from raster.

## Visibility and history

Cluster culling is the accepted
[cluster culling through vertex pulling](../../vortex/lld/occlusion.md#101-cluster-culling-through-vertex-pulling-vx-occ-05)
level, applied to virtual clusters. It reuses the occlusion module's culling
records, visibility bits, `IndirectListBuilder` compaction and two-phase
structure:

1. A visible draw expands into its clusters. Each cluster gets the frustum,
   coverage and cone tests; the cone is applied only where compiler metadata is
   valid and sidedness, transform orientation and deformation are compatible.
   Bounds cover the decoded surface plus the supported motion and displacement
   envelope. Unknown conditions disable an optimization, never geometry.
2. History is per-cluster visibility, keyed like the existing per-draw history
   with the cluster index appended to the draw source key. It is not a
   reprojected previous-frame HZB. A key that is absent from the new cut (a
   different approximation) simply has no history.
3. Phase 1 draws the clusters of the current cut that were visible last frame
   and builds a transient pyramid from that partial depth. Phase 2 tests the
   remaining clusters against it and draws the newly visible ones, so a camera
   cut, a moving occluder, new geometry or a new revision recovers in the
   current frame instead of staying absent because last frame hid it.
4. Visible clusters are compacted per draw into the indirect list.

The logical representation cut is fixed for the view's depth and GBuffer work.
Internal recovery expands the visible subset of that cut and never installs a
different approximation between depth and shading. The draw product carries
the same positions, material clipping and deformation inputs as depth. Shadow
views run the same machinery with light-space history, their own page mapping
and validity; camera-space visibility is never applied to shadow coordinates.

## Verification

- Enumerate the reachable cuts of tiny incidence DAGs, including the
  split-sibling example; reject overlapping source events; parallel disjoint
  rounds match permitted serial orders.
- Partial and out-of-order pages keep coarse coverage; evicting a consumed
  decode-only dependency leaves its installed output selectable; runtime
  dependencies stay protected.
- Bounds and error fixtures cover perspective, orthographic, near plane,
  scale and shear, and invalid input.
- Inject frontier, scratch, total output, material-bin and command-count
  failures at each commit step: the visible cut is a full prior checkpoint or a
  fully committed round; insufficient root bins reject admission.
- Shared boundary cohorts and intentional holes survive every allowed tested
  cut.
- Camera and shadow views choose independent cuts and merge residency demand
  correctly.
- Occlusion with a moving occluder, a camera cut and new geometry recovers in
  the current frame.
- Pressure and coarsening rebuild a valid root-derived cut; finite feasible
  completion demand follows the resource owner's progress contract rather than
  a blanket starvation-free claim.
