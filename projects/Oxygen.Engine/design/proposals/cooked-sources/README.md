# Cooked sources

Status: implemented

Last updated: 2026-10-31

The Cooker reads cooked content to build paks, to resolve references while it
imports, and to inspect and validate what was cooked. This design defines what
it reads, a **cooked source**, in the engine's own terms, and the one file
system port through which the Cooker reads Serio devices and publishes
atomically, in memory for tests.

It is the Cooker side of the [Serio](../../../src/Oxygen/Serio/README.md) consumers
(`PakPlanBuilder`, `PakWriter`, `PakManifestWriter`, the Cooker file helpers
and ports). The [cooked content builders](../cooked-content-builders/README.md)
build cooked sources for tests.

How a loose root is published while it is read, and what it requires of
other mounts, is designed in [loose root publication](root-publication.md).

## Contents

- [Scenarios](#scenarios)
- [Vocabulary](#vocabulary)
- [Design](#design)
  - [Cooked content](#cooked-content)
  - [Summary](#summary)
  - [Loose layout](#loose-layout)
  - [Cooker file system](#cooker-file-system)
  - [Mounts in formats and diagnostics](#mounts-in-formats-and-diagnostics)
  - [Test builders](#test-builders)
- [Consumers](#consumers)
- [Decisions](#decisions)

## Scenarios

- A game team cooks its project into `/Game`, and the engine's content into
  `/Engine`; a studio shares a props pack as `/Props`. Each is cooked content
  under one mount, read the same way whether it is the game or something the
  game uses.
- During development the content is a loose cooked root the editor and the
  import service rewrite; for shipping, PakTool seals one or more of them into
  a pak, or patches an earlier pak.
- An import resolves a sidecar's scene, a model's materials or a texture by
  virtual path, in whichever cooked source of the project's mount table holds
  it.
- The Inspector lists, validates and dumps a cooked source, loose or pak.
- A test builds cooked sources and paks in memory, packs them, and reads the
  result back, without touching the disk.

## Vocabulary

| Term               | Meaning                                                                                                   | Engine type                         |
| ------------------ | --------------------------------------------------------------------------------------------------------- | ----------------------------------- |
| Mount              | The first segment of every virtual path of some content (`Game`, `Engine`, `Props`)                       | `content::MountRootOf`              |
| Cooked source      | Where cooked content of one mount is stored: a loose cooked root (a folder and its index) or a pak (file) | `data::CookedSource { kind, path }` |
| Cooked content     | The read view of one cooked source: its assets, resources, named textures and summary                     | `CookedContent`                     |
| Summary            | What a cooked source provides, deletes and requires, read without loading its assets                      | `CookedSummary`                     |
| Asset              | A typed descriptor at a virtual path, with the references it binds                                        | `CookedAsset`                       |
| Resource table     | The rows of one resource kind and their payloads                                                          | `CookedResourceTable`               |
| Named texture      | A virtual path bound to a texture row (a texture descriptor)                                              | `NamedTexture`                      |
| Sidecar            | A record naming a resource row from outside the tables (a buffer's `.obuf`)                               | `CookedSidecar`                     |
| Cooker file system | Where the Cooker reads cooked sources and publishes paks, manifests and catalogs                          | `CookerStorage`                     |

A mount is neither a game nor a library: the content under `/Game` is the
game's, and the same machinery serves `/Engine` and `/Props`. Nothing in the
API, the formats or the diagnostics calls cooked content a "library".

## Design

### Cooked content

```cpp
//! The read view of one cooked source: a loose cooked root or a pak.
class CookedContent final {
public:
  //! Opens `source` through `storage`; throws `data::CookedFormatError`.
  static auto Open(const CookerStorageReader& storage,
    const data::CookedSource& source) -> CookedContent;

  auto Source() const -> const data::CookedSource&;
  auto Summary() const -> const CookedSummary&;
  auto Assets() const -> std::span<const CookedAsset>;
  auto FindAsset(const data::AssetKey& key) const -> const CookedAsset*;
  auto Descriptor(const data::AssetKey& key) const -> std::vector<std::byte>;
  auto DescriptorStored(const data::AssetKey& key) const -> StoredBytes;
  auto ResourceTable(TableKind kind) const -> const CookedResourceTable&;
  auto NamedTextures() const -> std::span<const NamedTexture>;
  auto Sidecars() const -> std::span<const CookedSidecar>;
};
```

- It is opened once and immutable; there is no empty state and no reload.
- `CookedAsset` holds the key, a typed `data::AssetType`, the virtual path,
  the references, the descriptor's size and SHA-256 (a pak's catalog records
  the digest), and the virtual path of the source that produced it (empty in
  a pak). It holds no storage path.
- `CookedResourceTable` holds the rows of one table (textures, buffers,
  scripts, physics, and audio, which only paks hold) and reads a row and its
  payload. `StoredBytes` (a file and a byte range) says where a descriptor, a
  table or a payload is stored, for the pak planner, which copies them
  verbatim; it is the only storage a reader sees.
- A pak stays open while its content lives. A loose root's files are opened
  for each read and closed after it, so the import service rewrites them
  while a job holds the content of the root it writes.
- A loose root and a pak answer every method; nothing is meaningful for one
  kind only. Checks that a loose root's index agrees with its files (a named
  texture against its table row) run at open, so every reader gets them.
- `Validate(content, check)` works on either kind; `VerifyStorage(check)`
  checks the stored bytes against what the source records of them (a loose
  root's sizes and digests, a pak's CRC).
- The writer side of a loose root (the import session's index, PakTool's
  staged copy, the inventory report's per-file listing) keeps the format's
  own reader, `LooseCookedIndex`, for storage paths.

### Summary

`CookedSummary` is what a mount table and a pak build need before loading any
asset: the source's kind and identity, its one mount, the assets it provides
(after a pak's deletions) with their types, the texture keys it provides, the
assets it deletes, what it requires of other mounts, and the virtual paths of
external script sources. `CookedContent::Summary()` returns it;
`ReadCookedSummary(storage, source)` reads it alone, without opening the
assets, for mount tables over many sources.

### Loose layout

Where a loose root's files lie is an internal detail of the loose writer
(`Import/Internal/LooseCookedLayout.h`): fixed names for the resource tables,
their data files and the folders that hold them. Only the index's own name,
`data::loose_cooked::kIndexFileName`, is part of the format; readers follow the
paths the index records, and those paths obey one rule,
`data::loose_cooked::RequireIndexRelPath`, that writers and readers share. A
writer stores every file but a data file under its content name
(`data::loose_cooked::ContentRelPath`), as
[loose root publication](root-publication.md) requires; a texture descriptor or
buffer sidecar a reference names keeps that identity as its index record's
virtual path.

### Cooker file system

```cpp
class CookerStorageReader {
public:
  virtual auto KindOf(const std::filesystem::path& path) const -> PathKind = 0;
  virtual auto Open(const std::filesystem::path& path) const
    -> Result<serio::AnyDevice> = 0;
};

class CookerStorage : public virtual CookerStorageReader {
public:
  virtual auto Publish(const std::filesystem::path& path)
    -> Result<std::unique_ptr<Publication>> = 0;
  virtual auto Remove(const std::filesystem::path& path) -> Result<void> = 0;
  virtual auto RemoveEmptyFolder(const std::filesystem::path& path)
    -> Result<bool> = 0;
  virtual auto Move(const std::filesystem::path& from,
    const std::filesystem::path& to) -> Result<void> = 0;
  virtual auto SizeOf(const std::filesystem::path& path) const
    -> std::optional<serio::ByteCount> = 0;
};
```

- `RealCookerStorage()` maps files to read and publishes through
  `serio::AtomicFileWriter`; the test `MemoryFileSystem` holds every file in
  memory, so what one build publishes is the next build's source.
- A `Publication` is a patchable output stream that replaces its destination
  only when committed.
- Every Cooker read and write of cooked content goes through it: cooked
  sources, the pak planner and writer, manifests, catalogs, the loose writer
  of the import service, and PakTool's artifacts and reports.
- `Move` replaces a file's destination in one step on one volume, so a reader
  finds the old file or the new one, never neither. It fails, changing
  nothing, when its source names no file or its destination names a folder.
- PakTool publishes a pak, its catalog, manifest and report together or not
  at all. Each is published to a staged sibling, then moved over its
  destination; a destination it replaces is moved to a backup sibling,
  restored when a later artifact fails, and removed once all are in place.
  A backup whose destination is gone is never removed: it is the only copy.
  Every cleanup or restore step that fails is reported with the publication's
  result.
- PakTool seals a loose source's external scripts into a staged root that
  holds only what sealing changes: the sealed script descriptors, the script
  tables and the index. The build reads its sources through a staged storage
  (`StagedSourceStorage`) that shows, inside a staged root, every other file
  of the original root; nothing is copied, and unchanged files keep the
  digests the original index records.
- The mount table's file system is a separate read-only port: it needs paths'
  kinds, their canonical spelling and a cooked source's summary, never
  publication. `IMountFileSystem` derives from `CookerStorageReader` only.

### Mounts in formats and diagnostics

- The pak catalog names its cooked source's mount in its `mount` field, in the
  encoded catalog and the JSON schema (catalog version 5).
- Planner and mount table diagnostics speak of mounts: `pak.plan.root_multiple`
  says a pak holds one mount; the mount table's codes are `mount.cooked_*`.
- `MountKind::kCooked` documents a cooked source.

### Test builders

The [cooked content builders](../cooked-content-builders/README.md)
(`Oxygen.Cooker.Testing`) build cooked sources for tests:

- `LooseCookedRoot("Game")` builds a loose cooked root of one mount, encoding
  its index with the product's encoder; `CookedPak` builds a pak of one or
  more of them through the product's planner and writer.
- Pipelines run on `CookerStorage`, so pak pipeline tests run in memory
  through `MemoryFileSystem`; only `AssetLoader` mounting and the executables
  write files out, with `Materialize`.

## Consumers

| Consumer                                                                                                    | Reads through                                                             |
| ----------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------- |
| Pak planner (`PakPlanBuilder`)                                                                              | `CookedContent`: resource tables and named textures                       |
| Import: sidecar scene resolution, `MountedCookedSources`, scene and collision shape jobs, sidecar pipelines | `CookedContent::Open` over the mount's sources; `FindAsset`, `Descriptor` |
| `ModelImportRecord`, `TextureReferenceResolver`                                                             | `CookedContent`; named textures by virtual path                           |
| Import session (the session's index)                                                                        | The loose writer's own index                                              |
| Mount table (`SourceMounts`)                                                                                | `ReadCookedSummary` through its read-only port                            |
| Inspector commands, PakTool `ScriptSealing`, PakDump                                                        | `CookedContent`; resource tables                                          |
| Validation                                                                                                  | `Validate(content, check)`                                                |

## Decisions

- **A cooked source is not a library.** The mount names content; whose it is,
  a game's or a shared pack's, is not the reader's business.
- **One view for both kinds.** A loose root and a pak have the same assets,
  tables and named textures; only their storage differs, and storage stays
  inside `CookedContent`.
- **Opened, not loaded.** A cooked source is opened once into an immutable
  view; there is no two-phase initialization.
- **Ports by need.** Building and inspecting read and publish through
  `CookerStorage`; a mount table only reads, so its port cannot publish.
- **Publishing several files together is the tool's.** The port publishes
  one file atomically and moves one file in one step; staging, backups and
  rollback across a pak and its sidecars are PakTool's, built on those two.
- **Format renames are format changes.** The catalog field was renamed with a
  version bump of its own, never silently and never waiting for another
  format change.
- **The module's namespace stays.** The types live in
  `oxygen::content::cooked`, beside the Cooker's other `oxygen::content`
  namespaces; renaming the Cooker's namespaces is a change of the whole
  module, not of this area.
- **The engine owns this vocabulary.** The editor's `CookedLibrary*` names
  move to cooked sources and mounts in the editor's own work; nothing here
  depends on them.
