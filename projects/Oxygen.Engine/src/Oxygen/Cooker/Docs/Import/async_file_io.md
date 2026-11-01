# Import File I/O

**Status:** Current
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

The import reads its sources and writes its cooked files through one
`serio::IoDriver`, owned by the import thread's `ImportEventLoop` and created
over its `asio::io_context`. `ImportManifest::AnalyzeSources`, which builds its
own loop, gets its driver the same way.

- **Source reads** go through `IAsyncFileReader`, the import's seam for
  sources. `ImportSourceSnapshot` decorates it to record and replay reads, and
  tests substitute it. Its implementation, `FileSourceReader`, opens each source
  through the driver and awaits `serio::AsyncReadAll`, or, for a range,
  `serio::AsyncReadExactlyAt`.
- **Cooked writes** go through `CookerStorage` only (see below).

## IAsyncFileReader

```cpp
struct ReadOptions {
  uint64_t offset = 0;     // first byte to read
  uint64_t max_bytes = 0;  // 0 reads to the end of the file
};

class IAsyncFileReader {
public:
  virtual auto ReadFile(const std::filesystem::path& path,
    ReadOptions options = {}) -> co::Co<Result<serio::ByteBuffer>> = 0;
  virtual auto GetFileInfo(const std::filesystem::path& path)
    -> co::Co<Result<FileInfo>> = 0;
  virtual auto Exists(const std::filesystem::path& path)
    -> co::Co<Result<bool>> = 0;
};

auto CreateAsyncFileReader(serio::IoDriver& driver)
  -> std::unique_ptr<IAsyncFileReader>;
```

- A range is clamped to the file: an offset past the end reads nothing.
- `GetFileInfo` and `Exists` are file system metadata, not bytes, and stay
  `std::filesystem` queries.
- Cancelling the awaiting task cancels the pending read.

## Errors

Ports return `Result<T>`, whose error is a `std::error_code`. Callers compare
it with Serio's conditions (`serio::Error::kNotFound`, `kAccessDenied`,
`kIsDirectory`) or with `std::errc` (`invalid_argument` for an empty path,
`operation_canceled`). A caller that reports an error names the path in its own
diagnostic.

## Cooked writes

`cooked::ImportStorage`, the role of `CookerStorage` that awaits its writes,
publishes the cooked files. The real file system and the in-memory test storage
implement it, so the in-memory storage covers the import too; storages that
never import (the pak tool's staging overlay) implement `CookerStorage` alone:

- `PublishAsync(driver, path, bytes)` publishes a whole file atomically
  (`serio::AsyncWriteFileAtomically`, buffered durability), for asset and
  resource descriptors and resource tables. A cancelled or failed import never
  leaves a torn descriptor.
- `OpenData(driver, path)` opens a data file (`buffers.data`, `textures.data`,
  `physics.data`) once, created when missing, never truncated and sharing
  reads and writes. The table aggregator holds it and reserves each write's
  range; emitters await `AsyncWriteAt` at the reserved offset, padding
  included.

Each emitter starts its writes as tasks of the import session's nursery
(`WriteGroup`), keeps the first error, and `Finalize` joins them. Cancelling
the session cancels the writes through the driver.
