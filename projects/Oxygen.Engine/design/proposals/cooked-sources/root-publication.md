# Loose root publication

Status: accepted; implemented by review set R6 slice S2

Last updated: 2026-11-03

A loose cooked root is read while it is written: a preview mounts a library
that ImportTool is recooking, and a second cook can start on a root the first
one has not finished. This design makes the index the only commit of a loose
root, so a reader sees one whole commit, a writer has the root to itself, a
crash leaves the last commit readable, and the root records what it requires
of other mounts. It answers review set R6 of the
[engine review sets](../../content-pipeline/engine-review-sets.md#r6-cooked-roots-write-read-verify)
and extends [cooked sources](README.md).

Read: [scenarios](#scenarios), [today](#today), [the protocol](#the-protocol),
[writer](#writer), [readers](#readers), [recovery](#recovery),
[checks](#checks), [requirements](#requirements),
[editor authoring](#editor-authoring),
[index version 9](#index-version-9), [impact](#impact),
[decisions](#decisions).

## Scenarios

**A library recooked under a running preview.** A studio keeps `/Props` as a
loose cooked root that every project mounts by absolute path. A technical
artist recooks it with ImportTool while three editors have it mounted and a
DemoShell runs on it. Each reader keeps rendering the commit it mounted, with
every descriptor, table row and payload of that commit, until it mounts again.
None reads a descriptor of the new commit through the index of the old one.

**Two cooks of one root.** A build agent and a developer both start ImportTool
on `/Props`. The second is refused at once with `import.cooked_root_busy`,
naming the root, before it writes a byte. The editor never meets it: each of
its cooks writes a fresh generation, one writer at a time.

**A cook killed halfway.** The machine loses power while a cook of `/Props`
has written half of its descriptors and appended payloads. After the restart,
`/Props` mounts at its last commit and validates clean of defects. The next
cook removes what the killed one left.

**A pack that needs the engine.** `/Props` materials sample
`/Engine/Textures/Checker`. Its index records that it requires that texture of
`Engine`. A DemoShell that mounts `/Props` without `/Engine` mounts it and
reports the missing requirement; the materials that sample the checker show it
missing, and the rest of `/Props` renders. An editor that has not added a
library the game uses still previews the game, with the mount's finding
`mount.cooked_dependency_missing`. A pak built from `/Props` carries the same
requirement, and a pak whose requirement is missing is refused, as today.

**A long session.** An editor stays open all day on a library that a cook
rewrites twenty times. Files the old commits listed stay on disk while a
reader of those commits lives, and the next cook after that reader moves on
reclaims them.

## Today

| Fact                                                                                                                                                     | Where                                                                                     |
| -------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| Descriptors, sidecars and tables are published atomically, but under fixed names, before the index: a reader of the old index opens the new file.        | `LooseCookedWriter::WriteBinaryFile`, `CookerStorage::PublishAsync`, emitters             |
| Files the new index drops are deleted right after it is published, under readers of the old one.                                                         | `LooseCookedWriter::Finish`, `DeleteRemovedFiles_`                                        |
| `.data` files are written in place at offsets taken from the file size at session start; two processes reserve the same range.                           | `ResourceTableAggregator` constructor, `ReserveDataRange`                                 |
| The only cross-process lock, `container.index.lock`, spans `Finish`; it blocks, and its failure is an untyped `std::system_error`.                       | `LooseCookedWriter::Finish`, `RealFiles::LockExclusive`                                   |
| A crashed session leaves temporary siblings, unindexed descriptors and a data tail past the indexed size; metadata validation then refuses the root.     | `serio::AtomicFileWriter`, `CheckInventory` (`kUnexpected`, `kSizeMismatch`)              |
| The runtime maps the index and tables, holds data files open, and opens a descriptor at each load.                                                       | `LooseCookedSource`                                                                       |
| The Cooker opens every file of a loose root at each read.                                                                                                | `CookedContent`, `CookerStorageReader::Open`                                              |
| Generations are immutable roots under a `.generation.lock` lease; readers hold it shared, reclamation takes it exclusive.                                | `ModelImportRecord`, `AssetLoader::MountLooseCookedGeneration`, editor `CookedGeneration` |
| Descriptors, tables and index are published with `kBuffered` durability: after a power loss the index can name files whose bytes never reached the disk. | `CookerStorage::PublishAsync`                                                             |
| A loose root's summary requires nothing of other mounts, so mount and generation swaps check no loose requirement.                                       | `CookedContent` `LooseSummary`, `ContentSourceRegistry::ValidateMount`                    |

Generations already give the editor's own cooks whole-commit reads: an editor
cook writes a fresh generation and selects it. The defects are in plain roots,
which libraries, ImportTool outputs, examples and DemoShell use, and in the
writer of any root, a generation included.

## The protocol

A loose root is a sequence of commits. Commit `N` is the index whose header
records `commit = N`; nothing else commits anything.

- **Content-named files.** Every file of a root but its data files and its
  index has its content in its name. A commit never changes a file it did not
  write: it adds files under new names and stops listing others.
- **Append-only data files.** A data file keeps its fixed name. Its committed
  length is the size the index records; bytes past it belong to no commit.
- **One writer.** A writer holds the root's writer lease from the start of its
  session to the end of its last cleanup.
- **Readers lease a commit.** A reader of a plain root holds a shared lease on
  the commit it read. A generation keeps its own lease instead: it is never
  written after it is selected.
- **The writer reclaims.** A file a commit stops listing is retired. The
  index lists it with the commit that retired it, and a writer deletes it
  once no reader holds a commit that lists it.

### Reserved names

| Name                    | What                                              |
| ----------------------- | ------------------------------------------------- |
| `container.index.bin`   | The index: the commit                             |
| `container.writer.lock` | The writer lease; replaces `container.index.lock` |
| `.leases/<commit>.lock` | Reader leases of one commit, in decimal           |
| `.generation.lock`      | The lease of a generation, unchanged              |

`data::loose_cooked::RequireIndexRelPath` refuses all of them, and the
`.leases` folder, as inventory paths.

### Content names

A content-named file is `<stem>.<digest>.<extension>`: the stem and extension
the layout gives it today, and the first 16 lowercase hexadecimal digits of the
SHA-256 of its bytes. `Materials/Red.omat` becomes
`Materials/Red.3f9a07c1d2b4e6a8.omat`; the buffer table becomes
`Resources/buffers.5c0e91aa7d03b2f4.table`. A writer treats a file of that
name and size as those bytes and does not write it again: two contents share
a name only when their digests share 64 bits. The index records the full
digest. The grammar is owned by `data::loose_cooked::ContentRelPath`;
readers follow the paths the index records and never parse a name.

A file a reference names by path, a texture descriptor or a buffer sidecar,
keeps that identity as the virtual path its index record carries, which the
index allows for auxiliary files too; the job that resolves the reference
looks it up by that virtual path, never by file name.

## Writer

A writer session of a root, from the first `ImportSession` of an in-process
cohort to the end of its cohort:

1. **Lease.** `LooseCookedIndexRegistry::BeginSession` takes
   `container.writer.lock` with `FileLock::TryAcquire` exclusive, when it
   creates the root's writer. A held lease fails the session with
   `import.cooked_root_busy` (the root and the lease file), a typed error
   distinct from every format failure, before anything is written. Later
   sessions of the cohort share the lease. A killed holder releases it with
   its process.
2. **Read commit `N`.** The writer reads the current index once. No other
   writer can commit before it ends, so `Finish` merges with this commit and
   never re-reads.
3. **Recover** (see [recovery](#recovery)), then **reclaim**.
4. **Write.** Each descriptor, sidecar and table is published to its content
   name. A name the commit already lists has those bytes; nothing is written.
   Payloads are written to each data file from its committed length on, at the
   aligned offsets the aggregator reserves. Tables are read from the paths
   commit `N` records, not from fixed names.
5. **Flush.** Every file the new commit lists that this session wrote, data
   files included, is flushed before the index is published.
6. **Commit.** The writer publishes index `N + 1` with `kStable` durability:
   its assets and files, the data files' new committed lengths, its retired
   files and its requirements.
7. **Sweep.** Files this session wrote that the commit does not list (a job
   that failed after writing) are deleted.
8. **Reclaim**, then release the lease.

An aborted cohort skips steps 5 and 6, and the root stays at commit `N`;
the next writer deletes what it wrote (see [recovery](#recovery)).

The in-process cohort stays as it is: sessions of one process share the
writer, and the last one out commits. `container.index.lock` is gone; the
writer lease serializes writers for the whole session, `Finish` included.

### Reclaiming

A retired file records the commit `R` that stopped listing it; commits before
`R` list it. Reclaiming:

1. Lists `.leases`. For each `<c>.lock`, `TryAcquire` exclusive: busy means a
   reader holds commit `c`; otherwise the file is a leftover, removed.
2. `L` is the oldest busy commit. A retired file with `R <= L`, or every
   retired file when no lease is busy, is deleted.
3. The next commit drops the entries of deleted files. Until then the index
   lists them as retired, and a retired file may be absent.

Reclaiming runs at session start and after each commit, under the writer
lease, so a root reclaims at its next cook after its readers move on. A
retired file that cannot be deleted (a managed reader opened it without
delete sharing) stays retired and is retried by the next session.

## Readers

A reader of a plain root:

1. Maps the index and reads `c` from its header.
2. Takes `.leases/<c>.lock` shared, with `TryAcquire` and
   `kTemporary`, so the last holder removes the file. Busy means a writer is
   checking that lease at this instant: go back to 1.
3. Reads the header of the current index again. If it records `c`, the lease
   holds: no file `c` lists can be reclaimed while it is held, because a
   reclamation needs a newer commit, and every later one sees the lease. If it
   records a newer commit, the reader releases the lease and goes back to 1.

A writer commits a few times a minute at most, so the loop ends on its first
or second turn; after eight turns the open fails busy
(`std::errc::device_or_resource_busy`), retryable and distinct from a format
failure.

- **Runtime.** `LooseCookedSource` holds the lease for its life, as it holds a
  generation's. A remount (`AddLooseCookedRoot` on a mounted root,
  `PrepareMountsAsync`) leases the new commit before it releases the old one.
  A data file's region is its committed length, never the whole file.
- **Cooker.** `CookedContent::Open` of a loose root leases through the storage
  port, `CookerStorageReader::TryLock(.leases/<commit>.lock, kShared)`, and
  holds the lease for the content's life. `MemoryFileSystem` implements leases
  in memory, so reclamation tests run without the disk.
- **Generations.** A root with `.generation.lock` takes the generation lease
  (unchanged) and no commit lease: a generation is written before it is
  selected and never after.
- **Read-only roots.** A reader that cannot create a lease (access denied)
  reads the root unleased and logs it once: no writer that shares its
  permissions can write it either.

## Recovery

A writer that starts after a crash finds, besides commit `N`:

| Leftover                                                              | Action at session start                      |
| --------------------------------------------------------------------- | -------------------------------------------- |
| Bytes of a data file past its committed length                        | Truncated to the committed length            |
| A content-named file no entry of commit `N` lists, current or retired | Deleted                                      |
| A temporary sibling `.oxygen-<uuid>.tmp`                              | Deleted                                      |
| A reader lease file nobody holds                                      | Deleted by reclaiming                        |
| Any other unlisted file                                               | Kept, and refused by checks as `kUnexpected` |

The writer deletes only names its own grammar produces, never a file someone
else put in the root. A crash between the index and the sweep leaves files the
new index retires or does not list; both cases are in the table. With every
file flushed before the index, a power loss after the index leaves no listed
file torn.

## Checks

`LooseCookedIndex::CheckContent` learns the protocol's states, so a root a
writer is cooking, or one a crash left, is not damaged:

| On disk                                                          | Issue                                      | Mount  | `VerifyStorage`, Inspector |
| ---------------------------------------------------------------- | ------------------------------------------ | ------ | -------------------------- |
| A data file longer than its committed length                     | `kUncommittedTail`                         | Accept | Refuse                     |
| A retired file, present or absent                                | None                                       | Accept | Accept                     |
| An unlisted file of the writer's grammar, or a temporary sibling | `kUncommitted`                             | Accept | Refuse                     |
| Reserved names, `.leases`                                        | None                                       | Accept | Accept                     |
| Any other unlisted file                                          | `kUnexpected`                              | Refuse | Refuse                     |
| A listed file shorter, longer or of other bytes                  | `kSizeMismatch`, digest mismatch, as today | Refuse | Refuse                     |

`VerifyStorage` refuses what a clean session never leaves, so the editor's
validation of a sealed candidate, and PakTool's of its sources, still fail on
a crashed writer's leftovers. `CookedContent` maps the two new issues to
`CookedDefect::kInconsistent`.

## Requirements

A loose root records, for each asset, the assets and textures of other mounts
it references, as a pak does.

- **Recorded at cook.** A job that resolves a reference in another mount
  (`MountedCookedSources::LookupReference`) registers it with the writer for
  the asset it cooks: the mount, the reference kind, the key and the type. A
  reference in the root's own mount is no requirement.
- **Kept per asset.** A rewritten asset replaces its requirements; a removed
  asset drops them; an untouched asset keeps those commit `N` recorded.
- **Read as a summary.** `CookedSummary::requirements` of a loose root is the
  union, grouped by mount as `data::PakCatalogDependency`, the type a pak
  uses. The doc line "A loose root states none" goes.
- **Reported, not refused, for a loose root.** A loose root is content being
  authored, and an editor previews it on machines that lack a library.
  `ContentSourceRegistry::ValidateMount` resolves a loose source's
  requirements as it resolves a pak's, over the layers below, and a mount, a
  generation swap or a retirement that leaves one unmet goes ahead: the
  source's unmet requirements are its `FailedRequirement` list, logged once as
  a warning and queried with `AssetLoader::UnmetRequirements(source)`. An asset
  that loads a missing reference fails that load, as any missing reference
  does. A pak's unmet requirement still refuses with
  `DependencyRequirementError`: a pak is shipped content, and its requirements
  were checked when it was built.
- **Read by the import checks.** `SourceMounts::CheckCookedSource` checks the
  summary's requirements already, with `mount.cooked_dependency_missing`; a
  loose root's now reach it.
- **One rule, two resolutions.** The key references an asset's descriptor
  binds are the only source of a requirement. A cook resolves them against
  the cooked roots its job mounts and records the loose root's requirements;
  `PakPlanBuilder` resolves the same references against the dependency paks
  it builds with and records the pak's. A pak's requirements are those of its
  dependencies, which may differ from what the loose root was cooked against.

## Editor authoring

The editor's cooks write fresh generations, one writer at a time, and its
libraries are read-only to it; the protocol keeps that and adds:

| Editor flow                                         | What it gets                                                                                                                                                                                                       |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Live Preview of a plain library root                | Its commit, whole, while ImportTool or CI recooks the library; the publication's index digest check republishes it as today                                                                                        |
| A cook killed while writing its candidate           | The candidate keeps its last commit instead of failing metadata validation; the operation journal recovers as today                                                                                                |
| A selected generation after a power loss            | No listed file torn: every listed file is flushed before the index                                                                                                                                                 |
| Seeding a candidate from the previous generation    | `CookRootImage` copies the folder: the writer lease is `kTemporary`, so no lock file is left, and a generation's retired files are deleted at the end of its session, because no reader leases a generation commit |
| Incremental cooks                                   | Unchanged: outputs are matched by the descriptor paths the published index records, which a content name changes only when the bytes change                                                                        |
| Add library, mount table library facts              | A loose library's dependencies, read from its index as a pak's are from its catalog                                                                                                                                |
| A project opened without a library its content uses | The preview mounts the rest and shows the mount finding                                                                                                                                                            |

The cost is the flush before each commit; the implementation measures it for
a material edit and a model import against the preview's refresh.

## Index version 9

One version bump, `kLooseCookedIndexVersion` 8 to 9, for the protocol and the
requirements. Readers accept version 9 only; maintained content is recooked.

| Change             | Content                                                                                   |
| ------------------ | ----------------------------------------------------------------------------------------- |
| Header             | `uint64 commit`; offset, count and record size of the retired and the requirement records |
| Retired record     | Path (string offset), kind, the commit that retired it                                    |
| Requirement record | Asset entry index, mount (string offset), `KeyReferenceKind`, key, `AssetType`            |
| Data file record   | Its size is the committed length (meaning made explicit; layout unchanged)                |
| Auxiliary record   | May carry the virtual path a reference names it by, as a buffer sidecar does              |
| `FileKind`         | `kAssetDescriptor`, which only a retired record names                                     |
| Reserved paths     | `container.writer.lock`, `.leases`; `container.index.lock` retired                        |

The new fields come out of `_reserved`, which keeps the 256-byte header. The
cooked outputs change names, so `ImporterRevision` is bumped with it.

## Impact

| Area                                                | Change                                                                                                       |
| --------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Data/LooseCookedIndexFormat.h`, `LooseCookedPaths` | Version 9 records, reserved names                                                                            |
| `Data` index encoder                                | Encodes commit, retired and requirement records                                                              |
| `Content` index reader and validation               | Decodes them; checks of [checks](#checks); exempt names                                                      |
| `Content/LooseCookedSource`, `AssetLoader`          | Commit lease, committed data regions, requirement validation                                                 |
| `Cooker/Import` writer, registries, aggregators     | Writer lease, content names, committed lengths, flush, sweep, recovery, reclaim, requirements                |
| `Cooker/Cooked` storage port, `CookedContent`       | `TryLock` (leases), `Truncate`, stable `PublishAsync`; commit lease; loose summary requirements; new defects |
| PakTool `ScriptSealing`, `StagedSourceStorage`      | The staged root's writer lease; a staged writer never cuts its original's data files                         |
| `Serio`                                             | None: `FileLock`, `AtomicFileWriter` and `kStable` cover it                                                  |
| Managed index readers and inventories               | `Oxygen.Managed.Assets` `LooseCookedIndex`, `CookedIndexSnapshot`, `CookedInventoryReport`: version 9, names |
| Editor                                              | Adopted by the [portable references plan](../portable-references/plan.md): S7, S8, S10, S14                  |
| Docs                                                | `Content/Docs/loose_cooked_content.md` (readers lease), cooked sources README, editor content pipeline LLD   |

Tests, beside each owner:

- Writer: a second writer is refused busy before writing; a crash at each
  step (fault injection through the storage port) leaves commit `N`
  readable and is recovered by the next session; a retired file outlives a
  held lease and is reclaimed after it; equal bytes are not rewritten.
- Readers: a reader racing a commit retries and holds a whole commit; a
  remount releases the old lease after leasing the new one.
- Two processes: ImportTool against a lease another process holds; a reader
  process mounted across twenty commits reads every asset of its commit.
- Requirements: recorded per asset, replaced on recook, dropped on removal;
  a loose root without the required mount mounts and lists them unmet; a pak
  of the root carries them and is refused without it.
- Editor-facing: a writer session leaves no lock file; a generation's retired
  files are gone when its session ends; the flush cost is measured.

## Decisions

- **The index is the commit.** Content-named files make every commit a single
  rename. Rejected: a generation per cook for every root, which copies a
  library at each cook; and a journal of renames, which has no single commit
  point.
- **Names carry content, not commits.** A file unchanged across commits keeps
  its name, so a cook rewrites only what changed and a reader shares files
  across commits.
- **Data files stay appended.** Payloads are large and shared across commits;
  a committed length gives them the same guarantee as a content name.
- **A writer lease for the session, refused when busy.** Writers never
  interleave appends or tables, and a busy root is reported, never waited on
  inside a tool; whoever queues decides how long to wait.
- **Per-commit reader leases.** A preview open all day pins only the files of
  its own commit; a single readers' lock would pin every retired file of the
  root until the last reader left.
- **The writer reclaims.** Readers never delete; deletion needs the writer
  lease, so it never races a commit.
- **Leftovers are the next writer's.** A crash costs disk space until the
  next cook, never a refused mount.
- **Requirements come from references.** A loose root records the
  resolution its cook made, against the roots the cook mounted; a pak
  resolves the same references against its dependency paks. Rejected:
  deriving a pak's requirements from its loose sources' records, which would
  hold requirements on roots the pak does not depend on.
- **Loose roots report, paks refuse.** Refusing a loose root would blank an
  editor's whole preview when one library is missing, because a mount set is
  replaced whole; a shipped pak's requirements were checked at build.
