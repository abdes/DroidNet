# Runtime Formats (Loose Cooked + PAK-Compatible)

This document defines the **binary runtime formats** used by the editor/runtime-compatible **loose cooked** layout and by the **PAK** container.

The goal is that the editor can run the engine directly against cooked artifacts ("works just like that"), and a later packaging step can concatenate/merge those artifacts into `.pak` with **minimal translation**.

## Scope

This spec covers the canonical binary encoding for:

- Resource files:
  - `textures.table` + `textures.data`
  - `buffers.table` + `buffers.data`
- First-class asset descriptor files:
  - `.omat` (materials)
  - `.ogeo` (geometry)

It does **not** define authoring/import sidecar JSON; those are tooling concerns and are separate from runtime formats.

## References (authoritative)

- `Oxygen.Engine/src/Oxygen/Data/PakFormat.h` (packed structs and invariants)
- `Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h` (index file schema)
- `Oxygen.Engine/src/Oxygen/Content/Docs/chunking.md` (PAK layout + alignment goals)

## Global binary conventions

Unless a specific format states otherwise:

- **Endianness**: little-endian.
- **Struct packing**: on-disk structs are packed (`#pragma pack(push, 1)`).
- **Offsets**: offsets stored in structs are **absolute** from the start of the file that contains the referenced data.
- **Alignment**: data blobs are padded with zero bytes to satisfy alignment constraints.
- **Validation**: runtimes should fail fast on malformed data (bounds checks, size checks, alignment checks).

## Loose cooked container layout (high-level)

A loose cooked container is described by `container.index.bin` and a set of files referenced by it.

`container.index.bin` contains:

- Asset entries, mapping `AssetKey` → descriptor file relpath + size/hash (+ optional virtual path)
- File records, identifying the canonical resource files by kind:
  - `kBuffersTable`, `kBuffersData`
  - `kTexturesTable`, `kTexturesData`

See `Oxygen.Engine/src/Oxygen/Data/LooseCookedIndexFormat.h` for the definitive index schema.

### Key rule: descriptor files are PAK-compatible

Descriptor files (`.omat`, `.ogeo`, …) must match the in-PAK binary descriptor encoding. This enables:

- Runtime: the same loader logic for PAK and loose cooked.
- Packaging: the packer can copy descriptor bytes as-is into the `.pak` directory region.

## Texture resources (`textures.table` + `textures.data`)

### `textures.table`

A binary file containing a tightly packed array of `oxygen::data::pak::TextureResourceDesc` entries.

- Entry size: 40 bytes (`sizeof(TextureResourceDesc)`).
- Indexing: resource index $i$ refers to table entry at index $i$.
- Reserved index:
  - Entry 0 is reserved (`kFallbackResourceIndex` / `kNoResourceIndex`) and should be all zeros.

### `textures.data`

A binary file containing concatenated texture payload blobs.

For each `TextureResourceDesc` entry $i>0$:

- `data_offset` points into **this file** (`textures.data`).
- `size_bytes` is the payload size.
- `alignment` must be 256 (and `data_offset` must be a multiple of 256).

#### Texture payload encoding (MVP)

The payload is **GPU-ready** data in the declared `format` and `compression_type`.

- No JSON headers.
- No per-mip directory.
- The runtime computes subresource sizes/offsets from:
  - `width`, `height`, `depth`, `array_layers`, `mip_levels`, `format`, `compression_type`.

##### Canonical byte ordering

The payload is serialized in this order:

1. Array layer major: for `layer = 0 .. array_layers-1`
2. Mip major: for `mip = 0 .. mip_levels-1`
3. For each subresource (layer,mip): raw bytes for that mip image

Notes:

- For `TextureType::kTextureCube` and `kTextureCubeArray`, `array_layers` counts cube *faces* (so for a single cube, `array_layers == 6`). Face order is:
  - +X, -X, +Y, -Y, +Z, -Z
- For 3D textures, each mip level serializes the full mip volume for the layer.

This ordering is intentionally simple and matches common graphics API expectations.

## Buffer resources (`buffers.table` + `buffers.data`)

### `buffers.table`

A binary file containing a tightly packed array of `oxygen::data::pak::BufferResourceDesc` entries.

- Entry size: 32 bytes (`sizeof(BufferResourceDesc)`).
- Indexing: resource index $i$ refers to table entry at index $i$.
- Reserved index:
  - Entry 0 is reserved (`kFallbackResourceIndex` / `kNoResourceIndex`) and should be all zeros.

### `buffers.data`

A binary file containing concatenated buffer payload blobs.

For each `BufferResourceDesc` entry $i>0$:

- `data_offset` points into **this file** (`buffers.data`).
- `size_bytes` is the payload size.
- Alignment rule:
  - If `element_format != 0` (typed buffer), the payload must be aligned for the implied element size.
  - If `element_format == 0` (raw/structured), the payload must be aligned to `element_stride`.
  - `element_stride == 0` is invalid.

### Common buffer conventions (MVP)

- **Vertex buffers**: structured (`element_format == 0`) with `element_stride == 72` bytes.
  - This corresponds to the engine’s current `oxygen::data::Vertex` layout assumptions in tooling/tests.
- **Index buffers**: structured (`element_format == 0`) with `element_stride == 4` bytes (uint32 indices).

## Material descriptors (`.omat`)

A `.omat` file is a single binary descriptor file referenced by `container.index.bin`.

Binary encoding is identical to the in-PAK encoding:

- `oxygen::data::pak::MaterialAssetDesc` (256 bytes, packed)
- Followed by `ShaderReferenceDesc[]`:
  - Entry count = popcount(`shader_stages`)
  - Order = ascending set-bit order (least-significant set bit first)

Texture references:

- `base_color_texture`, `normal_texture`, etc are `ResourceIndexT` values indexing into `textures.table`.

## Geometry descriptors (`.ogeo`)

A `.ogeo` file is a single binary descriptor file referenced by `container.index.bin`.

Binary encoding is identical to the in-PAK encoding:

- `oxygen::data::pak::GeometryAssetDesc` (256 bytes, packed)
- Followed by `MeshDesc meshes[lod_count]`

For each `MeshDesc`:

- Immediately followed by an optional mesh-type-specific blob:
  - For procedural meshes: a parameter blob of size `ProceduralMeshInfo::params_size`.
- Then `SubMeshDesc submeshes[submesh_count]`.
- Each `SubMeshDesc` is followed immediately by `MeshViewDesc mesh_views[submesh.mesh_view_count]`.

Resource references:

- Standard meshes reference:
  - `vertex_buffer` (`ResourceIndexT`, indexes `buffers.table`)
  - `index_buffer` (`ResourceIndexT`, indexes `buffers.table`)
- Submeshes reference their material by `AssetKey` (`material_asset_key`).

## Versioning and compatibility

- Per-asset versioning uses `AssetHeader.version` inside each asset descriptor.
- The container-level compatibility gate is `IndexHeader.content_version` in `container.index.bin`.
- Any incompatible change to the binary layout of descriptors or resource tables must bump the relevant version and update loaders accordingly.

## Integrity and reproducibility

- `container.index.bin` already provides per-descriptor size and SHA-256 metadata.
- Resource files may also be hashed via file records.
- Tooling should emit deterministic file ordering and padding so that identical inputs produce identical cooked outputs.
