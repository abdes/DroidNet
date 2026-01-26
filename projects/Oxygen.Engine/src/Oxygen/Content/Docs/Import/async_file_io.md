# Async File I/O Abstraction

**Status:** Approved Design
**Date:** 2026-01-14
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies `IAsyncFileReader`, a platform-agnostic async file I/O
abstraction designed for the Oxygen Engine import pipeline. The API is designed
to be implementable using native async I/O on Windows (IOCP), Linux (io_uring),
and macOS (kqueue/dispatch_io). The current implementation ships a Windows
IOCP-based reader (see `WindowsFileReader`), with additional platform
implementations and optional fallbacks planned in future phases.

---

## Goals

1. **Future-Proof API**: Design for true async I/O without requiring API
   changes when native implementations are added.

2. **Cross-Platform**: Abstract over Windows, Linux, and macOS differences.

3. **OxCo Integration**: Return awaitables compatible with the import event
   loop.

4. **Error Handling**: Use `Result<T, E>` for all operations; no exceptions.

5. **Cancellation**: Support cooperative cancellation via coroutine
   cancellation.

6. **Streaming**: Support both full-file reads and chunked streaming.

---

## API Design

### Error Types

```cpp
namespace oxygen::content::import {

//! File I/O error codes.
enum class FileError : uint32_t {
  kOk = 0,
  kNotFound,
  kAccessDenied,
  kAlreadyExists,
  kIsDirectory,
  kNotDirectory,
  kTooManyOpenFiles,
  kNoSpace,
  kDiskFull,
  kReadOnly,
  kInvalidPath,
  kPathTooLong,
  kIOError,
  kCancelled,
  kUnknown,
};

//! Detailed file error with path and system error code.
struct FileErrorInfo {
  FileError code = FileError::kUnknown;
  std::filesystem::path path;
  std::error_code system_error;
  std::string message;

  [[nodiscard]] auto ToString() const -> std::string;
};

} // namespace oxygen::content::import
```

### IAsyncFileReader Interface

```cpp
namespace oxygen::content::import {

//! Read options for async file operations.
struct ReadOptions {
  //! Offset to start reading from (0 = beginning).
  uint64_t offset = 0;

  //! Maximum bytes to read (0 = entire file from offset).
  uint64_t max_bytes = 0;

  //! Hint for expected file size (enables pre-allocation).
  uint64_t size_hint = 0;

  //! Buffer alignment requirement (0 = no requirement).
  size_t alignment = 0;
};

//! File metadata.
struct FileInfo {
  uint64_t size = 0;
  std::filesystem::file_time_type last_modified;
  bool is_directory = false;
  bool is_symlink = false;
};

//! Async file reader interface.
/*!
 Platform-agnostic interface for asynchronous file reading. Implementations
 may use native async I/O (IOCP, io_uring, kqueue) or ThreadPool-based
 blocking I/O.

 ### Thread Safety

 Implementations must be safe to use from the import event loop thread.
 Multiple concurrent reads to different files are supported.

 ### Cancellation

 All operations respect coroutine cancellation. When canceled, operations
 return `FileError::kCancelled`.

 ### Error Handling

 All operations return `Result<T, FileErrorInfo>`. No exceptions are thrown.
*/
class IAsyncFileReader {
public:
  virtual ~IAsyncFileReader() = default;

  //! Read entire file contents.
  /*!
   @param path    Absolute path to file.
   @param options Read options (offset, max_bytes, etc.)
   @return File contents or error.
  */
  [[nodiscard]] virtual auto ReadFile(
    const std::filesystem::path& path,
    ReadOptions options = {}
  ) -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> = 0;

  //! Get file metadata without reading contents.
  [[nodiscard]] virtual auto GetFileInfo(
    const std::filesystem::path& path
  ) -> co::Co<Result<FileInfo, FileErrorInfo>> = 0;

  //! Check if file exists.
  [[nodiscard]] virtual auto Exists(
    const std::filesystem::path& path
  ) -> co::Co<Result<bool, FileErrorInfo>> = 0;
};

} // namespace oxygen::content::import
```

### Streaming API (Future Extension)

For very large files, a streaming interface avoids loading everything into
memory:

```cpp
namespace oxygen::content::import {

//! Async file stream for chunked reading.
class IAsyncFileStream {
public:
  virtual ~IAsyncFileStream() = default;

  //! Read next chunk of data.
  /*!
   @param buffer      Buffer to read into.
   @param max_bytes   Maximum bytes to read.
   @return Bytes actually read (0 = EOF), or error.
  */
  [[nodiscard]] virtual auto Read(
    std::span<std::byte> buffer,
    size_t max_bytes
  ) -> co::Co<Result<size_t, FileErrorInfo>> = 0;

  //! Get current position in file.
  [[nodiscard]] virtual auto Position() const -> uint64_t = 0;

  //! Seek to position.
  [[nodiscard]] virtual auto Seek(uint64_t position)
    -> co::Co<Result<void, FileErrorInfo>> = 0;

  //! Get file size.
  [[nodiscard]] virtual auto Size() const -> uint64_t = 0;

  //! Close the stream.
  virtual auto Close() -> co::Co<void> = 0;
};

//! Extended reader with streaming support.
class IAsyncFileReaderWithStreaming : public IAsyncFileReader {
public:
  //! Open file for streaming.
  [[nodiscard]] virtual auto OpenStream(
    const std::filesystem::path& path,
    ReadOptions options = {}
  ) -> co::Co<Result<std::unique_ptr<IAsyncFileStream>, FileErrorInfo>> = 0;
};

} // namespace oxygen::content::import
```

---

## Error Mapping

Error mapping is centralized via utility helpers that operate on `std::error_code`.
The public helpers are:

- `auto MapSystemError(std::error_code ec) -> FileError` — maps `std::errc` values
  and platform system error categories (Windows `std::system_category`) to the
  cross-platform `FileError` enum.

- `auto MakeFileError(const std::filesystem::path& path, std::error_code ec)
  -> FileErrorInfo` — convenience to produce a fully-populated `FileErrorInfo`
  with the mapped `FileError`, the supplied path, and the underlying system
  message.

These helpers ensure the same mapping logic is used across Windows and POSIX
paths (mapping `ENOENT`/`ERROR_FILE_NOT_FOUND` → `kNotFound`, permission
errors → `kAccessDenied`, disk full/read-only/path-too-long, cancellation, etc.).

---

## Usage in Import Pipeline

```cpp
// In AsyncImporter construction
file_reader_ = CreateAsyncFileReader(*loop_);

// In texture worker
auto bytes_result = co_await file_reader_->ReadFile(texture_path);
if (!bytes_result) {
  result.status = TextureWorkStatus::kFailed;
  result.error_message = bytes_result.error().ToString();
  co_await results_writer->Send(std::move(result));
  continue;
}

auto cooked = co_await CookTextureAsync(*bytes_result, ...);
```

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md) - Main architecture
- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md) - Texture pipeline
