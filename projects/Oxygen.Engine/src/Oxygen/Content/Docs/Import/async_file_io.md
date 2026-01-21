# Async File I/O Abstraction

**Status:** Approved Design
**Date:** 2026-01-14
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

This document specifies `IAsyncFileReader`, a platform-agnostic async file I/O
abstraction designed for the Oxygen Engine import pipeline. The API is designed
to be implementable using native async I/O on Windows (IOCP), Linux (io_uring),
and macOS (kqueue/dispatch_io), while starting with a ThreadPool-based blocking
implementation.

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
namespace oxygen::io {

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

} // namespace oxygen::io
```

### IAsyncFileReader Interface

```cpp
namespace oxygen::io {

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

} // namespace oxygen::io
```

### Streaming API (Future Extension)

For very large files, a streaming interface avoids loading everything into
memory:

```cpp
namespace oxygen::io {

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

} // namespace oxygen::io
```

---

## Implementation Strategy

### Phase 1: ThreadPool-Based Blocking Implementation

The initial implementation wraps blocking I/O in ThreadPool tasks:

```cpp
namespace oxygen::io {

//! Async file reader using ThreadPool for blocking I/O.
/*!
 This implementation offloads blocking std::ifstream reads to a ThreadPool.
 It is correct but uses thread pool capacity for I/O waits.

 Use this as a baseline until native async I/O is implemented.
*/
class ThreadPoolFileReader final : public IAsyncFileReader {
public:
  explicit ThreadPoolFileReader(std::shared_ptr<co::ThreadPool> thread_pool);

  [[nodiscard]] auto ReadFile(
    const std::filesystem::path& path,
    ReadOptions options = {}
  ) -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> override;

  [[nodiscard]] auto GetFileInfo(
    const std::filesystem::path& path
  ) -> co::Co<Result<FileInfo, FileErrorInfo>> override;

  [[nodiscard]] auto Exists(
    const std::filesystem::path& path
  ) -> co::Co<Result<bool, FileErrorInfo>> override;

private:
  std::shared_ptr<co::ThreadPool> thread_pool_;
};

} // namespace oxygen::io
```

#### Implementation

```cpp
auto ThreadPoolFileReader::ReadFile(
  const std::filesystem::path& path,
  ReadOptions options
) -> co::Co<Result<std::vector<std::byte>, FileErrorInfo>> {

  auto result = co_await thread_pool_->Run(
    [path, options](co::ThreadPool::CancelToken canceled)
      -> Result<std::vector<std::byte>, FileErrorInfo> {

      // Check cancellation before I/O
      if (canceled) {
        return Err(FileErrorInfo{
          .code = FileError::kCancelled,
          .path = path,
        });
      }

      std::error_code ec;
      if (!std::filesystem::exists(path, ec)) {
        return Err(FileErrorInfo{
          .code = FileError::kNotFound,
          .path = path,
          .system_error = ec,
        });
      }

      const auto file_size = std::filesystem::file_size(path, ec);
      if (ec) {
        return Err(FileErrorInfo{
          .code = FileError::kIOError,
          .path = path,
          .system_error = ec,
        });
      }

      // Determine read range
      const uint64_t offset = options.offset;
      const uint64_t max_bytes = options.max_bytes > 0
        ? options.max_bytes
        : (file_size > offset ? file_size - offset : 0);

      if (offset >= file_size) {
        return Ok(std::vector<std::byte>{});
      }

      const auto bytes_to_read = static_cast<size_t>(
        std::min(max_bytes, file_size - offset));

      // Open and read
      std::ifstream file(path, std::ios::binary);
      if (!file) {
        return Err(FileErrorInfo{
          .code = FileError::kAccessDenied,
          .path = path,
        });
      }

      file.seekg(static_cast<std::streamoff>(offset));

      std::vector<std::byte> buffer(bytes_to_read);
      file.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(bytes_to_read));

      if (!file) {
        return Err(FileErrorInfo{
          .code = FileError::kIOError,
          .path = path,
        });
      }

      return Ok(std::move(buffer));
    });

  co_return result;
}
```

### Phase 2: Windows IOCP Implementation (Future)

```cpp
namespace oxygen::io {

//! Async file reader using Windows IOCP.
class IocpFileReader final : public IAsyncFileReader {
public:
  explicit IocpFileReader(HANDLE iocp_handle);

  // ... interface implementation using OVERLAPPED + GetQueuedCompletionStatus
};

} // namespace oxygen::io
```

### Phase 3: Linux io_uring Implementation (Future)

```cpp
namespace oxygen::io {

//! Async file reader using Linux io_uring.
class IoUringFileReader final : public IAsyncFileReader {
public:
  explicit IoUringFileReader(struct io_uring* ring);

  // ... interface implementation using io_uring_prep_read
};

} // namespace oxygen::io
```

### Phase 4: macOS dispatch_io Implementation (Future)

```cpp
namespace oxygen::io {

//! Async file reader using macOS dispatch_io.
class DispatchIoFileReader final : public IAsyncFileReader {
public:
  explicit DispatchIoFileReader(dispatch_queue_t queue);

  // ... interface implementation using dispatch_io_read
};

} // namespace oxygen::io
```

---

## Factory Function

Platform-appropriate implementation is selected at runtime:

```cpp
namespace oxygen::io {

//! Configuration for async file reader.
struct AsyncFileReaderConfig {
  //! ThreadPool for blocking fallback (required for Phase 1).
  std::shared_ptr<co::ThreadPool> thread_pool;

  //! Prefer native async I/O when available.
  bool prefer_native = true;
};

//! Create platform-appropriate async file reader.
/*!
 Returns the best available implementation:
 - Windows: IOCP (when implemented), else ThreadPool
 - Linux: io_uring (when implemented), else ThreadPool
 - macOS: dispatch_io (when implemented), else ThreadPool

 Phase 1 always returns ThreadPoolFileReader.
*/
[[nodiscard]] auto CreateAsyncFileReader(AsyncFileReaderConfig config)
  -> std::unique_ptr<IAsyncFileReader>;

} // namespace oxygen::io
```

---

## Error Mapping

### Windows

```cpp
auto MapWindowsError(DWORD error, const path& p) -> FileErrorInfo {
  FileError code;
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      code = FileError::kNotFound;
      break;
    case ERROR_ACCESS_DENIED:
      code = FileError::kAccessDenied;
      break;
    case ERROR_ALREADY_EXISTS:
      code = FileError::kAlreadyExists;
      break;
    case ERROR_TOO_MANY_OPEN_FILES:
      code = FileError::kTooManyOpenFiles;
      break;
    case ERROR_DISK_FULL:
      code = FileError::kDiskFull;
      break;
    default:
      code = FileError::kIOError;
  }
  return FileErrorInfo{
    .code = code,
    .path = p,
    .system_error = std::error_code(error, std::system_category()),
  };
}
```

### POSIX (Linux/macOS)

```cpp
auto MapPosixError(int err, const path& p) -> FileErrorInfo {
  FileError code;
  switch (err) {
    case ENOENT:
      code = FileError::kNotFound;
      break;
    case EACCES:
    case EPERM:
      code = FileError::kAccessDenied;
      break;
    case EEXIST:
      code = FileError::kAlreadyExists;
      break;
    case EISDIR:
      code = FileError::kIsDirectory;
      break;
    case ENOTDIR:
      code = FileError::kNotDirectory;
      break;
    case EMFILE:
    case ENFILE:
      code = FileError::kTooManyOpenFiles;
      break;
    case ENOSPC:
      code = FileError::kDiskFull;
      break;
    case EROFS:
      code = FileError::kReadOnly;
      break;
    case ENAMETOOLONG:
      code = FileError::kPathTooLong;
      break;
    default:
      code = FileError::kIOError;
  }
  return FileErrorInfo{
    .code = code,
    .path = p,
    .system_error = std::error_code(err, std::generic_category()),
  };
}
```

---

## Usage in Import Pipeline

```cpp
// In AsyncImporter construction
file_reader_ = io::CreateAsyncFileReader({
  .thread_pool = thread_pool_,
  .prefer_native = true,
});

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

## Testing Strategy

### Unit Tests

1. **ThreadPoolFileReader basic operations**
   - Read existing file
   - Read non-existent file
   - Read with offset
   - Read with max_bytes

2. **Error handling**
   - All FileError codes mapped correctly
   - System error preserved

3. **Cancellation**
   - CancelToken checked before I/O
   - Returns kCancelled on cancel

### Integration Tests

1. **Large file reads** (>100MB)
2. **Concurrent reads** (many files in parallel)
3. **Error recovery** (file deleted during read)

---

## Implementation Checklist

### Phase 1 (This Release)

- [ ] `FileError` enum and `FileErrorInfo` struct
- [ ] `IAsyncFileReader` interface
- [ ] `ThreadPoolFileReader` implementation
- [ ] `CreateAsyncFileReader` factory
- [ ] Error mapping for Windows and POSIX
- [ ] Unit tests

### Phase 2+ (Future)

- [ ] Windows IOCP implementation
- [ ] Linux io_uring implementation
- [ ] macOS dispatch_io implementation
- [ ] Streaming interface

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md) - Main architecture
- [texture_work_pipeline_v2.md](texture_work_pipeline_v2.md) - Texture pipeline
