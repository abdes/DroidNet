# Binary Packing Discipline for PakFormat Structures

Read [current rules](#mandatory-rules) and the separately planned
[M08.F1 reference format](#planned-descriptor-local-references-m08f1).

## Mandatory Rules

All `PakFormat_*.h` structures MUST follow these rules.

Scope: this document defines binary format layout rules only.

### Material-slot records

Geometry version 2 stores a non-nil 16-byte `MaterialSlotId` in each
`SubMeshDesc`. Repeated IDs explicitly bind one semantic slot across LODs;
equal labels or default materials never establish identity. The loader builds
and validates the geometry-owned inventory once, including its canonical layout
revision. Slot provenance and source-layout witnesses are import data, not
runtime payloads.

Scene version 9 stores material assignments in a separate component table:
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

## Planned descriptor-local references (M08.F1)

Status: planned, after M08.1.9. The separate
[format milestone](../../../../../../design/editor/plan/ED-M08.F1-descriptor-local-references.md)
owns delivery and cutover. This is not the current wire layout.

Resource-bearing descriptor fields use a uint32 local reference index. UINT32_MAX
means absent; an explicit fallback binding remains distinct. Each asset directory
entry locates its binding table. A binding identifies an explicit uint8 ResourceKind
and uint32 container resource index; native validation checks kind, count, extent
and placement before decode. Native producers also emit complete direct asset
reference metadata. Stable AssetKey values remain stable across packaging.

Data owns immutable decoded binding metadata. Content resolves bindings into runtime
identities once; shaders and rendering hot paths do not perform a new table lookup.
Do not use process-dependent TypeId values in the wire contract or introduce a
reflection framework. F1.1 enumerates every supported reference-bearing field and
separates hard runtime dependencies from non-owning logical references.

Scene script-slot/parameter arrays move into the scene descriptor with relative
offsets. Packaging can then preserve descriptor bytes and digests while moving
resource bindings. All affected format versions change together; update native
producers/readers, Inspector and PakGen, recook content and reject prior versions.
