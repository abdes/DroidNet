# Serio

Oxygen's one IO and serialization library: devices, regions, awaitable IO,
buffered streams, hashing, binary serialization and atomic publication. The
API design is [serio-io/api.md](../../../design/proposals/serio-io/api.md).

## At a glance

| Need                                  | Use                                                                   |
| ------------------------------------- | --------------------------------------------------------------------- |
| Read or write a file, blocking        | `NativeFile::Open(path, spec)`, `ReadExactlyAt`, `WriteAt`            |
| Parse a file in place                 | `MappedFile::Open(path)`, a `ByteReader` over its bytes               |
| Await reads and writes of a file      | `driver.Open(path, spec)`, an `AsyncFile`                             |
| Await without an event loop           | `IoDriver::CreateInline()`: the awaiting thread blocks                |
| Bytes in memory as a device           | `MemoryFile`, `ByteView`, both awaitable by themselves                |
| Read a whole device into a buffer     | `ReadAll(device, limit)`, `co_await AsyncReadAll(device, limit)`      |
| Own payload bytes                     | `ByteBuffer`: `Allocate`, `CopyOf`, `Unsigned()`                      |
| Read or write values of a region      | `RegionReader<D>`, `RegionWriter<D>` (an `AnyReader`/`AnyWriter`)     |
| Hash a device or region               | `Hash<H...>`, `ComputeSha256`, `ComputeCrc32`, `AsyncHash`            |
| Publish a whole file atomically       | `WriteFileAtomically`, `AsyncWriteFileAtomically`, `AtomicFileWriter` |
| Move a file over another              | `MoveFileOver(from, to)`                                              |
| Read a file another process publishes | `OpenPublishedFile`, `ReadPublishedFile`                              |
| Lock across threads and processes     | `FileLock::Acquire` (waits), `FileLock::TryAcquire`                   |

## Awaiting IO

Only a driver makes a file awaitable. A consumer that awaits reads always holds
one: a loop driver (`IoDriver::Create(loop)`, IOCP on Windows, io_uring on
Linux), or an inline driver when it has no loop, whose operations complete on
the awaiting thread. It never wraps a blocking device to make it look
awaitable. Memory devices are awaitable by themselves; a `MappedFile` never is,
because reading a mapping blocks on page faults no operation can report. A
consumer that parses a mapping and awaits payloads from the same file opens it
a second time through its driver.

## File locks

`FileLock::TryAcquire(path, mode, open_mode)` owns an operating-system lock
until destruction or move replacement; contention returns
`std::errc::device_or_resource_busy`. `FileLock::Acquire` waits until the lock
is granted. Shared locks coexist; an exclusive lock excludes both shared and
exclusive owners. Each acquisition opens its own handle, so threads of one
process exclude each other as processes do.

Windows uses `LockFileEx`; POSIX uses `flock`. These are cooperative locks:
every reader and publisher must participate. The lock file is an ordinary
marker, separate from replaceable content; replacing or recreating it while it
has owners breaks its identity. Content's
[generation lifetime](../Content/Docs/loose_cooked_content.md#published-generations)
uses shared and exclusive leases on markers, and the loose cooked index's
writers serialize on `container.index.lock`.

## Atomic replacement

`WriteFileAtomically(path, bytes, durability)` writes a unique sibling, flushes
it to the durability asked and moves it over the destination with
`MoveFileOver`; `AsyncWriteFileAtomically` writes the sibling through a driver.
The parent directory must exist, existing access permissions are preserved, and
callers own locking and revision checks. Readers that open the destination
sharing deletion (`OpenPublishedFile`) keep the bytes they opened.

`kStable` (the default) waits for the disk before the move and makes the move
durable; `kBuffered` leaves both to the OS cache, for files a stable record
elsewhere validates. A successful `FileReplacement` may carry
`durability_error` when the name moved but making it durable failed. The
operation creates no backup.

## IO outside Serio

A file a program parses, and a file it publishes for another reader, goes
through Serio. A whole-file read names a limit for its family of files, and a
larger file is refused with `kTooLarge` before anything is allocated:

| Files                                                  | Limit                                    |
| ------------------------------------------------------ | ---------------------------------------- |
| Import sources                                         | `kDefaultMaxSourceBytes`, set per import |
| Manifests, mounts files, tool requests, import records | `kMaxImportMetadataBytes`                |
| Cooked indexes, descriptors, resource tables, catalogs | `cooked::kMaxMetadataBytes`              |
| Cooked resource data files                             | `cooked::kMaxDataFileBytes`              |
| Scripts                                                | `kMaxScriptBytes`                        |
| Console archive and history                            | `kMaxPersistedFileBytes`                 |
| Player launch files                                    | `kMaxLaunchFileBytes`                    |
| ShaderBake sources, module artifacts, manifest, state  | `shader_bake::kMaxReadBytes`             |

Iostreams and C file IO remain only where Serio adds nothing:

- Text written for people: reports, traces, crash dumps and benchmark output.
  A torn file is rewritten whole on the next run.
- Console scripts, read line by line as a person wrote them.
- The logger's own files.
- Tests and benchmarks, which write and check their own fixtures.
