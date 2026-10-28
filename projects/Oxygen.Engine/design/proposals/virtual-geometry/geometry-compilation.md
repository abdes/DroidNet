# Virtual geometry compilation

Status: proposal. Part of [virtual geometry](README.md). Owner module: Cooker.
The [geometry format](geometry-format.md) owns the emitted schema and
encoding.

Last updated: 2026-10-28

Read: [pipeline](#pipeline-and-boundaries), [hierarchy](#hierarchy-construction),
[packing and errors](#packing-and-errors), [verification](#verification).

## Pipeline and boundaries

Virtual geometry is an explicit representation profile of the existing
[geometry import pipeline](../../../src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md).
That pipeline already supplies what compilation needs:

- `ImportPlanner` schedules work per job and `AsyncImportService` cancels
  jobs; every work item honors its stop token and returns failure with no
  partial output.
- `MeshBuildPipeline` cooks CPU geometry (LOD validation, vertex expansion,
  normals, material bucketing, spatial mesh-view split into Morton-ordered
  views of at most 4096 triangles) and `GeometryPipeline` finalizes
  descriptors on the thread pool.
- `BufferEmitter` and `AssetEmitter` write buffers and assets;
  `LooseCookedWriter::Finish()` publishes the index.
- Diagnostics are `ImportDiagnostics` entries with a severity and a string
  code; settings are validated against the JSON schemas under
  `Cooker/Import/Schemas`.

Virtual geometry reuses all of it: source identity, resource-table emission,
cancellation and diagnostics. There is no runtime per-cluster asset generation
and no alternate PAK writer. Terrain compilation generates a triangle surface
from authored heights, layers, holes and baked displacement; ordinary triangle
meshes enter the same clustering pipeline through the glTF and FBX adapters.

### Public API and reuse

Public additions are bounded representation and profile fields on the existing
import settings and schema plus the normal result and diagnostic surface.
Clustering, simplification, quantization, packing and job orchestration stay
internal and are not an exported geometry-processing framework. Reuse Data IDs
and manifest views, existing import jobs, cancellation and resource emission,
Serio, Base digests and the owning JSON-schema validator. Internal helpers
are never published as SDK dependencies. A cooker parameter that crosses an
interface and carries a unit is a `NamedType`; Data IDs are not redeclared.
See the [shared rules](README.md#shared-rules).

Consumers of settings must follow the schema chain. The geometry descriptor
schema forbids additional properties, so every new field is declared in the
Cooker schema, registered and installed with the other schemas, and appears
in the installed SDK that the editor reads through `OxygenSdkSchemasDir`. The
editor's content pipeline then adds the field to its manifest builder and
validator. Cooked-format changes require recooking; there is no compatibility
reader.

Pipeline: validate source and schema → canonical surface and boundaries →
finest clusters → replacement groups, simplification and reclustering →
spatial traversal metadata → compatible quantization → page packing and
encoding → validate the decoded artifact → publish descriptor and resources
transactionally.

Inputs are the source revision, deterministic compiler settings, material
domain boundaries, the precision and error profile and boundary-cohort
constraints. Outputs are the Data manifest, source-scoped resource payloads
and bounded diagnostics. An output is publishable only when its manifest
references a complete validated resource set. A cancelled or failed
compilation never replaces an existing usable revision and never leaves its
indices pointing at partially emitted resources.

## Hierarchy construction

Use the [v1 incidence-DAG profile](geometry-format.md#replacement-profile-v1)
as a producer contract: partition the current cluster set, simplify each
group, and assign every reclustered coarse output its generating group. A
later partition may split those siblings among different higher groups, but no
cluster acquires two generating groups or two coarsening owners. Emit and
validate `C_g`/`F_g`, ranks, root closure and independently compatible group
boundaries before packing.

Partition finest triangles into clusters of bounded vertices and triangles
with coherent spatial extent. Limits come from measurement at format freeze,
within at most 256 vertices and 256 triangles
([decision 12](README.md#decisions)); a cluster's limits are independent of
page capacity and hardware wave size.
Material, hole, seam and required authored-boundary constraints are preserved
explicitly, and the cluster's material is its submesh's.

Group adjacent clusters, simplify the group's combined surface, recluster the
result, and record the replacement relation and accumulated error. Repeat only
while bounds, topology and error contracts hold. A group that cannot simplify
becomes terminal; no arbitrary triangle is substituted to manufacture
progress. Irregular group cardinality and shared relations are supported.

Build a separate spatial acceleration structure over the group records.
Replacement semantics are never inferred from BVH ancestry. Record monotonic
bounds and errors sufficient for conservative traversal. Stop conditions,
maximum depth and degree, and page and dependency counts are bounded compiler
settings.

Independent LOD choices do not establish boundary compatibility. The compiler
either preserves shared boundary constraints across allowed replacements or
emits explicit compatible boundary and cohort relationships that selection
enforces. Initial fixtures use constrained boundaries with exactly decoded
shared vertices. Skirts do not hide incompatible topology and precision
matching is not a proof of compatible cuts.

### Library

meshoptimizer is the Cooker's mesh library: the classic model import adopts
it as a pinned Conan requirement for LOD simplification, vertex cache and
fetch ordering and meshopt decoding. Virtual geometry reuses that requirement
and its version; it adds no second mesh library.

clusterlod (`clodBuild`, `clodGroup`) is the candidate cluster builder and
group simplifier. It is not part of the meshoptimizer library or its Conan
package; it ships in the repository's `demo/` directory. A reviewed revision
is vendored into the Cooker, matching the pinned library version, only after
the boundary and error fixtures pass. Oxygen owns the asset contract,
coverage proof, error semantics and qualification; library defaults, optional
sloppy simplifiers, border dilation and terminal behavior are audited against
that contract. See [geometry research](references.md#geometry-research).

## Packing and errors

Establish region-relative quantization domains before final page packing,
quantize shared boundaries compatibly, then reconstruct and validate final
triangles. Measure or derive error on the decoded surface, not just the
pre-quantized simplification. Conservative bounds include rounding and
baked-displacement error. Cone metadata is optional and explicitly invalid
when its assumptions fail.

Pack spatially and co-selection-related clusters into pages within the payload
limit. Do not force one page per cluster or per group. The first corpus must
contain several clusters per page and groups crossing page boundaries. Root
and routing metadata stay discoverable without loading fine payload. Packing
records explicit decode-only and runtime-reference dependencies and cannot
introduce cycles, hidden cross-source dependencies or load-order rules.

Report useful authored diagnostics: the offending source region, material or
boundary, the failed constraint and the requested resource budget. Distinguish
invalid source, unsupported profile, simplification limit, payload overflow,
encoding or validation failure, cancellation and publication failure with
distinct diagnostic codes. A successful asset never relies on the runtime
dropping geometry.

Reproducibility covers source digest, compiler and library revision,
settings, codec version and deterministic tie-breaking. Parallel scheduling
cannot change manifest IDs or payload bytes for the same inputs and profile.
If a chosen library cannot guarantee that, the acceptance decision is made at
format freeze and cache and content identity are not quietly weakened.

## Verification

- Plane, ridge, curved surface, material boundary, hole and cliff fixtures.
- Enumerate the cuts of tiny incidence DAGs, including siblings split across
  higher groups; reject overlapping `C` sets and partial-group replacement;
  independently check decoded topology and boundaries.
- Measured or analytic error and bounds include quantization; an invalid cone
  disables culling.
- Irregular groups, multi-cluster pages and multi-page groups survive
  round-trip decode.
- Serial and parallel compilation emit identical output for a pinned profile.
- Cancellation or failure never publishes an incomplete manifest or resources.
- Two neighboring regions with a revised boundary cannot publish incompatible
  cohorts.

Compilation needs a shallow real hierarchy and a simple encoding, not a
mature terrain editor or an optimal codec. A tiny hand-verified graph fixture
for selection unit tests lives beside cooker-produced fixtures; neither alone
proves the whole producer/consumer path.
