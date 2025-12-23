# LooseCooked Index (V1)

This folder contains the .NET implementation of the runtime-compatible **LooseCookedIndex v1** format used by loose cooked roots via `container.index.bin`.

Scope:

- Read/write the binary index format.
- Provide small, immutable model types representing the decoded index.

Non-goals:

- Cooking/building assets.
- Resolving assets/resources at runtime.
- UI/editor integration.

## File map

### LooseCookedIndex.cs

Defines `LooseCookedIndex`, the codec for the on-disk index:

- `Read(Stream)` parses `container.index.bin` into a `Document`.
- `Write(Stream, Document)` writes a `Document` back to the binary format.
- `ComputeSha256(ReadOnlySpan<byte>)` convenience helper for tests/build pipelines.

Implementation notes:

- Uses fixed-size binary layout constants (`HeaderSize`, `AssetEntrySize`, `FileRecordSize`, `Sha256Size`).
- Reads/writes little-endian fields via `BinaryPrimitives`.
- Encodes strings through a null-terminated UTF-8 string table.
- Writes a deterministic string table (ordinal ordering) and reserves offset `0` as an empty-string sentinel.
- Validates section ranges against the stream length.

Internal helpers (private):

- Header parsing/validation (`HeaderFields`, `ReadHeaderFields`, `ValidateHeaderFields`).
- Section IO for assets/files (`ReadAssets`, `ReadFileRecords`, `WriteAssetsSection`, `WriteFileRecordsSection`).

### Document.cs

Defines `Document`, the in-memory representation of an index file:

- `ContentVersion`: content schema version (runtime-facing, not the index file version).
- `Flags`: feature flags (`IndexFeatures`).
- `Assets`: ordered list of `AssetEntry`.
- `Files`: ordered list of `FileRecord`.

### AssetEntry.cs

Defines `AssetEntry`, one entry per cooked asset descriptor:

- `AssetKey`: stable 128-bit identifier (`AssetKey`).
- `DescriptorRelativePath`: path into the cooked root for the asset descriptor.
- `VirtualPath`: optional editor/runtime virtual path (only present when `IndexFeatures.HasVirtualPaths`).
- `AssetType`: small numeric type tag (matches runtime expectations).
- `DescriptorSize`: size in bytes of the descriptor file.
- `DescriptorSha256`: SHA-256 hash of the descriptor file contents.

### AssetKey.cs

Defines `AssetKey`, a 128-bit identifier stored as two little-endian `ulong` parts.

Key points:

- Uses `[StructLayout(LayoutKind.Sequential)]` to keep layout explicit.
- Does **not** use `Guid` to avoid byte-order ambiguity.
- Helpers:
  - `FromBytes(ReadOnlySpan<byte>)` reads exactly 16 bytes.
  - `WriteBytes(Span<byte>)` writes exactly 16 bytes.

### FileRecord.cs

Defines `FileRecord`, an entry for an auxiliary data file referenced by the index:

- `Kind`: file purpose/category (`FileKind`).
- `RelativePath`: path into the cooked root.
- `Size`: size in bytes.
- `Sha256`: SHA-256 hash of the file contents.

### FileKind.cs

Defines `FileKind`, a small enum describing the auxiliary file category.

These are intended to line up with the runtime’s v1 `FileKind` values (e.g. buffers/textures table/data).

### IndexFeatures.cs

Defines `IndexFeatures`, bit flags describing optional sections/fields:

- `HasVirtualPaths`: asset entries contain valid virtual paths.
- `HasFileRecords`: file-record section is present.

## Relationships

- `LooseCookedIndex` reads/writes a `Document`.
- `Document` aggregates `AssetEntry` and `FileRecord`.
- `AssetEntry` uses `AssetKey`.
- `FileRecord` uses `FileKind`.
- `Document.Flags` uses `IndexFeatures`.

## Compatibility

This code targets the runtime’s LooseCookedIndex v1 binary layout (`container.index.bin`). If the runtime introduces a v2 format, it should be implemented as a sibling namespace (e.g. `...LooseCooked.V2`) with its own folder and types.
