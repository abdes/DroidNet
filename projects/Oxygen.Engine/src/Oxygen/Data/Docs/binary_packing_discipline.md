# Binary Packing Discipline for PakFormat Structures

Read [current rules](#mandatory-rules) and the implemented
[descriptor-local reference format](#descriptor-local-references-m08f1).

## Mandatory Rules

All `PakFormat_*.h` structures MUST follow these rules.

Scope: this document defines binary format layout rules only.

### Material-slot records

Geometry descriptors store a non-nil 16-byte `MaterialSlotId` in each
`SubMeshDesc`. Repeated IDs explicitly bind one semantic slot across LODs;
equal labels or default materials never establish identity. The loader builds
and validates the geometry-owned inventory once, including its canonical layout
revision. Slot provenance and source-layout witnesses are import data, not
runtime payloads.

Scene descriptors store material assignments in a separate component table:
node index, SlotId, material asset key and the last resolved layout revision.
Renderable records contain geometry and visibility only. An absent assignment
uses each binding's geometry default. Readers reject retired format versions;
maintained source content is migrated and recooked.

### Rule 1: Use `#pragma pack(1)`

```cpp
#pragma pack(push, 1)
struct MyFormatStruct {
  // fields
};
#pragma pack(pop)
static_assert(sizeof(MyFormatStruct) == EXPECTED_SIZE);
```

### Rule 2: Size Verification

Every packed structure MUST have `static_assert(sizeof(...) == N)`.

### Rule 3: No Unjustified Reserves

Reserve fields MUST NOT be added without explicit justification.

**Valid justifications**:

1. Union arm padding (mandatory)
2. Power-of-2 sizing (64, 128, 256 bytes) or cache line alignment (64 bytes)

**Invalid justifications**:

- "Might need space later" / "Forward compatibility"
- "Makes the size look nice"

**Default**: Omit reserves. Use versioning for format evolution.

### Rule 4: Reserve Naming

When reserves are justified, name them `_reserved` (singular, underscore-prefixed) with a comment explaining why.

```cpp
uint8_t _reserved[48] = {};  // Round to 64-byte cache line
```

Forbidden for reserve fields: `reserved`, `reserved0`, `reserved1`, `_padding`, `_pad`, or reserves without justification comments.

### Rule 5: Reserve Placement

Reserves MUST appear at the END of the structure, after all active fields.

### Rule 6: Reserve Initialization

All reserve fields MUST be zero-initialized: `= {}`.

### Rule 7: Union Arm Padding (Mandatory)

All union arms MUST be padded to the same size using `_reserved` at the end of each arm.

```cpp
#pragma pack(push, 1)
union ShapeParams {
  struct { float radius; float _reserved[19] = {}; } sphere;    // 80 bytes
  struct { float extents[3]; float _reserved[17] = {}; } box;   // 80 bytes
};
#pragma pack(pop)
static_assert(sizeof(ShapeParams::sphere) == sizeof(ShapeParams::box));
```

Ensures union arm sizes are deterministic and reserve bytes are explicitly defined.

### Rule 8: Alignment Padding

For internal alignment requirements (rare with pack(1)), use `_pad_<reason>`:

```cpp
uint8_t _pad_for_alignment[3] = {};
```

`_pad_<reason>` is allowed only for internal alignment padding and MUST NOT be used as reserve space.

### Rule 9: Field Ordering

Order fields by descending size when possible to minimize alignment surprises.

---

## Enforcement Checklist

When adding/modifying PakFormat structures:

- [ ] Uses `#pragma pack(push, 1)` and `#pragma pack(pop)`
- [ ] Has `static_assert(sizeof(...) == N)`
- [ ] Reserve fields (if any) named `_reserved` with justification comment
- [ ] Reserve fields (if any) at END of structure
- [ ] Reserve fields zero-initialized `= {}`
- [ ] Union arms padded to equal size
- [ ] Fields ordered by descending size

## Descriptor-local references (M08.F1)

Native producers, readers and packaging implement this layout. The
[format milestone](../../../../../../design/editor/plan/ED-M08.F1-descriptor-local-references.md)
owns the remaining editor authoring and qualification.

[PakFormatVersions.inc](../PakFormatVersions.inc) defines the current container,
index and asset versions. Native format declarations, producers and inspection
tools consume this catalog. Tools accept only those current versions. Format changes require rebuilding tools and recooking
content, without compatibility readers.

### Reference representation

Numeric resource fields become strongly typed, zero-based **descriptor-local**
indices. `UINT32_MAX` means absent; local index zero is a valid first binding.
Do not change Core's container-index sentinel globally. Stable `AssetKey` fields,
including physics payload identities, remain keys.

Each PAK/loose asset directory entry adds a reference-block offset (`uint64`),
resource-binding count (`uint32`) and key-reference count (`uint32`). Offsets are
relative to the containing PAK/index file, never to a machine path. Empty blocks
use zero offset and counts. The block contains these packed arrays in order:

| Record           | Fields                                                      | Size     |
| ---------------- | ----------------------------------------------------------- | -------- |
| Resource binding | `ResourceKind:uint8`, container index:`uint32`              | 5 bytes  |
| Key reference    | `AssetKey`, target kind:`uint8`, expected `AssetType:uint8` | 18 bytes |

Wire resource kinds are explicitly numbered Buffer=1, Texture=2, Script=3,
Physics=4. They are independent of C++ `TypeId` and typelist order. Audio has no
current descriptor/runtime consumer and is not assigned a supported binding kind.
Key target kinds are Asset=1, PhysicsResource=2 and Logical=3. Asset references
require a concrete expected asset type; PhysicsResource requires Unknown; Logical
may carry a known expected asset type or Unknown when no asset type exists.

An absent field creates no binding. A texture binding with index zero selects
Oxygen's existing neutral white fallback; it is a built-in marker, not a resource
payload to relocate. Preserve its authored distinction from absence, while keeping
the material binder's existing per-slot behavior (normal/ORM slots skip neutral
fallback sampling). A texture binding with container
index `UINT32_MAX` explicitly selects Oxygen's existing shared magenta/black error
texture. It has no container payload; packaging preserves it and runtime loading
retains the existing error-texture behavior. This marker is distinct from an
absent descriptor-local reference even though their numeric sentinels match:
the two values belong to different index domains. Other out-of-range physical
indices are malformed, not requests for substitution.

Producers intern bindings by `(kind, container index)` within each descriptor and
emit deterministic first-use order. Repacking preserves those slots: two local
slots may become aliases after resource deduplication, so readers permit equal
targets without merging or reordering slots. Key references are
deduplicated and sorted by target kind, key and expected type; conflicting types
for the same target are invalid.

The PAK directory remains 64 bytes by consuming 16 bytes of its existing reserve.
The loose asset entry grows from 65 to 81 bytes. Reference blocks are index data,
not new per-asset sidecar files. The existing index/file integrity boundary covers
them; descriptor digests continue to cover descriptor bytes alone.

### Complete field inventory

The producing serializer emits reference metadata alongside its typed descriptor;
packaging never discovers references by walking byte offsets. The typed decoder
checks the declared inventory against actual fields before asset publication.

| Descriptor/record     | Local resource bindings                                                        | Key references                                                                                                                                                |
| --------------------- | ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Material              | All 12 texture fields, including AO and extension textures                     | None                                                                                                                                                          |
| Standard mesh         | Vertex and index buffers                                                       | Submesh default materials                                                                                                                                     |
| Skinned mesh          | Vertex, index, joint-index, joint-weight, inverse-bind and joint-remap buffers | Submesh materials; skeleton key is logical while no skeleton asset type is supported                                                                          |
| Procedural mesh       | None                                                                           | Submesh default materials                                                                                                                                     |
| Scene                 | Exposure metering mask                                                         | Renderable geometry, material overrides and embedded script-slot assets                                                                                       |
| Scene environment     | None beyond the exposure mask                                                  | Existing fog/sky cubemap keys are logical: current cooked decoding does not resolve them to a texture asset type                                              |
| Script                | Embedded bytecode and source resources                                         | None; external source paths remain paths, not asset dependencies                                                                                              |
| Input action          | None                                                                           | None                                                                                                                                                          |
| Input mapping context | None                                                                           | Mapping actions, linked trigger actions and auxiliary trigger actions                                                                                         |
| Physics material      | None                                                                           | None                                                                                                                                                          |
| Collision shape       | None                                                                           | Material asset; cooked and compound-child payload keys target physics resources                                                                               |
| Physics scene         | None                                                                           | Body/collider/character shape and material assets; soft-body topology and joint/vehicle constraint keys target physics resources; target-scene key is logical |

Nil optional keys are omitted. Required-field validation remains with the typed
owner. Node IDs, material-slot IDs, source IDs, node indices and descriptor-relative
offsets are identities/ranges, not external dependency edges. Logical references
are reported but neither eagerly loaded nor included in owning-cycle detection.
Their classification does not deliver a missing rendering feature or weaken the
existing target-scene hash check.

### Scene-local scripts

Add a descriptor-relative script-slot table to `SceneAssetDesc`. Component slot
ranges index that table. `ScriptSlotRecord.params_array_offset` becomes relative
to the scene descriptor; parameters live inside the same descriptor. Validate
record sizes, arithmetic, bounds and disjoint owned ranges. Empty slots/parameters
use canonical empty ranges. Include these ranges when locating the trailing
environment block and computing the complete scene descriptor size.

Remove the container script-slot table and loose `script-bindings.table/data`
roles. Script bytecode/source resource tables remain. Scripting sidecar import
rebuilds the target scene's local script section and regenerates its metadata;
it does not relocate another scene's slots. Physics sidecars are cooked against
the final scene descriptor hash, after script updates.

### Validation and runtime ownership

- Preserve model-import recovery: missing or failed authored textures select the
  error texture and retain diagnostics; the model remains usable. Keep the
  existing explicit white-substitution option. Reimport after source repair
  replaces the special binding with the real texture. Ordinary unassigned slots
  retain their scalar/neutral defaults; loading placeholders remain transient.
- Validate block extents with checked arithmetic before allocating; reject
  unknown kinds, overlap with structural sections, invalid target types,
  duplicate/conflicting key references and invalid physical indices. Texture fallback/error markers are
  validated as built-ins without indexing a resource table. Typed decode rejects absent required
  fields, out-of-range local indices, wrong-kind bindings and incomplete or
  extraneous inventories.
- Validate hard key targets and cycles against the selected ordered source set,
  including declared base sources for patch builds. Independent library roots
  need not duplicate each other's descriptors. Logical links are not hard edges.
- Data owns decoded immutable reference metadata. Content resolves bindings
  through the existing source-instance identity registry and dependency ownership
  before publishing the asset. Preserve generation isolation and cancellation.
- Keep wire-local indices distinct from resolved runtime indices/keys. Material,
  geometry, script and environment consumers receive their existing resolved
  resources; no binding-table lookup is added to rendering or shader evaluation.
- Inspector reads dependency metadata without loading resource payloads. Full
  descriptor/inventory agreement belongs to typed validation, not badge refresh.

### Embedded layer catalog

Every PAK8 contains a native `PakCatalog`, including empty and deletion-only
archives. Two 64-bit footer fields locate it; the footer stays 256 bytes and its
CRC offset is unchanged. Store the catalog after directory/browse data and before
the footer. Readers reject missing catalogs and overlapping/out-of-range storage.

The packed catalog contains the current catalog version, SourceKey, content
version, three 32-bit counts, followed by entries, deleted keys and ordered base
records, then its SHA-256 digest. Entries store AssetKey, 8-bit AssetType and two
SHA-256 digests (descriptor and resource dependencies). A base record stores
SourceKey, 16-bit content version and catalog digest. Canonical serialization
sorts entries/deletions by key but preserves base precedence. The trailing digest
covers all preceding canonical bytes, excluding itself.

Entries and deletions are disjoint and duplicate-free; bases cannot repeat or
reference the containing archive. The catalog identity and key/type inventory
must match the PAK header/directory. `Data::PakCatalog` owns native encoding,
decoding and digest validation; Cooker JSON is an inspection representation of
that same metadata. Runtime layer selection belongs to Content.

### Cutover and precedent

Use PAK v8 and loose index v3. Bump material, geometry, scene and script descriptor
versions for their changed reference semantics/layout; unchanged key-only layouts
retain their own versions. All shipping readers accept only the new current
container formats. Update Serio codecs, native producers/readers, Inspector,
native fixture writers together, then recook maintained content. Do not add
legacy decoding or infer a binding table from an old descriptor.

UE5.7's `CoreUObject/Public/Serialization/AsyncLoading2.h` provides the relevant
precedent: `FPackageImportReference` separates local import indices from resolved
identity; `FZenPackageSummary` locates import and dependency tables. Oxygen uses
that separation with its existing asset/resource model, without importing Unreal's
object model, export scheduler or reflection system.
