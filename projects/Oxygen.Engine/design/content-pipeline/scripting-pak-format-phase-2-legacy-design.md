# Scripting PAK Format (Phase 2 Legacy Design)

**Date:** 2026-02-28
**Status:** Outdated / Superseded
**Superseded By:** `design/content-pipeline/scene-scripting-cooking-design.md`

## Scope

This document specifies how scripting data is represented in PAK/loose-cooked
content for Oxygen Phase 2.

Goals:

- Add a dedicated scripts resource region/table in the container format.
- Keep script bytes out of scene/asset descriptors.
- **Model one scripting component per node** using strict fixed-size records.
- **Use Fixed-Size C Unions** for all binary script parameters to ensure cross-tool predictability.
- **Use Serio** for script resource descriptors (metadata) only.
- Keep scene decoding and dependency publication aligned with existing loader
  architecture.

Non-Goals:

- Runtime script execution. This document covers data format and loading only.
- Editor authoring UI for script parameters.
- Hot-reload protocol.

---

## 1. Architectural Overview

The design uses a **Split Table Model** where lightweight scene components
reference a global, deduplicated table of script slot instances.

### Relationship Chain

1. **Scene Node ↔ Scripting Component (1 : 0..1)**
   - An optional, lightweight tag attached to a scene node.
   - **Fixed-Size** record (16 bytes). Lives in the scene's component table.
   - Says: "I possess a range of scripts starting at Global Slot Index `X` with
     `N` entries."

2. **Scripting Component ↔ Script Slots (1 : N, where N > 0)**
   - A contiguous range of indices into the **Global Script Slot Table** stored
     in the PAK footer.
   - Runtime look-up is **O(1)** — pointer arithmetic on a flat global array.

3. **Script Slot ↔ Script Asset (N : 1)**
   - The **Fixed-Size** `ScriptSlotRecord` (128 bytes) in the global table.
   - Binds a **Script Asset** (bytecode/source) to a **Parameter Array**
     (instance data).
   - 5,000 "Orc" entities can all point to the *same* `OrcBehavior` asset key.

4. **Script Slot ↔ Parameters (N : 1)**
   - A binary array containing variable overrides (e.g., `Speed = 50.0`).
   - If 4,999 Orcs have default stats, they all share the **same** global slot
     index (and the same param array). If 1 Orc has `Speed = 100.0`, the cooker
     creates a **unique** slot+array for that override.
   - Format: **Fixed-Size C Union Records (`ScriptParamRecord`)** (see §5).

### Diagram

```text
[Scene Node]
  |
  +-- [ScriptingComponentRecord] (Local to Scene Component Table)
        |
        +-- slot_start_index = 100
        +-- slot_count = 2
              |
              v
[Global Script Slot Table] (In PakFooter)
  | (Slot 100)
  +-- [ScriptSlotRecord] (128 bytes)
  |     +-- script_asset_key = {guid}
  |     +-- params_array_offset = 0x8000 (Absolute)
  |     +-- params_count = 2
  |
  | (Slot 101)
  +-- [ScriptSlotRecord] (128 bytes)
        +-- script_asset_key = {guid}
        +-- params_array_offset = 0x8100 (Absolute)
        +-- params_count = 1
              |
              v
[Script Region] (Binary Data Region in PAK)
  |
  +-- [0x8000: Params Array A] (2 x ScriptParamRecord)
  |     Record[0]: { "aggressive", Bool(true) }
  |     Record[1]: { "patrol_radius", Float(15.0) }
  |
  +-- [0x8100: Params Array B] (1 x ScriptParamRecord)
        Record[0]: { "rate", Float(5.0) }
```

---

## 2. Container-Level Changes

### 2.1 Version Bump

**Introduce PAK format version 5** (`PakHeader::version = 5`).

Version lineage: v2 → v3 (environment block) → v4 (texture payloads,
material v2) → **v5 (scripting)**.

v5 PAK files cannot be read by v4 loaders. v5 loaders MAY choose to support
reading v4 PAK files (no script region/table — treat as "no scripts").

### 2.2 PakHeader (v5)

```cpp
namespace oxygen::data::pak::v7 {

using namespace v7;

#pragma pack(push, 1)
struct PakHeader {
  char magic[8] = { 'O', 'X', 'P', 'A', 'K', 0, 0, 0 };
  uint16_t version = 5;           // Format version
  uint16_t content_version = 0;   // Content version
  uint8_t guid[16] = {};          // Unique identifier for this PAK
  uint8_t reserved[228] = {};     // Reserved for future use
};
#pragma pack(pop)
static_assert(sizeof(PakHeader) == 256);
```

### 2.3 PakFooter (v5)

The v4 `PakFooter` is exactly 256 bytes. v4 has 108 bytes of `reserved` space
before `pak_crc32` and `footer_magic`. v5 consumes 48 bytes of that reserved
space (3 × 16 bytes) to add two new tables and one new region.

**Footer reserved-space accounting:**

| Field                    | Size | v4 offset | v5 offset |
|:-------------------------|-----:|----------:|----------:|
| `script_region`          | 16   | —         | 72  |
| `script_resource_table`  | 16   | —         | 136 |
| `script_slot_table`      | 16   | —         | 152 |
| `browse_index_offset`    | 8    | 120       | 168 |
| `browse_index_size`      | 8    | 128       | 176 |
| `reserved` (remaining)   | 60   | 108 (reserved=108) | 184 |
| `pak_crc32`              | 4    | 244       | 244 |
| `footer_magic`           | 8    | 248       | 248 |

> **Note:** `v7::PakFooter` MUST remain exactly **256 bytes** for backward
> compatibility with the "read footer from end-of-file" strategy in
> `PakFile.cpp`.

```cpp
namespace oxygen::data::pak::v7 {

#pragma pack(push, 1)
struct PakFooter {
  uint64_t directory_offset = 0;     // 0   (8 bytes)
  uint64_t directory_size = 0;       // 8   (8 bytes)
  uint64_t asset_count = 0;         // 16  (8 bytes)

  // -- Resource data regions (v2+) --
  ResourceRegion texture_region = {};   // 24  (16 bytes)
  ResourceRegion buffer_region = {};    // 40  (16 bytes)
  ResourceRegion audio_region = {};     // 56  (16 bytes)
  ResourceRegion script_region = {};    // 72  (16 bytes)

  // -- Resource tables (v2+) --
  ResourceTable texture_table = {};      // 88  (16 bytes)
  ResourceTable buffer_table = {};       // 104 (16 bytes)
  ResourceTable audio_table = {};        // 120 (16 bytes)
  ResourceTable script_resource_table = {};  // 136 (16 bytes)
  ResourceTable script_slot_table = {};      // 152 (16 bytes)

  // -- Embedded Browse Index (Optional, v2+) --
  OffsetT browse_index_offset = 0;      // 168 (8 bytes)
  uint64_t browse_index_size = 0;       // 176 (8 bytes)

  // Reserved for future use
  uint8_t reserved[60] = {};                 // 184 (60 bytes)

  // -- CRC32 Integrity --
  uint32_t pak_crc32 = 0;                    // 244 (4 bytes)
  char footer_magic[8] = { 'O', 'X', 'P', 'A', 'K', 'E', 'N', 'D' }; // 248 (8 bytes)
};
#pragma pack(pop)
static_assert(sizeof(PakFooter) == 256);

} // namespace oxygen::data::pak::v7
```

> **CRITICAL:** The `script_slot_table` reuses the existing `ResourceTable`
> struct type (8+4+4 = 16 bytes). This is intentional: the format is identical
> (offset, count, entry_size). The v4 document already used this pattern for
> `texture_table`, `buffer_table`, and `audio_table`. Do NOT introduce a
> separate `ScriptSlotTable` struct.

### 2.4 Script Region Layout

The `script_region` is a contiguous byte range inside the PAK file. Its start
and size are given by `PakFooter::script_region` (a `ResourceRegion`:
`{ uint64_t offset, uint64_t size }`). The region holds **two kinds** of
blobs, appended sequentially by the cooker:

1. **Script data blobs** — raw bytecode or raw source text for script assets.
2. **Parameter arrays** — Fixed-size `ScriptParamRecord` arrays containing per-slot property overrides.

Nothing else goes in this region (future metadata blobs would be added in a
later format revision).

#### 2.4.1 Type Alias Quick Reference

Before looking at the diagram, here is which type alias is used for what:

| Type Alias | C++ Type | Width | Purpose |
| :--- | :--- | :--- | :--- |
| `OffsetT` | `uint64_t` | 8 bytes | **Absolute file offset** from byte 0 of the PAK. Used in `ScriptResourceDesc::data_offset` and `ScriptSlotRecord::params_array_offset`. |
| `DataBlobSizeT` | `uint32_t` | 4 bytes | **Byte count** of a single data blob. Used in `ScriptResourceDesc::size_bytes`. |
| `ResourceIndexT` | `uint32_t` | 4 bytes | **Index into a resource table** (NOT a byte offset). `ScriptAssetDesc::bytecode_resource_index` / `source_resource_index` are `ResourceIndexT` values. Given index `i`, the corresponding `ScriptResourceDesc` is at `script_resource_table.offset + i * script_resource_table.entry_size` in the PAK file. |

Key distinction:

- **`ResourceIndexT` → table entry index** ("give me the 3rd script resource
  descriptor"). You multiply by `entry_size` and add `table.offset` to get
  the file position of the descriptor struct.
- **`OffsetT` → absolute file position** ("seek to this byte in the PAK
  file"). Used to locate the actual binary data blob that the descriptor
  *talks about*.
- **`DataBlobSizeT` → blob length** ("read this many bytes starting at the
  offset"). Paired with an `OffsetT`.

#### 2.4.2 Region Byte-Level Diagram

The diagram below shows a PAK file with two script data blobs and two
parameter blobs. Addresses are hypothetical absolutes.

```text
┌─────────────────────────────────────────────────────────────────────┐
│  PAK FILE                                                          │
├─────────────────────────────────────────────────────────────────────┤
│  PakHeader (256 bytes)                                     0x0000  │
│  ...                                                               │
│  Asset Directory, Browse Index, other regions...                   │
│  ...                                                               │
├─────────── script_resource_table.offset ────────────────────────────┤
│                                                                    │
│  ScriptResourceDesc[0]  (32 bytes, RESERVED)               0x5000  │
│    [all zeros — sentinel for kNoResourceIndex, never read]         │
│                                                                    │
│  ScriptResourceDesc[1]  (32 bytes)                         0x5020  │
│    .data_offset    = 0x6000   (OffsetT: absolute in PAK)           │
│    .size_bytes     = 1200     (DataBlobSizeT: blob length)         │
│    .encoding       = kBytecode                                     │
│    ...                                                             │
│                                                                    │
│  ScriptResourceDesc[2]  (32 bytes)                         0x5040  │
│    .data_offset    = 0x64B4   (OffsetT)                            │
│    .size_bytes     = 830      (DataBlobSizeT)                      │
│    .encoding       = kBytecode                                     │
│    ...                                                             │
│                                                                    │
├─────────── script_slot_table.offset ───────────────────────────────┤
│                                                                    │
│  ScriptSlotRecord[0]  (128 bytes)                          0x5090  │
│    .script_asset_key    = {GUID A}                                 │
│    .params_array_offset = 0x67F0  (OffsetT: absolute in PAK)      │
│    .params_count        = 2                                         │
│    ...                                                             │
│                                                                    │
│  ScriptSlotRecord[1]  (128 bytes)                          0x5110  │
│    .script_asset_key    = {GUID A}                                 │
│    .params_array_offset = 0x681C  (OffsetT)                        │
│    .params_count        = 1                                         │
│    ...                                                             │
│                                                                    │
├─────────── script_region.offset (= 0x6000) ────────────────────────┤
│                                                                    │
│  Script Data Blob 0  (bytecode, 1200 bytes)                0x6000  │
│    [raw bytes — opaque bytecode payload]                           │
│    [pad to 4-byte alignment: 0 bytes (1200 is 4-aligned)]          │
│                                                            0x64B0  │
│  [4 bytes alignment padding]                               0x64B0  │
│                                                                    │
│  Script Data Blob 1  (source, 830 bytes)                   0x64B4  │
│    [raw bytes — UTF-8 source text]                                 │
│    [pad to 4-byte alignment: 2 bytes]                              │
│                                                            0x67F0  │
│                                                                    │
│  Param Blob 0  (Serio ScriptParamList, 42 bytes)           0x67F0  │
│    [Serio: count=2, {"aggressive",Bool,1}, {"patrol_radius",Float,15.0}]
│    [pad to 4-byte alignment: 2 bytes]                              │
│                                                            0x681C  │
│                                                                    │
│  Param Blob 1  (Serio ScriptParamList, 18 bytes)           0x681C  │
│    [Serio: count=1, {"rate",Float,5.0}]                            │
│    [pad to 4-byte alignment: 2 bytes]                              │
│                                                            0x6830  │
│                                                                    │
├─────────── script_region.offset + script_region.size ──────────────┤
│                                                                    │
│  ... PakFooter (256 bytes from end of file) ...                    │
└─────────────────────────────────────────────────────────────────────┘
```

> **Note:** `ScriptResourceDesc[0]` is always a zeroed-out 32-byte sentinel.
> `script_resource_table.count = 3` in this example (indices 0, 1, 2), but
> only indices 1 and 2 contain real data. This matches the `kNoResourceIndex`
> convention documented in `PakFormat.h`.

#### 2.4.3 How the Indirection Works (Step by Step)

**Reading a script asset's bytecode (via ResourceIndexT):**

1. The `ScriptAssetDesc` has `bytecode_resource_index = 1` — a `ResourceIndexT`
   (uint32_t index, **not** a byte offset). A value of `0` would mean
   "absent" (`kNoResourceIndex`), so the loader checks for `!= 0` first.
2. Look up the table entry:
   `seek_to(footer.script_resource_table.offset + 1 * sizeof(ScriptResourceDesc))`
3. Read a 32-byte `ScriptResourceDesc` struct from that position.
4. The descriptor says `data_offset = 0x6000` (an `OffsetT` — absolute file
   position) and `size_bytes = 1200` (a `DataBlobSizeT`).
5. `seek_to(0x6000)`, `read(1200 bytes)` → raw bytecode blob.

6. The `ScriptSlotRecord` at global index 0 has `params_array_offset = 0x67F0`
   (an `OffsetT` — absolute file position) and `params_count = 2`.
7. `seek_to(0x67F0)`, `read(2 * sizeof(ScriptParamRecord))` → raw binary records.
8. Cast the buffer to `const ScriptParamRecord*`, iterate and resolve parameters by type tag.

> Notice the difference: script **resources** (bytecode/source) are reached
> through a *ResourceIndexT → table → descriptor → OffsetT → data* chain.
> Script **parameters** skip the table indirection and use a direct
> *OffsetT → array* reference, because parameter records are already fixed-size
> and predictable.

#### 2.4.4 Cooker Append Protocol

The cooker builds the `script_region` incrementally, appending blobs in
first-come order:

1. **Initialize** a write cursor at `script_region.offset` (= current file
   end, before the footer is written).
2. **Emit the index-0 sentinel:** Append a zeroed-out 32-byte
   `ScriptResourceDesc` to the resource table (all fields zero). This
   reserves index 0 as `kNoResourceIndex`.
3. **For each unique script data blob** (bytecode or source):
   a. Record the current write cursor as `ScriptResourceDesc::data_offset`.
   b. Write the raw blob bytes.
   c. Record the byte count as `ScriptResourceDesc::size_bytes`.
   d. Pad the cursor forward to the next 4-byte boundary
      (`cursor = (cursor + 3) & ~3`).
   e. Append the `ScriptResourceDesc` to the resource table (at index ≥ 1).
4. **For each unique parameter blob:**
   a. Record the current write cursor as
      `ScriptSlotRecord::params_array_offset`.
   b. Write the Serio-serialized `ScriptParamList` bytes.
   c. Record the record count as `ScriptSlotRecord::params_count`.
   d. Pad the cursor forward to the next 4-byte boundary.
5. **Finalize** `script_region.size = cursor - script_region.offset`.
6. Write the `script_resource_table` entries (flat array of
   `ScriptResourceDesc`, starting with the zeroed sentinel at index 0) and
   the `script_slot_table` entries (flat array of `ScriptSlotRecord`). These
   tables may be placed outside the region itself (they sit in the
   directory/table area), or inside it — the footer fields describe their
   positions independently.

#### 2.4.5 Validation Contracts

Loaders MUST enforce the following invariants:

| # | Invariant | Error if Violated |
| :--- | :--- | :--- |
| V1 | `script_region.offset + script_region.size <= file_size` | Region extends past end of file. |
| V2 | For every `ScriptResourceDesc`: `data_offset >= script_region.offset` | Data offset before region start. |
| V3 | For every `ScriptResourceDesc`: `data_offset + size_bytes <= script_region.offset + script_region.size` | Data blob extends past region end. |
| V4 | For every `ScriptSlotRecord` with `params_count > 0`: `params_array_offset >= script_region.offset` | Param offset before region start. |
| V5 | For every `ScriptSlotRecord` with `params_count > 0`: `params_array_offset + params_count * 128 <= script_region.offset + script_region.size` | Param array extends past region end. |
| V6 | `script_resource_table.entry_size == sizeof(ScriptResourceDesc)` (32) | Entry size mismatch. |
| V7 | `script_slot_table.entry_size == sizeof(ScriptSlotRecord)` (128) | Entry size mismatch. |
| V8 | All `data_offset` and `params_array_offset` values are 4-byte aligned. | Alignment violation. |
| V9 | No Resource indices or Param ranges overlap. | Overlapping writes. |
| V10 | `ScriptResourceDesc` at index 0 MUST be all-zero sentinel. | Index-0 invariant violated. |

---

## 3. Script Resource Descriptor

Script bytecode/source lives in the `script_region` and is indexed by
`ResourceIndexT` via `script_resource_table`.

> **Index-0 invariant:** Scripts have **no fallback concept** — there is no
> "default bytecode" analogous to the pink fallback texture. Therefore,
> `ResourceIndexT(0)` for scripts means **absent / not-assigned**
> (matching `kNoResourceIndex`). Packers MUST populate valid script resource
> entries starting at index **1**. See §3.6 for the full contract.

### 3.1 Enumerations

```cpp
namespace oxygen::data::pak::v7 {

enum class ScriptLanguage : uint8_t {
  kLuau = 0,
};

enum class ScriptEncoding : uint8_t {
  kBytecode = 0,
  kSource = 1,
};

enum class ScriptCompression : uint8_t {
  kNone = 0,
  kZstd = 1,
};

} // namespace oxygen::data::pak::v7
```

### 3.2 ScriptResourceDesc (32 bytes)

```cpp
namespace oxygen::data::pak::v7 {

#pragma pack(push, 1)
struct ScriptResourceDesc {
  OffsetT data_offset = 0; // 0  (8 bytes) - Absolute to script_region
  DataBlobSizeT size_bytes = 0; // 8  (4 bytes) - Size of stored data
  ScriptLanguage language = ScriptLanguage::kLuau; // 12 (1 byte)
  ScriptEncoding encoding = ScriptEncoding::kBytecode; // 13 (1 byte)
  ScriptCompression compression = ScriptCompression::kNone; // 14 (1 byte)
  uint64_t content_hash = 0; // 15 (8 bytes) - first 8 bytes of SHA256(payload)
  uint8_t reserved[9] = {}; // 23 (9 bytes)
};
#pragma pack(pop)
static_assert(sizeof(ScriptResourceDesc) == 32);

} // namespace oxygen::data::pak::v7
```

### 3.3 Data Payload Format (On-Disk)

The data blob at `data_offset` contains raw bytes (NOT Serio-wrapped). This is
consistent with how texture and buffer data blobs work in the existing PAK
format: the descriptor provides the size, and the data is read as a raw byte
blob via `reader.ReadBlob(size_bytes)`.

> **Rationale:** Unlike parameters (which are structured KV data), script
> bytecode is an opaque byte array. Writing a Serio length prefix on top of
> `size_bytes` would be redundant. The existing `TextureResourceDesc` and
> `BufferResourceDesc` follow this same pattern — `data_offset` + `size_bytes`
> point to raw data.

**Loading Pseudocode:**

```cpp
// 1. Seek to script_resource_table, read ScriptResourceDesc at index N.
// 2. Seek to desc.data_offset.
// 3. Read desc.size_bytes raw bytes.
auto raw = reader.ReadBlob(desc.size_bytes);

// 4. If compressed, decompress according to runtime loader policy.
if (desc.compression == ScriptCompression::kZstd) {
  auto decompressed = ZstdDecompress(*raw);
  // use decompressed
} else {
  // use raw directly
}
```

### 3.4 Compression Contract

- `ScriptResourceDesc::compression` indicates payload compression.
- Current descriptor layout does not carry a dedicated uncompressed-size field.
- Runtime behavior follows the active loader implementation.

### 3.5 Payload Content Hash Contract

- `ScriptResourceDesc::content_hash` stores the first 8 bytes of SHA256 over
  the exact stored payload bytes (`size_bytes` at `data_offset`).
- A value of `0` means hash verification is disabled/absent for that entry.
- When non-zero, loaders may verify at load time and fail on mismatch.

### 3.6 Index-0 Contract for `script_resource_table`

The PAK format reserves `ResourceIndexT(0)` with category-specific semantics:

- **Textures** define a fallback (the pink "missing" texture). Index 0 MUST
  be populated with that fallback entry.
- **Buffers** also use index 0 as a fallback in some cases.
- **Scripts have no fallback.** There is no sensible "default script" that
  can substitute for a missing one.

Therefore, for the `script_resource_table`:

1. **`ResourceIndexT(0)` means "absent / not assigned"** — it semantically
   equals `kNoResourceIndex`.
2. **Packers MUST NOT** store a real script payload descriptor at index 0.
   Index 0 is a sentinel, not a real entry.
3. **Valid indices start at 1.** The first real script resource descriptor is
   at index 1 in the table.
4. **`script_resource_table.count`** includes the reserved index-0 slot. For
   example, if there are 3 real script resources, `count = 4` (indices
   0, 1, 2, 3 — where index 0 is zeroed-out padding).
5. **Loaders MUST** treat index 0 as "no resource" and skip reads. They
   MUST NOT seek or read from the data_offset of the index-0 entry.
6. **Cookers MUST** emit a zeroed-out 32-byte `ScriptResourceDesc` at
   index 0 (all fields zero). This ensures the table is contiguous and
   index arithmetic is straightforward.
7. **Runtime Resilience:** The implementation of runtime scripting classes
   (e.g., `ScriptResource`) MUST be resilient to zeroed descriptors.
   Constructors should allow zeroed values to avoid invariant failures during
   table iteration or diagnostic dumps.

> This matches the existing documentation in `PakFormat.h`:
>
> ```cpp
> constexpr ResourceIndexT kNoResourceIndex = 0;
> // "...for types that have no concept of fallback."
> ```

---

## 4. Script Asset Descriptor

`ScriptAssetDesc` stores asset-level metadata and references into
`script_resource_table`. No inline script payload is allowed in the descriptor.

### 4.1 ScriptAssetFlags

```cpp
namespace oxygen::data::pak::v7 {

enum class ScriptAssetFlags : uint32_t {
  kNone = 0,
  kAllowExternalSource = (1u << 0),
};

} // namespace oxygen::data::pak::v7
```

**Flag Rationale:**

| Flag | Purpose |
|:-----|:--------|
| `kNone` | No additional behavior flags. |
| `kAllowExternalSource` | Asset allows fallback to `external_source_path` when embedded payload is unavailable. |

### 4.2 ScriptAssetDesc (256 bytes)

```cpp
namespace oxygen::data::pak::v7 {

#pragma pack(push, 1)
struct ScriptAssetDesc {
  AssetHeader header;
  ResourceIndexT bytecode_resource_index = 0;
  ResourceIndexT source_resource_index = 0;
  ScriptAssetFlags flags = ScriptAssetFlags::kNone;
  char external_source_path[120] = {}; // null-terminated/padded
  uint8_t reserved[29] = {};
};
#pragma pack(pop)
static_assert(sizeof(ScriptAssetDesc) == 256);

} // namespace oxygen::data::pak::v7
```

**Runtime resolution:**

1. Prefer `bytecode_resource_index` when non-zero.
2. Otherwise use `source_resource_index` when non-zero.
3. If both are zero and `kAllowExternalSource` is set, runtime may use
   `external_source_path` as fallback.
4. For embedded resources, resolve index in `script_resource_table`, then read
   `ScriptResourceDesc` and payload bytes from `data_offset/size_bytes`.

---

## 5. Script Parameters Format

Script parameters (property overrides) are serialized as binary blobs stored
in the `script_region`.

### 5.1 Fixed-Size C Union Array

To guarantee 100% binary predictability across C++ and Python tools, parameters
are stored as a flat array of fixed-size C `union` records. This eliminates
variable-length serialization overhead and ensures bit-identical results.

### 5.2 ScriptParamRecord (128 bytes)

```cpp
#pragma pack(push, 1)
namespace oxygen::data::pak::v7 {

struct ScriptParamRecord {
  char key[64];           // Parameter name (null-terminated)
  uint32_t type;          // ScriptParamType enum
  union {
    bool as_bool;
    int32_t as_int32;
    float as_float;
    float as_vec[4];      // Used for Vec2, Vec3, Vec4
    char as_string[60];   // Maximum 59 chars + null terminator
  } value;
};
static_assert(sizeof(ScriptParamRecord) == 128);

} // namespace oxygen::data::pak::v7
#pragma pack(pop)
```

### 5.3 Runtime Transition (C Union → std::variant)

While the on-disk format uses raw unions for predictability, the runtime uses
`std::variant` for type safety. The loader translates between the two.

#### 5.3.1 ScriptParamValue (Runtime)

```cpp
namespace oxygen::data::pak::v7 {

using ScriptParamValue = std::variant<
  std::monostate,   // kNone
  bool,             // kBool
  int32_t,          // kInt32
  float,            // kFloat
  std::string,      // kString
  Vec2,             // kVec2
  Vec3,             // kVec3
  Vec4              // kVec4
>;

//! A single named parameter with a type-safe value.
struct ScriptParam {
  std::string key;            // Parameter name
  ScriptParamValue value;     // Type-tagged value (variant)

  //! Convenience: returns the ScriptParamType tag for this param.
  [[nodiscard]] auto Type() const noexcept -> ScriptParamType
  {
    return static_cast<ScriptParamType>(value.index());
  }
};

//! A list of named parameters for a script slot.
struct ScriptParamList {
  std::vector<ScriptParam> params;
};

} // namespace oxygen::data::pak::v7
```

#### 5.3.3 Helper Loaders (Math Types)

These Serio overloads ensure we can read/write the math types used in scripts.

```cpp
namespace oxygen::serio {

inline auto Load(AnyReader& r, data::pak::Vec2& v) -> Result<void> {
  CHECK_RESULT(r.ReadInto(v.x)); return r.ReadInto(v.y);
}
inline auto Store(AnyWriter& w, const data::pak::Vec2& v) -> Result<void> {
  CHECK_RESULT(w.Write(v.x)); return w.Write(v.y);
}

inline auto Load(AnyReader& r, data::pak::Vec3& v) -> Result<void> {
  CHECK_RESULT(r.ReadInto(v.x)); CHECK_RESULT(r.ReadInto(v.y));
  return r.ReadInto(v.z);
}
inline auto Store(AnyWriter& w, const data::pak::Vec3& v) -> Result<void> {
  CHECK_RESULT(w.Write(v.x)); CHECK_RESULT(w.Write(v.y));
  return w.Write(v.z);
}

inline auto Load(AnyReader& r, data::pak::Vec4& v) -> Result<void> {
  CHECK_RESULT(r.ReadInto(v.x)); CHECK_RESULT(r.ReadInto(v.y));
  CHECK_RESULT(r.ReadInto(v.z)); return r.ReadInto(v.w);
}
inline auto Store(AnyWriter& w, const data::pak::Vec4& v) -> Result<void> {
  CHECK_RESULT(w.Write(v.x)); CHECK_RESULT(w.Write(v.y));
  CHECK_RESULT(w.Write(v.z)); return w.Write(v.w);
}

} // namespace oxygen::serio
```

#### 5.3.4 Binary to Runtime Translation

The `ScriptLoader` uses the following logic to hydrate runtime `ScriptParam`
objects from binary `ScriptParamRecord`s:

```cpp
auto Hydrate(const data::pak::ScriptParamRecord& binary) -> ScriptParam {
  ScriptParam runtime;
  runtime.key = binary.key;

  auto type = static_cast<ScriptParamType>(binary.type);
  switch (type) {
    case ScriptParamType::kBool:  runtime.value = binary.value.as_bool; break;
    case ScriptParamType::kInt32: runtime.value = binary.value.as_int32; break;
    case ScriptParamType::kFloat: runtime.value = binary.value.as_float; break;
    case ScriptParamType::kString: runtime.value = std::string(binary.value.as_string); break;
    case ScriptParamType::kVec2:  runtime.value = Vec2{binary.value.as_vec[0], binary.value.as_vec[1]}; break;
    case ScriptParamType::kVec3:  runtime.value = Vec3{binary.value.as_vec[0], binary.value.as_vec[1], binary.value.as_vec[2]}; break;
    case ScriptParamType::kVec4:  runtime.value = Vec4{binary.value.as_vec[0], binary.value.as_vec[1], binary.value.as_vec[2], binary.value.as_vec[3]}; break;
    default: runtime.value = std::monostate{}; break;
  }
  return runtime;
}
```

**Usage Examples:**

```cpp
using namespace oxygen::data::pak;

// Construction:
ScriptParam p1 { .key = "speed",  .value = 42.0f };          // kFloat
ScriptParam p2 { .key = "name",   .value = std::string{"A"} }; // kString
ScriptParam p3 { .key = "active", .value = true };            // kBool
ScriptParam p4 { .key = "offset", .value = Vec3{1, 2, 3} };   // kVec3

// Access (compile-time type-safe):
float speed = std::get<float>(p1.value);       // OK
// int bad = std::get<int32_t>(p1.value);       // throws std::bad_variant_access!

// Visitor pattern (exhaustive — compiler warns on missing cases):
std::visit(overloaded {
  [](std::monostate) { /* kNone */ },
  [](bool b)         { /* kBool */ },
  [](int32_t i)      { /* kInt32 */ },
  [](float f)        { /* kFloat */ },
  [](const std::string& s) { /* kString */ },
  [](Vec2 v)         { /* kVec2 */ },
  [](Vec3 v)         { /* kVec3 */ },
  [](Vec4 v)         { /* kVec4 */ },
}, p1.value);
```

#### 5.3.2 Variant Index ↔ ScriptParamType Mapping

The variant alternative index **intentionally matches** the `ScriptParamType`
enum ordinal. This is enforced by the `Type()` convenience method and by the
Serio overloads below. The mapping is:

| `ScriptParamType` | Enum Value | `std::variant` Alternative |
| :--- | :--- | :--- |
| `kNone` | 0 | `std::monostate` (index 0) |
| `kBool` | 1 | `bool` (index 1) |
| `kInt32` | 2 | `int32_t` (index 2) |
| `kFloat` | 3 | `float` (index 3) |
| `kString` | 4 | `std::string` (index 4) |
| `kVec2` | 5 | `Vec2` (index 5) |
| `kVec3` | 6 | `Vec3` (index 6) |
| `kVec4` | 7 | `Vec4` (index 7) |

#### 5.3.3 Binary Layout on Disk (C-Union Array)

```text
ScriptParamArray:
  [align 128] For each entry (params_count):
    Offset: 0    [64 bytes] char key[64]      -- Name (NUL-terminated/padded)
    Offset: 64   [ 4 bytes] uint32_t type     -- ScriptParamType tag
    Offset: 68   [60 bytes] union value:      -- Parameter value
                 - bool as_bool       (1 byte)
                 - int32_t as_int32   (4 bytes)
                 - float as_float     (4 bytes)
                 - float as_vec[4]    (16 bytes)
                 - char as_string[60] (60 bytes)
```

> **Alignment Note:** Each `ScriptParamRecord` is strictly **128 bytes**. The
> loader reads them as a contiguous array of fixed-size records.

### 5.4 Cooker Packing Rules

To ensure bit-identical results, the Python cooker (`packers.py`) MUST:

1. **Pad parameter keys** with null bytes up to 64 bytes.
2. **Order parameters alphabetically** by key within the array to ensure
    stability.
3. **Strictly use 128-byte alignment** for each `ScriptParamRecord` in the
    binary stream.
4. **Use little-endian** encoding for all numeric types.

### 5.5 Parameter Array Location

Parameter arrays are concatenated in the `script_region`.
`ScriptSlotRecord::params_array_offset` is the **absolute file offset** to the
start of the contiguous `ScriptParamRecord[params_count]` array.

A `params_count` of `0` means "no parameters" (e.g., a script with no
overrides). In that case, `params_array_offset` SHOULD be `0` but loaders MUST
NOT attempt to read from it.

---

## 6. Scene Scripting Component Model

### 6.1 Component Type Registration

**File:** `src/Oxygen/Data/ComponentType.h`

```cpp
enum class ComponentType : uint32_t {
  // ... existing entries ...
  kScripting = 0x50524353, //!< 'SCRP' - Scripting component
};
```

> **FourCC verification:** `'S'=0x53, 'C'=0x43, 'R'=0x52, 'P'=0x50` →
> Little-endian 32-bit: `0x50524353`.

### 6.2 Asset Type Registration

**File:** `src/Oxygen/Data/AssetType.h`

```cpp
enum class AssetType : uint8_t {
  // ... existing entries ...
  kScript       = 4,
  kMaxAssetType = kScript
};
```

### 6.3 Flag Enumerations

```cpp
namespace oxygen::data::pak::v7 {

enum class ScriptingComponentFlags : uint32_t {
  kNone          = 0,
  kEnabled       = (1u << 0),
  kPauseWithGame = (1u << 1),
};

enum class ScriptSlotFlags : uint32_t {
  kNone        = 0,
  kEnabled     = (1u << 0),
  kRunOnLoad   = (1u << 1),
  kRunInEditor = (1u << 2),
  kCatchErrors = (1u << 3),
};

} // namespace oxygen::data::pak::v7
```

**Flag Rationale:**

| Flag | Scope | Purpose |
|:-----|:------|:--------|
| `ScriptingComponentFlags::kEnabled` | Component | Fast skip of all slot dispatch on a node without mutating slot records. |
| `ScriptingComponentFlags::kPauseWithGame` | Component | Node-level pause semantics — avoids per-slot checks. |
| `ScriptSlotFlags::kEnabled` | Slot | Per-script enable/disable when a node hosts multiple scripts. |
| `ScriptSlotFlags::kRunOnLoad` | Slot | Deterministic bootstrap hook dispatch during scene hydration. |
| `ScriptSlotFlags::kRunInEditor` | Slot | Slot-level gating to avoid running gameplay scripts in editor. |
| `ScriptSlotFlags::kCatchErrors` | Slot | Per-script error isolation (protected-call behavior). |

### 6.4 ScriptingComponentRecord (16 bytes)

```cpp
namespace oxygen::data::pak::v7 {

#pragma pack(push, 1)
struct ScriptingComponentRecord {
  SceneNodeIndexT node_index = 0;    // 0 (4 bytes)
  ScriptingComponentFlags flags
    = ScriptingComponentFlags::kNone; // 4 (4 bytes)

  // Reference into the GLOBAL ScriptSlotTable (in PakFooter)
  uint32_t slot_start_index = 0;     // 8  (4 bytes)
  uint32_t slot_count = 0;           // 12 (4 bytes)
};
#pragma pack(pop)
static_assert(sizeof(ScriptingComponentRecord) == 16);

} // namespace oxygen::data::pak::v7
```

> This follows the same pattern as `RenderableRecord`, `PerspectiveCameraRecord`,
> etc. The `node_index` field links to a `NodeRecord` in the scene's node table.
> Component tables are sorted by `node_index` (existing convention).

### 6.5 ScriptSlotRecord (128 bytes)

```cpp
namespace oxygen::data::pak::v7 {

#pragma pack(push, 1)
struct ScriptSlotRecord {
  AssetKey script_asset_key = {};    // 0  (16 bytes) - References a ScriptAssetDesc

  OffsetT params_array_offset = 0; // 16 (8 bytes) - Absolute offset in PAK; 0 = no params
  uint32_t params_count = 0;       // 24 (4 bytes) - Number of ScriptParamRecords
  int32_t execution_order = 0;      // 28 (4 bytes) - Lower = earlier. Slots with same order: stable.

  ScriptSlotFlags flags
    = ScriptSlotFlags::kNone;        // 32 (4 bytes)
  uint8_t reserved[92] = {};        // 36 (92 bytes) - Padded to 128 bytes
};
#pragma pack(pop)
static_assert(sizeof(ScriptSlotRecord) == 128);

} // namespace oxygen::data::pak::v7
```

### 6.6 Scene Asset Descriptor (v5)

The `SceneAssetDesc` retains its 256-byte layout from v4. No structural changes
are needed — scripting components are just another entry in the component table
directory (like `kRenderable`, `kPerspectiveCamera`, etc.).

```cpp
namespace oxygen::data::pak::v7 {

//! Scene asset descriptor version for PAK v7.
//! Same as v4 (no scene descriptor structural changes for scripting).
constexpr uint8_t kSceneAssetVersion = v7::kSceneAssetVersion;

} // namespace oxygen::data::pak::v7
```

### 6.7 Namespace Alias

At the bottom of `PakFormat.h`, update the default namespace alias:

```cpp
namespace oxygen::data::pak {
//! Default namespace alias for latest version of the PAK format
using namespace v7;
} // namespace oxygen::data::pak
```

---

---

## 8. Loader and Validation Rules

### 8.1 PAK-Level Validation (PakFile.cpp)

When opening a v5 PAK file, `PakFile` must:

1. Read the v5 `PakFooter` (256 bytes from end of file).
2. For `script_resource_table`:
   - If `count > 0`, validate `entry_size == sizeof(ScriptResourceDesc)`.
   - Validate that the table range is within file bounds.
3. For `script_slot_table`:
   - If `count > 0`, validate `entry_size == sizeof(ScriptSlotRecord)`.
   - Validate that the table range is within file bounds.
4. For `script_region`:
   - If `size > 0`, validate that `[offset, offset+size)` is within file bounds.
5. For each `ScriptSlotRecord`:
   - `script_asset_key` must be non-zero (a valid GUID).
   - If `params_count > 0`: `params_array_offset + params_count * 128` must be
     within `script_region` bounds.

### 8.2 Scene-Level Validation (SceneLoader.h)

When loading a scene with a `kScripting` component table:

1. **Entry Size:** `entry.table.entry_size == sizeof(ScriptingComponentRecord)`.
2. **Node Index:** Each `node_index` must be `< node_count` (existing pattern).
3. **Sort Order:** Table must be sorted by `node_index` (existing pattern).
4. **Slot Range:** For each record:
   `slot_start_index + slot_count <= PakFooter::script_slot_table.count`.
   (This requires the loader to have access to the global slot count.)
5. **No Overlap:** Slot ranges from different component records SHOULD NOT
   overlap. (Warning, not hard error — the cooker must prevent this.)

**Extension to `ValidateComponentTable`:**

```cpp
// In SceneLoader.h detail namespace:
template <>
inline auto ValidateComponentTable<data::pak::ScriptingComponentRecord>(
  const std::span<const std::byte> table_bytes, const uint32_t count,
  const uint32_t entry_size, const uint32_t node_count) -> void
{
  using Record = data::pak::ScriptingComponentRecord;
  if (count == 0) return;
  if (entry_size != sizeof(Record)) {
    throw std::runtime_error("scene asset scripting component record size mismatch");
  }
  if (table_bytes.size() < static_cast<size_t>(count) * sizeof(Record)) {
    throw std::runtime_error("scene asset scripting component table out of bounds");
  }

  data::pak::SceneNodeIndexT prev = 0;
  bool have_prev = false;
  for (uint32_t i = 0; i < count; ++i) {
    Record record {};
    std::memcpy(&record,
      table_bytes.subspan(static_cast<size_t>(i) * sizeof(Record), sizeof(Record)).data(),
      sizeof(Record));

    if (record.node_index >= node_count) {
      throw std::runtime_error("scene asset scripting component node_index out of range");
    }
    if (have_prev && record.node_index < prev) {
      throw std::runtime_error("scene asset scripting component table must be sorted by node_index");
    }
    prev = record.node_index;
    have_prev = true;
  }
}
```

### 8.3 Dependency Publication

During scene decode, the loader must:

1. Iterate the `kScripting` component table.
2. For each `ScriptingComponentRecord`, read the referenced
   `ScriptSlotRecord` range from the global table.
3. Collect unique `script_asset_key` values.
4. Publish each unique key via `context.dependency_collector->AddAssetDependency(key)`.

This follows the exact same pattern used for geometry dependencies from
`RenderableRecord::geometry_key` (see `SceneLoader.h:396-408`).

---

## 9. SceneAsset Extensions

**File:** `src/Oxygen/Data/SceneAsset.h`

### 9.1 ComponentTraits Specialization

```cpp
namespace oxygen::data {

template <>
struct ComponentTraits<pak::ScriptingComponentRecord> {
  static constexpr ComponentType kType = ComponentType::kScripting;
};

} // namespace oxygen::data
```

### 9.2 SceneAsset Validation (ParseAndValidate)

In `SceneAsset::ParseAndValidate()`, add entry-size validation for
`kScripting` (following the existing pattern for `kRenderable`, `kPerspectiveCamera`, etc.):

```cpp
if (type == ComponentType::kScripting
  && entry.table.entry_size != sizeof(pak::ScriptingComponentRecord)) {
  throw std::runtime_error("SceneAsset scripting component record size mismatch");
}
```

---

## 10. PakFile Extensions

**File:** `src/Oxygen/Content/PakFile.h` and `PakFile.cpp`

### 10.1 Script Resource Table Access

Add a `ScriptsTable()` accessor and `CreateScriptDataReader()` method, following
the exact pattern of `BuffersTable()` and `TexturesTable()`.

```cpp
// In PakFile.h:
using ScriptsTableT = /* ResourceTable for ScriptResourceDesc */;

//! Get the script resource table.
OXGN_CNTT_NDAPI auto ScriptsTable() const -> ScriptsTableT&;

//! Create a Reader for the script data region.
OXGN_CNTT_NDAPI auto CreateScriptDataReader() const -> /* Reader type */;
```

### 10.2 Script Slot Table Access

```cpp
//! Read a ScriptSlotRecord at the given global index.
OXGN_CNTT_NDAPI auto ReadScriptSlotRecord(uint32_t index) const
  -> data::pak::ScriptSlotRecord;

//! Read `count` ScriptSlotRecords starting at `start_index`.
OXGN_CNTT_NDAPI auto ReadScriptSlotRecords(uint32_t start_index, uint32_t count) const
  -> std::vector<data::pak::ScriptSlotRecord>;

//! Number of entries in the global script slot table.
OXGN_CNTT_NDAPI auto ScriptSlotCount() const -> uint32_t;
```

### 10.3 Footer Reading

In `PakFile::ReadFooter()`, v5 footers must be read with the new fields. Because
the footer is always 256 bytes and v5 simply uses former-reserved space, a
`memcpy` of the v5 struct works directly. For v4-compatible reading, the script
fields will be zero-initialized (safe — count=0 means "no scripts").

---

## 11. Runtime Hydration Contract

### 11.1 PAK Startup

1. Read the `PakFooter`.
2. If `script_slot_table.count > 0`:
   - Validate the global slot table.
   - Optionally memory-map the script region for demand-loading.
3. For each script resource loaded:
   - Call `content_cache_.Touch(hash_key)` to ensure a baseline `refcount = 1`. This pins the script in cache mirror texture handling.

### 11.2 Scene Loading

When loading a scene:

1. Find the `kScripting` component table in the scene's component directory.
2. For each `ScriptingComponentRecord`:
   a. Attach a `ScriptingComponent` runtime object to the node at `node_index`.
   b. Access the global slots array at `[slot_start_index, +slot_count)`.
   c. For each `ScriptSlotRecord` in that range:
      - Resolve the `script_asset_key` to a loaded `ScriptAsset`.
      - If `params_count > 0`:
        Seek to `params_array_offset` in the PAK (absolute), read
        `params_count * 128` bytes, translate each `ScriptParamRecord`
        into a runtime `ScriptParam`.
      - Instantiate a runtime `ScriptInstance` with the parameters.
   d. If `ScriptSlotFlags::kRunOnLoad` is set, queue the slot for immediate
      execution.

### 11.3 Memory Layout at Runtime

```text
Global Script Slot Table (contiguous array, cache-friendly):
┌──────────────────────────────────────────────────┐
│ SlotRecord[0]  SlotRecord[1]  ...  SlotRecord[N] │  (128 bytes each)
└──────────────────────────────────────────────────┘

Scene A (10,000 trees using WindSway with default parameters):
  ScriptingComponentRecord[0]:  slot_start=0, slot_count=1
  ScriptingComponentRecord[1]:  slot_start=0, slot_count=1
  ...
  (All 10,000 trees reference slot 0 — the SAME slot.)

Global table entry 0:
  script_asset_key = {WindSway GUID}
  params_array_offset = 0x5000
  params_count = 0                (no overrides — use defaults)
```

---

## 12. Incremental Cooking & Packing Strategy

### 12.1 Per-Scene Cook (Incremental)

When cooking a single scene, the process is isolated:

1. The cooker processes `ScriptingComponent`s in the editor scene.
2. For each script instance, it generates a **Hash Signature**:
   `hash(script_asset_key || params_array_bytes)`.
3. It emits `ScriptSlotRequest` intermediates:

   ```cpp
   struct ScriptSlotRequest {
     AssetKey script_asset_key;
     std::vector<ScriptParamRecord> params_array;
     uint64_t signature;                  // hash(asset_key + params_array)
     ScriptSlotFlags flags;
     int32_t execution_order;
   };
   ```

4. The `ScriptingComponentRecord` in the loose-cooked scene uses
   **temporary/relative** indices pointing to these local requests. These
   are NOT final global indices.

### 12.2 Global Packing (Linker)

The Packer assembles the final PAK from cooked assets:

1. **Collect:** Gathers all `ScriptSlotRequest`s from all scenes being packed.
2. **Deduplicate:** Inserts signatures into a global hash map:
   - **Hit:** Reuse existing Global Slot Index.
   - **Miss:**
     1. Allocate new `ScriptSlotRecord`.
     2. Write param blob to `script_region` (4-byte aligned).
     3. Register `signature → new_global_index`.
3. **Patch:** Updates the `ScriptingComponentRecord`s in each scene's payload:
   - Replace temporary `slot_start_index` with the final global index.
   - Update `slot_count` (should already be correct).
4. **Finalize:** Writes the `ScriptSlotTable` entries (packed array of
   `ScriptSlotRecord`) and updates the `PakFooter`.

### 12.3 Deduplication Guarantees

- Two slots are considered identical if and only if their signatures match
  (same `script_asset_key` AND byte-identical `params_array`).
- `params_hash` in `ScriptSlotRecord` is stored for runtime diagnostics only —
  the cooker uses the full array hash for deduplication.

---

## 13. Loose-Cooked Index Extensions

**File:** `src/Oxygen/Data/LooseCookedIndexFormat.h`

Add a new `FileKind` for script resources:

```cpp
enum class FileKind : uint16_t {
  // ... existing entries ...
  kScriptsTable = 5,
  kScriptsData  = 6,
};
```

---

## 14. Change Scope (Implementation Checklist)

### 14.1 Core Data / Schema

| File | Changes |
|:-----|:--------|
| `src/Oxygen/Data/PakFormat.h` | Add `v5` namespace. `PakHeader` (version=5), `PakFooter` (script fields), `ScriptLanguage`, `ScriptEncoding`, `ScriptCompression`, `ScriptResourceDesc`, `ScriptAssetFlags`, `ScriptAssetDesc`, `ScriptParamType`, `ScriptParam`, `ScriptParamList`, `ScriptingComponentFlags`, `ScriptingComponentRecord`, `ScriptSlotFlags`, `ScriptSlotRecord`. Update default namespace alias to `v5`. |
| `src/Oxygen/Data/ComponentType.h` | Add `kScripting = 0x50524353`. |
| `src/Oxygen/Data/ComponentType.cpp` | Add `to_string` case for `kScripting`. |
| `src/Oxygen/Data/AssetType.h` | Add `kScript = 4`, update `kMaxAssetType`. |
| `src/Oxygen/Data/AssetType.cpp` | Add `to_string` case for `kScript`. |
| `src/Oxygen/Data/PakFormatSerioLoaders.h` | Add `Load` overloads for `ScriptingComponentRecord`, `ScriptSlotRecord`, `ScriptParamType`, `ScriptParam`, `ScriptParamList`. Add `Store` overloads for `ScriptParamType`, `ScriptParam`, `ScriptParamList`. |
| `src/Oxygen/Data/SceneAsset.h` | Add `ComponentTraits<ScriptingComponentRecord>`. |
| `src/Oxygen/Data/SceneAsset.cpp` | Add entry-size check for `kScripting` in `ParseAndValidate()`. |
| `src/Oxygen/Data/LooseCookedIndexFormat.h` | Add `kScriptsTable`, `kScriptsData` to `FileKind`. |

### 14.2 Runtime Loading

| File | Changes |
|:-----|:--------|
| `src/Oxygen/Content/PakFile.h` | Add script table/region accessors, slot reader methods. |
| `src/Oxygen/Content/PakFile.cpp` | Handle v5 footer, init script table, implement slot readers. |
| `src/Oxygen/Content/Loaders/SceneLoader.h` | Add `kScripting` validation branch. Collect `script_asset_key` dependencies. |

### 14.3 Tooling

| File | Changes |
|:-----|:--------|
| `src/Oxygen/Content/Tools/PakDump/AssetDumpers.h` | Add `ScriptAssetDumper` registration. |
| `src/Oxygen/Content/Tools/PakDump/ScriptAssetDumper.h` | New: Dump `ScriptAssetDesc` fields. |
| `src/Oxygen/Content/Tools/PakDump/PakFileDumper.h/cpp` | Dump script region/table summary. Ensure index 0 is labeled `(sentinel/reserved)` and "User Resource Count" is reported correctly as `Total - 1`. |
| `src/Oxygen/Content/Tools/Inspector/main.cpp` | Update `scripts` command to label index 0 with `*` and provide a legend. |

### 14.4 Tests

| File | Changes |
|:-----|:--------|
| `src/Oxygen/Data/Test/PakFormat_test.cpp` | `static_assert` tests for all new struct sizes. Round-trip Serio tests for `ScriptingComponentRecord`, `ScriptSlotRecord`, `ScriptParam`, `ScriptParamList`. |
| `src/Oxygen/Content/Tools/PakGen/tests/` | Add **Binary Regression Tests** ensuring the emitted PAK contains a strictly all-zero descriptor at index 0 for scripting resource tables. |
| `src/Oxygen/Content/Test/SceneLoader_test.cpp` | Test loading a scene with `kScripting` component. Test validation rejection for bad slot ranges. |

---

## 15. Versioning Summary

| Element | v4 (Current) | v5 (This Spec) |
|:--------|:-------------|:---------------|
| `PakHeader::version` | 4 | 5 |
| `PakFooter` reserved | 108 bytes | 60 bytes (48 consumed) |
| `PakFooter::script_region` | — | New `ResourceRegion` |
| `PakFooter::script_resource_table` | — | New `ResourceTable` |
| `PakFooter::script_slot_table` | — | New `ResourceTable` |
| `SceneAssetDesc` | 256 bytes (unchanged) | 256 bytes (unchanged) |
| `kSceneAssetVersion` | 2 | 2 (unchanged) |
| `ComponentType::kScripting` | — | `0x50524353` |
| `AssetType::kScript` | — | `4` |
| Default namespace | `v4` | `v5` |
