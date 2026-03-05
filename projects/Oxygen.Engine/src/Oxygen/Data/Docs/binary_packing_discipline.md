# Binary Packing Discipline for PakFormat Structures

## Mandatory Rules

All `PakFormat_*.h` structures MUST follow these rules.

Scope: this document defines binary format layout rules only.

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
