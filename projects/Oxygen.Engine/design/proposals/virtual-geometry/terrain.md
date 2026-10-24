# Clustered terrain proof and terrain boundaries

Status: proposal. Part of [virtual geometry](README.md). The proof workload
that exercises every module, plus the boundary of the separate, later terrain
system.

Last updated: 2026-10-24

Read: [scope](#scope), [fixture](#content-and-runtime-fixture),
[workflow](#runnable-workflow), [observable behavior](#observable-behavior),
[terrain system boundaries](#terrain-system-boundaries).

## Scope

Render terrain through paged adaptive triangle clusters compiled from a
heightfield, and render an accompanying cliff or rock mesh through the same
geometry resources. The workload proves the infrastructure's coverage,
dependency, publication and pressure contracts. A runtime-displaced regular
grid is only a synthetic page producer for pool tests, never the terrain
implementation.

There is no terrain asset, importer or sample in the engine today. The
workload therefore adds a deterministic heightfield source to the geometry
import settings (see [compilation](geometry-compilation.md)), and a runnable
example following the existing conventions: an `Oxygen.Examples.VirtualGeometry`
module under `Examples/` (one `add_subdirectory` line, `DemoModuleBase`-derived
module, `DemoShell` panels and console commands), plus native offscreen
fixtures. The example owns camera and scripted controls; it owns no allocator,
loader or renderer of its own.

### Public API and reuse

The example adds no stable terrain SDK and no public engine page-control API.
It reuses DemoShell, the supported scene and cooker configuration, existing
diagnostics and profiling, and the owning resource interfaces. Script and debug
controls stay example-local; not every allocator knob becomes a renderer
setting. Test adapters use Data and Core identities and the
[strong-type rules](README.md#strong-types); test visibility alone never
justifies exporting internals.

## Content and runtime fixture

A finite region of about one kilometre square with a flat area, a ridge, a
curved area, a material boundary and an authored hole, plus one ordinary
cliff or rock mesh through the same compile, load and render path. The source
terrain is generated deterministically during fixture construction and the
runtime consumes cooked geometry. Small analytic or hand-verified graphs for
unit tests are kept separately.

| Property        | Requirement                                                                                                           |
| --------------- | --------------------------------------------------------------------------------------------------------------------- |
| Hierarchy       | At least three approximation levels in part of the fixture; irregular replacement groups, not a fixed four-child grid |
| Packing         | Several clusters per page, at least one group spanning pages, shared dependency and cohort cases                      |
| Precision       | Compatible lattice and independently decoded boundary checks                                                          |
| Materials       | Static opaque and one masked case using existing shading contracts                                                    |
| Coarse coverage | Guaranteed geometry and routing metadata for admitted regions                                                         |
| Capacity        | Physical pool deliberately smaller than the full detail set; valid minimum and all-protected cases                    |
| Scheduling      | Bounded install, generation and selection counts and bytes; deterministic delay and failure injection in tests        |
| Views           | Two cameras plus a conventional shadow view with an off-camera caster                                                 |
| Rendering       | GPU selection and counted indirect hardware raster into existing depth, GBuffer and shadow products                   |

Cluster size, page count and throughput are not frozen from any earlier grid
example; the format work selects a bounded profile. Budget root and routing
data, fine pages, staging, scratch, selection outputs, old revisions and
captures, not just payload buffers.

Root coverage stays while fine pages are missing. A replacement group becomes
selectable only when its geometry, runtime dependencies and boundary
compatibility are satisfied. Pages may arrive out of order. The workload
exercises the resource owner's single completion permit with reserved logical
capacity, pre-eviction dependency protection and dependency-first immutable
install batches, with production slots GPU-selected. A consumed decode-only
input may be evicted without withdrawing its installed output's
selectability.

It uses the v1 partition/recluster incidence DAG and bounded rank-round
selection, including coarse siblings split across higher groups, not just a
tree fixture. The conservative local-shadow policy refreshes products that
intersect virtual casters even when their cut is stable, while classic-only
disjoint lights keep reuse.

## Runnable workflow

The first runnable proof is a synthetic buffer and texture pool loop; the
final workflow uses the real cooker, Content page loading and virtual
geometry:

1. Cook the deterministic terrain and mesh through the supported tools.
2. Load the scene and manifest; admit coarse region coverage asynchronously.
3. Fly a fixed camera route, stop, teleport and activate a second view.
4. Constrain the pool and work budgets; delay or drop a feedback sample and
   delay page arrivals. Existing coarse coverage stays usable.
5. Move the camera so a caster is off camera while its shadow stays visible.
6. Relocate resident pages without changing content, then publish a prepared
   changed approximation or revision fixture and observe distinct
   invalidation behavior.
7. Close and reopen the view and destroy the scene with deliberately pending
   work.

One diagnostic panel shows admitted regions, selected versus desired detail,
resident, protected and producing page counts, bytes by category, deferred
demand and overflow and failure counters, with debug colour modes for groups,
pages and selection. The host already presents frame statistics and GPU work
timing in its viewport toolbar and overlay, so the panel does not duplicate
them. Internal allocation identities are not ordinary product UI. Status is
transported asynchronously and diagnostics can be disabled without affecting
resident rendering.

No terrain editor, erosion, physics cooking, live sculpting, full world
streaming, advanced codec, virtual texture or composed material cache is
required here. Prepared revision fixtures exercise publication without
pretending the complete editing pipeline exists. Intended holes are preserved;
missing coverage is never hidden with skirts, duplicated parents, placeholder
triangles or sky-coloured gaps.

## Observable behavior

- Exact abstract coverage and allowed boundaries before visibility culling.
- No accidental holes and no overlapping parent and child representation under
  pressure.
- Independent numerical decode, position, bounds and error tests and
  shared-lattice tests.
- Depth and GBuffer agree, including masked coverage.
- Stable stationary work: no needless reinstallation or regeneration after
  demand settles.
- No resource, descriptor or pin growth after warmup with fixed admitted inputs.
- No render-thread wait for allocation, readback or I/O; bounded GPU outputs.
- Delayed, out-of-order loading, teleport and lost feedback preserve coarse
  coverage.
- Independent camera and shadow cuts and merged page demand.
- Relocation-neutral surface identity and a coherent old/new revision
  transition.
- Clean completion, discard and fault handling and teardown under delayed GPU
  uses.
- Completion of a feasible admitted group under bounded delivery, without
  split-pinning the two-slot adversarial case.
- Delayed installs survive ordinary Upload ring reuse, including rejected
  entries with dropped acknowledgements.
- First-frame shadow correctness when arriving detail changes only the GPU
  shadow approximation.

## Terrain system boundaries

Heightfields, source layers, holes and authored displacement are authoring and
simulation inputs. Rendering uses compiled adaptive triangle and cluster
hierarchies with compressed pages and guaranteed coarse coverage. Rock, cliff
and cave meshes share virtual geometry; the heightfield does not constrain
their topology or create a scene object per cluster.

Terrain owns source topology, authoring semantics, compilation settings,
spatial materials, terrain-specific LOD policy and collision coordination.
Nexus and Graphics own the shared paging and lifetime mechanisms, not those
policies. The selected surface is a valid compatible cut; page residency alone
does not make a group selectable. Cluster size, page capacity and authoring
region size stay independent. Baked displacement participates in geometry,
bounds and error, and quantization reconstructs shared boundaries
compatibly. Compilation can increase storage and edit cost and is not assumed
cheaper than a regular heightfield representation.

### Edits and revisions

An edit identifies the changed source region, the required boundary
neighbourhood and the dependent hierarchy ancestors. Replacement data is built
under a new immutable revision, and cancelled or superseded builds cannot
publish. Its boundary cohort is validated against adjacent retained regions
before representations switch. Rendering keeps the old coherent revision until
replacement readiness and memory-overlap admission are satisfied. Publication
is a region or cohort transaction, not a whole-world atomic rebuild and not
independent arbitrary page swaps. Source surface identity survives packing;
`ClusterId` need not survive recooking.

Gameplay-affecting changes need a separately coordinated collision revision.
Authoring intent, compiled render data and collision data share a transaction
identity but have different producers and readiness, and the gameplay owner
defines when the collision switch occurs and the permitted render and physics
skew. Collision reuses the existing heightfield physics resources
(`HeightFieldParams` in the Data physics formats) rather than a parallel
representation. Undo and redo, save and load, cooking and live sync must
complete together before editor authoring can be called delivered.

### Material and deformation

Material coordinates are stable in source or surface space across topology
changes. Virtual texture residency manages source material data. A composed
surface cache can store sufficiently stable expensive combinations of layers,
roads and contributors. Their resolution, update cadence, pages and budgets
are independent of geometry.

Time-varying displacement or deformation needs bounded movement, conservative
bounds and error, previous-state evaluation, and matching geometry in depth,
GBuffer and shadow passes. Arbitrary vertex-shader motion is not compatible
with a precomputed hierarchy or cone bounds, and unsupported deformation is an
explicit failure. Mesh shaders are an optional execution profile; a visibility
buffer or software micropolygon rasterizer is a separate renderer decision;
neither is a prerequisite for GPU residency or clustered selection. Terrain
never bypasses Vortex's material, lighting, velocity, depth or shadow
publication contracts.

### Extension points

| Topic                                    | What it needs from this design                                                      | Owner                   |
| ---------------------------------------- | ----------------------------------------------------------------------------------- | ----------------------- |
| Deep hierarchy and world streaming       | Region admission for many manifests, large-coordinate precision and error policy    | Geometry, Terrain       |
| Source editing                           | Cancellation, boundary rebuild cohorts, persistence on top of revision transactions | Terrain, Cooker, Editor |
| Collision and queries                    | Coordinated collision revision publication                                          | Terrain, Physics        |
| Virtual textures, layers, composed cache | Their own paging domains over the same pool contracts                               | Materials, Terrain      |
| Qualified dynamic deformation            | Previous-state geometry in every pass and temporal and shadow responses             | Geometry, Rendering     |
| Mesh shaders and compression             | Graphics mesh dispatch, measured benefit                                            | Graphics, Geometry      |
| Sparse backing and driver residency      | Pressure evidence that justifies reserved resources                                 | Graphics, Nexus         |
| GPU-driven VSM                           | The shared page kernels; VSM keeps its own pool resource and shadow policy          | Shadows                 |

Each of these needs its own design, user workflow and measurable exit before
it is built; none is absorbed into this design.
