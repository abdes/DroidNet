# Virtual geometry format

Status: proposal. Part of [virtual geometry](README.md). Owner module: Data.
This is the review baseline for the cooked profile; production assets are not
emitted until its layout and version are frozen in the Data version catalog.

Last updated: 2026-10-24

Read: [identity and units](#identity-and-units), [manifest](#manifest),
[pages and clusters](#page-and-cluster-encoding), [validation](#validation),
[versioning](#versioning-and-publication).

## Identity and units

Today a `GeometryAsset` holds one `Mesh` per LOD (`MeshAt(LodIndex)`), each
with `SubMesh` and `MeshView` records; its cooked form is `GeometryAssetDesc`
followed by `MeshDesc[lod_count]`, `SurfaceDesc[surface_count]` and the string
table, with vertex and index data in `BufferResource`s named by the asset's
reference block. The virtual representation extends that asset; it does not
replace it:

- A geometry asset may carry a **virtual representation** next to, or instead
  of, its classic LOD meshes. A consumer chooses a declared supported
  representation. It never silently substitutes a differently shaped one;
  asset policy decides which to use when materials or deformation are
  unsupported by the virtual path.
- Each asset stays one first-class asset with resource payloads. Clusters and
  pages are neither scene nodes nor assets, and page payloads are resources,
  not assets.
- A cluster belongs to exactly one `SubMesh`, which supplies its material.
  Surface ownership reuses the existing `SurfaceId` and `SurfaceDesc`
  declarations, so edits, shadows and temporal logic share one authored
  surface identity with classic meshes.

### Public API and reuse

The new Data surface is the representation accessor on `GeometryAsset`,
immutable validated manifest views, profile metadata and the IDs real readers
use. Builder state, codec internals and raw-buffer ownership stay private.
`PageId`, `ClusterId`, `GroupId`, `TraversalNodeId` and `BoundaryCohortId` are
distinct Base `NamedType`s declared once in Data and consumed unchanged by
Cooker, Content and Vortex. Identity reuses Core `AssetKey`, Data `SourceKey`
and `SurfaceId`, and the asset reference block. `ResourceKey` is Content's
opaque loader-lifetime key and is never serialized or encoded into a cooked
page identity. Hashing and digests come from Base, serialization from Serio
and the Data packing rules; no new UUID, hash or serialization framework. See
the [shared rules](README.md#shared-rules).

Identity and references:

- All resources of a virtual representation come from the same mounted
  source, bound through the asset's reference block as ordinary buffer
  resources (`ResourceKind` plus container index). No cross-source resource
  index ever reaches the renderer.
- `AssetKey` plus the source snapshot and manifest revision identify a
  manifest. Page, cluster and group IDs are stable within that immutable
  manifest; a recook may assign new IDs. Cross-revision cluster identity is
  not promised and is never derived from physical addresses.

| Unit              | Identity and role                                                                                                             |
| ----------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Page              | Manifest-local `PageId`; bounded transfer and residency unit                                                                  |
| Cluster           | Manifest-local `ClusterId`; decode, culling and raster unit (about 128 triangles), located by `PageId` and local record index |
| Replacement group | `GroupId`; coarse representation and the compatible refinement relationship                                                   |
| Traversal node    | `TraversalNodeId`; spatial acceleration over groups, independent of replacement topology                                      |
| Boundary cohort   | `BoundaryCohortId`; set whose boundary encoding and revision must agree                                                       |

The cluster is deliberately the same unit as the accepted
[cluster culling design](../../vortex/lld/occlusion.md#101-cluster-culling-through-vertex-pulling-vx-occ-05):
local bounds, a normal cone and a fixed triangle budget so a draw of
`visible_clusters × 128 × 3` vertices decodes cluster and triangle from
`SV_VertexID`. A classic mesh compiled to this format has only finest
clusters, all resident, and no groups above them.

### Replacement profile v1

V1 is a partition/recluster incidence DAG. For each group `g` the format
encodes the complete coarse cluster set `C_g` and the complete finer input set
`F_g`. The collective replacement `C_g → F_g` is certified by
simplification and reclustering as one surface correspondence. Invented
disjoint finest-triangle masks per coarse cluster are not used: reclustering
moves partition boundaries.

- Each non-finest cluster has exactly one generating group whose `C` set
  contains it. Each non-root cluster has exactly one next coarsening owner
  whose `F` set contains it. Finest clusters have no generating group; roots
  have no coarsening owner.
- Group sides are nonempty and duplicate-free. A finite rank increases
  strictly from coarse to fine along replacement dependencies, and every group
  lies in the certified root-to-finest closure. Traversal BVH ancestry is
  independent.
- Irregular DAGs are legal: coarse siblings produced by one group may enter
  different higher groups. Competing rewrites that share a coarse input are
  malformed: with `C_g={a,b}` and `C_h={b,c}`, `b` would have two generating
  groups. A nested tree would needlessly constrain reclustering, and general
  competing rewrites would need a conflict solver; neither is the profile.

The root cluster set `R` certifies complete coarse coverage. Legal cuts are
the markings reachable from `R` by whole-group replacements. Reachability does
not prove triangle, boundary or hole correctness: the Cooker certifies each
collective substitution under the decoded error and topology contract. V1
preserves boundaries independently for each substitution; a required joint
boundary change is compiled into one bounded legal transition or rejected as
unsupported.

A page may hold clusters of several groups, and a group may need several
pages. Neither fixes a child count, page size or cluster size.

## Manifest

The manifest owns the format version, source-content digest and revision,
compiler and codec profile IDs, counts, bounds, quantization domains, root
representations, the group graph, cluster directory, page directory and
dependency and boundary tables. Typed decoded Data wrappers expose them with
no Graphics objects or renderer-private headers.

| Record              | Required fields and semantics                                                                                                                                                             |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Header              | magic, major/minor, header bytes, total descriptor bytes, profile ID, source revision and digest, checked table offsets and counts, reserved zeros                                        |
| Page directory      | `PageId`, resource reference, byte range, transfer and resident byte counts, codec and version, payload integrity digest, cluster range, dependency range                                 |
| Cluster directory   | `ClusterId`, `PageId` and local record index, submesh (material) index, `SurfaceId` reference, conservative bounds, cone validity, decode metadata index                                  |
| Group               | `GroupId`, complete `C_g`/`F_g` ranges, rank, geometric error and bounds, required page set, boundary cohort references; generating and coarsening owners stored or validated derivations |
| Traversal node      | bounds and error bound, child or group-reference range, leaf discriminator                                                                                                                |
| Dependency          | target `PageId` and lifetime kind: decode-only or runtime-reference                                                                                                                       |
| Quantization domain | integer-lattice origin and scale, precision and range, boundary-domain identity, reconstruction convention                                                                                |
| Root coverage       | admitted region's compatible coarse representation, routing metadata, transitive required resource set                                                                                    |

Layouts follow the Data
[binary packing discipline](../../../src/Oxygen/Data/Docs/binary_packing_discipline.md):
fixed-width little-endian scalars in packed structs with a `static_assert` on
every size, checked ranges, no `size_t`, pointers, `bool` or ABI-dependent
padding. The format work supplies byte and offset tables and generated schema
checks before any consumer relies on a layout, and the version tag lives in
`PakFormatVersions.inc` (the asset gets a new `kGeometryAssetVersion`), never
invented by Cooker or Content.

The GPU runtime page directory is derived from the manifest. It carries no
PAK file offsets and no backend addresses. Nexus owns the `PageKey` layout in
[page residency](page-residency.md#identity-and-wire-contract).

## Page and cluster encoding

A fixed maximum resident page payload is a profile parameter, with 64 KiB as
the measurement starting point. It is software page capacity, not a hardware
sparse-tile requirement. Actual transfer and useful resident bytes vary. Every
cluster lies wholly inside one resident page in v1; groups may span pages.
Packing splits an oversized cluster or group construction before encoding and
never emits a continuation chain hidden from validation.

Resident payload holds a checked header, a cluster-record directory, compact
triangle indices, quantized positions and the required attributes. The header
gives record counts, section offsets and bytes, and the resident-format
version. GPU loads stay aligned to their declared view or typed-load
requirement; byte-sized triangle indices need a documented aligned extraction
path. Layout and codec are independent of mesh-shader group dimensions.

The first transfer codec may be the identity over the compact quantized
resident format. Advanced entropy or inter-page compression is optional; a
codec defines maximum decoded bytes, scratch, malformed-input behavior and an
independent decoder, and declares any dependencies explicitly. Compressed
transfer completion is not installed resident content.

Positions reconstruct on compatible region-relative integer lattices. Shared
boundary vertices use exactly compatible integer coordinates and scale and
canonical decode arithmetic before camera-relative reconstruction. Unrelated
per-cluster AABB quantizers are forbidden at shared boundaries. Lattice
precision error is part of group error and conservative bounds. Quantization
compatibility does not prove topology compatibility; they are separate
validation obligations.

Groups carry accumulated error to the finest authored surface, including
simplification, quantization and any baked displacement approximation.
Selection bounds are monotonic and conservative. A library's heuristic
simplification metric is not automatically a certified bound: the profile
states and qualifies its guarantee. An uncertified estimate can prioritize
work but cannot be presented as a conservative bound or authorize an
error-based rejection that requires one. Backface cones carry a validity flag
and apply only for supported sidedness and deformation; an invalid cone
disables that optimization.

For per-axis nearest-rounded lattice steps `sx, sy, sz`, quantization adds at
most `0.5 * sqrt(sx² + sy² + sz²)` of positional error before further
encoding and transform error. Reconstruct the shared integer coordinate
before applying its common scale and origin: different float addition orders
across page boundaries must not create incompatible positions. Scale local
error to world error with a conservative linear-transform norm, never a single
axis scale under shear. Tests include range extremes and region-boundary
translations.

## Validation

Validate schema and record shape first, then semantic invariants. Reject:

- overflow, forbidden section overlap, truncated payload, out-of-range IDs,
  unknown required format, codec or feature, nonzero reserved fields;
- nonfinite bounds or errors, negative errors, inverted bounds, invalid
  quantization ranges, indices outside a cluster, resident bytes beyond the
  pool profile;
- cycles in replacement or required decoding dependencies; duplicate or
  ambiguous generating or coarsening ownership; invalid rank, membership or
  root closure; illegal cross-source references; missing root coverage or
  routing metadata;
- incompatible boundary cohorts, inconsistent shared decode coordinates,
  material ranges or unqualified topology-changing simplification;
- refinement requirements beyond the declared admission and progress limits
  without a usable coarse representation.

Bounds encompass decoded triangles and the declared deformation envelope.
Hole boundaries are authored boundaries: coarse coverage preserves them and
does not fill them as accidental gaps. Validators distinguish intended holes
from missing streamed coverage.

Verification: byte/offset golden files; an independent decoder and malformed
inputs; acyclic graph and range checks; quantized boundary equivalence;
decoded bounds and error fixtures; root, cohort and dependency completeness.
Golden assets are small reproducible inputs, not archives of arbitrary cooker
output.

## Versioning and publication

Asset revision, selected approximation and physical page generation are
distinct. Changing packing or physical placement alone does not change the
represented surface. Runtime aliases use manifest-local IDs and a namespace
generation, so stale I/O or install results cannot attach to a new asset
incarnation.

Readers accept only the current cooked formats; there is no compatibility
reader or legacy fallback, so a format change means recooking maintained
content. For a recook the old manifest and its dependency closure stay usable
until the new coherent region or cohort is ready. Publication swaps an
immutable revision token and never mixes page directories from two manifests.
The old/new overlap memory is admitted explicitly, and failure preserves the
old revision. Cross-region edit and cohort construction and collision
publication belong to the later terrain contract, not to a permissive loader
fallback.
