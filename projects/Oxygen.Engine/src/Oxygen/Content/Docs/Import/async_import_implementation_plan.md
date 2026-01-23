# Async Import Pipeline - Implementation Plan

**Status:** In Progress
**Date:** 2026-01-17
**Estimate:** 7-10 weeks (one engineer)
**Last Updated:** Manifest phase added; TexturePipeline status refreshed

---

## Overview

This plan implements the async import pipeline in 7 phases:

| Phase | Name | Status | Key Deliverables |
| ----- | ---- | ------ | ---------------- |
| 1 | Foundation | ✅ COMPLETE | EventLoop, ThreadPool, ThreadNotification |
| 2 | Async File I/O | ✅ COMPLETE | IAsyncFileReader, WindowsFileReader |
| 3 | AsyncImportService | ✅ COMPLETE | Thread-safe API, job lifecycle |
| 4 | ImportSession + Emitters + Jobs | ⏳ IN PROGRESS | Async writes, emitters, stable indices, job actor + unified cancellation |
| 5 | Manifest Support (Importer-Level) | ⏳ IN PROGRESS | Manifest schema, parser, batch expansion |
| 6 | TexturePipeline | ⏳ IN PROGRESS | Pure compute pipeline, async job wiring |
| 7 | Integration & Polish | ❌ NOT STARTED | End-to-end tests, example, docs |

**Unit test coverage (high-level):** WindowsFileWriter, ImportSession, TextureEmitter, BufferEmitter, AssetEmitter, ImportJob, AsyncImporter.

**How to run tests (canonical):** use the project runner:

`oxyrun asyncimp -- --gtest_filter="*ImportJob*:*AsyncImporter*"`

---

## ⚠️ CRITICAL: Lessons Learned ⚠️

These lessons were learned through painful debugging. **DO NOT IGNORE.**

### 1. NEVER Reinvent Types That Already Exist

The `oxygen::content::import` namespace ALREADY contains:

| Type | Header | Key Members |
| ---- | ------ | ----------- |
| `ImportRequest` | `<Oxygen/Content/Import/ImportRequest.h>` | `std::filesystem::path source_path`, `LooseCookedLayout`, `ImportOptions` |
| `ImportReport` | `<Oxygen/Content/Import/ImportReport.h>` | `std::filesystem::path cooked_root`, `bool success`, `std::vector<ImportDiagnostic>` |
| `ImportDiagnostic` | `<Oxygen/Content/Import/ImportDiagnostics.h>` | `ImportSeverity severity`, `std::string code`, `std::string message` |
| `ImportSeverity` | `<Oxygen/Content/Import/ImportDiagnostics.h>` | `kInfo`, `kWarning`, `kError` |

**Before defining ANY new type, grep the codebase for existing types!**

### 2. Thread Startup Synchronization

When spawning a thread that creates resources, **always use `std::latch`** to
ensure the thread has fully initialized before the constructor returns:

```cpp
std::latch startup_latch_{1};

void StartThread() {
  thread_ = std::thread([this]() { ThreadMain(); });
  startup_latch_.wait();  // Block until thread is ready
}

void ThreadMain() {
  // Create resources...
  event_loop_ = std::make_unique<ImportEventLoop>();
  file_reader_ = CreateAsyncFileReader(*event_loop_);

  startup_latch_.count_down();  // Signal ready

  event_loop_->Run();  // Now safe to use
}
```

### 3. Shutdown Flag vs. Shutdown Complete Flag

**Use TWO separate flags:**

- `shutdown_requested_`: Set by `RequestShutdown()`, prevents new jobs
- `shutdown_complete_`: Set by `Shutdown()`, prevents double-cleanup

```cpp
// WRONG: Using same flag for both causes early return
void RequestShutdown() { shutdown_.store(true); }
void Shutdown() {
  if (shutdown_.exchange(true)) return;  // bad: Returns if RequestShutdown was called!
  // ... cleanup never runs ...
}

// RIGHT: Separate concerns
void RequestShutdown() { shutdown_requested_.store(true); }
void Shutdown() {
  if (shutdown_complete_.exchange(true)) return;  // Only returns if Shutdown already ran
  shutdown_requested_.store(true);  // Also mark requested
  // ... cleanup always runs ...
}
```

### 4. Resource Cleanup Order in Threaded Code

Resources created on a background thread must be destroyed on that same thread
BEFORE the destructor tries to access them. Use `join()` properly:

```cpp
// WRONG: Destructor destroys event_loop_ while thread still uses it
~Impl() = default;  // Destroys members including event_loop_
                    // But thread might still be running!

// RIGHT: Thread cleans up its own resources, then exits
void Shutdown() {
  event_loop_->Stop();       // Signal thread to exit
  thread_.join();            // Wait for thread to finish cleanup
}                            // Now safe to destroy Impl

void ThreadMain() {
  event_loop_->Run();        // Blocks until Stop()
  file_reader_.reset();      // Thread cleans up its resources
  event_loop_.reset();
  // Thread exits, join() returns
}
```

### 5. Include Style

**NEVER use relative `""` includes. ALWAYS use fully-qualified `<Oxygen/...>`:**

```cpp
// WRONG
#include "ImportEventLoop.h"
#include "../ImportRequest.h"

// RIGHT
#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/Content/Import/ImportRequest.h>
```

### 6. Logging Format

**Use fmt-style `{}` format strings, NOT printf-style:**

```cpp
// WRONG
LOG_F(INFO, "Job %llu: %s", job_id, path.c_str());

// RIGHT
DLOG_F(INFO, "Job {}: {}", job_id, path.string());
```

### 7. Emitters MUST Match PAK Format Exactly

**Before implementing any emitter, study the existing sync implementation:**

1. **Check the PAK format structs** in `<Oxygen/Data/PakFormat.h>`:
   - `TextureResourceDesc` (40 bytes, packed)
   - `BufferResourceDesc` (32 bytes, packed)
   - Alignment requirements (`kRowPitchAlignment = 256`)

2. **Study the existing sync emitter** in `emit/TextureEmitter.cpp`:
   - How does `AppendResource()` handle alignment padding?
   - What fields are populated in table entries?
   - What alignment is used between resources?

3. **Tests must verify actual file content**, not just API behavior:
   - Parse the written `.table` file and verify offsets are aligned
   - Verify `.data` file includes padding bytes between resources
   - Verify table entries match what the loader expects

```cpp
// WRONG: Test only checks API returns expected values
EXPECT_EQ(emitter.Count(), 2);  // Fake test - doesn't verify file content

// RIGHT: Test verifies actual PAK format compliance
const auto table = ParseTextureTable(ReadBinaryFile(table_path));
EXPECT_EQ(table[1].data_offset, AlignUp(table[0].size_bytes, 256));
```

1. **Alignment is critical for GPU uploads:**
   - D3D12 requires `D3D12_TEXTURE_DATA_PITCH_ALIGNMENT` (256 bytes)
   - Each texture must start at an aligned offset
   - Padding bytes (zeros) must be written between textures

### 8. Handle [[nodiscard]] Return Values Properly

**Never use `(void)` to suppress `[[nodiscard]]` warnings. Handle the result:**

```cpp
// WRONG: Silencing the warning without handling the error
(void)writer.WriteBlob(data);

// RIGHT: Check the result and handle errors
auto result = writer.WriteBlob(data);
if (!result.has_value()) {
  LOG_F(ERROR, "WriteBlob failed");
  co_return false;
}

### 9. Completion Must Be Cancellation-Safe

If a coroutine is canceled while suspended on a cancellable `co_await`, code
after that await may never run. Any “must-run” cleanup and the **single
completion notification** must be placed in cancellation-safe branches (e.g.
guarded via `co::UntilCancelledAnd(...)` / mux patterns).

This is especially important for the async import contract where cancellation
must be reported through exactly one path (`on_complete(..., report)` with
`success=false` + diagnostic code `import.canceled`).

### 10. Tests Must Not Pollute the Repo Root

Async import jobs can write cooked output (`.cooked/...`) as part of session
finalization. Tests must:

- Set `ImportRequest::cooked_root` to a per-test temp directory.
- Clean up that directory recursively in fixture teardown so cleanup runs on
  both success and failure.
```

---

## Phase 1: Foundation (Week 1) ✅ COMPLETE

### Objective

Establish the async infrastructure: event loop, ThreadNotification, and
basic threading.

### Tasks

#### 1.1 Import Event Loop

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h/.cpp`

- [X] Create `ImportEventLoop` class wrapping ASIO io_context
- [X] Implement `Run()`, `Stop()`, `Post()` methods
- [X] Add work guard to prevent premature exit
- [X] Unit test: event loop starts, posts callback, stops

#### 1.2 EventLoopTraits Specialization

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h`

- [X] Specialize `co::EventLoopTraits<ImportEventLoop>`
- [X] Implement `EventLoopId()`, `Run()`, `Stop()`, `IsRunning()`
- [X] Unit test: traits work with `co::Run()`

#### 1.3 ThreadNotification Specialization

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h`

- [X] Specialize `co::ThreadNotification<ImportEventLoop>`
- [X] Implement using `asio::post()` or equivalent
- [X] Unit test: notification posts from worker thread to event loop

#### 1.4 ThreadPool Integration

**File:** Unit test only

- [X] Construct `co::ThreadPool` with `ImportEventLoop`
- [X] Verify `ThreadPool::Run()` works correctly
- [X] Verify results return via ThreadNotification
- [X] Unit test: CPU-bound work offloads and returns

### Deliverables

- ✅ `ImportEventLoop` class with ASIO integration
- ✅ EventLoopTraits and ThreadNotification specializations
- ✅ ThreadPool working with custom event loop
- ✅ All 12 unit tests passing

### Acceptance Criteria

```cpp
// This works:
ImportEventLoop loop;
co::ThreadPool pool(loop, 4);

co::Run(loop, [&]() -> co::Co<> {
  auto result = co_await pool.Run([]{ return 42; });
  EXPECT_EQ(result, 42);
  co_return;
}());
```

---

## Phase 2: Async File I/O (Week 2) ✅ COMPLETE

### Objective

Implement the async file reader abstraction with Windows IOCP-based
native implementation.

### Tasks

#### 2.1 Error Types

**File:** `src/Oxygen/Content/Import/Async/FileError.h/.cpp`

- [X] Define `FileError` enum
- [X] Define `FileErrorInfo` struct with `ToString()`, `IsError()`
- [X] Implement Windows error mapping via `MapSystemError()`
- [X] Implement POSIX error mapping via `MapSystemError()`
- [X] Unit test: error mapping coverage (21 tests)

#### 2.2 IAsyncFileReader Interface

**File:** `src/Oxygen/Content/Import/Async/IAsyncFileReader.h`

- [X] Define `ReadOptions` struct (offset, max_bytes, size_hint, alignment)
- [X] Define `FileInfo` struct (size, last_modified, is_directory, is_symlink)
- [X] Define `IAsyncFileReader` interface with `Co<Result<T>>` returns
- [X] Document thread safety and cancellation

#### 2.3 WindowsFileReader Implementation

**File:** `src/Oxygen/Content/Import/Async/WindowsFileReader.h/.cpp`

- [X] Implement `ReadFile()` using ASIO random_access_handle + IOCP
- [X] Implement `GetFileInfo()` via std::filesystem
- [X] Implement `Exists()` via std::filesystem
- [X] Unit test: read existing file (small and large)
- [X] Unit test: read non-existent file (error)
- [X] Unit test: read with offset and max_bytes
- [X] Unit test: GetFileInfo for file and directory
- [X] Unit test: Exists for file, directory, and non-existent
- [X] 14 WindowsFileReader tests passing

#### 2.4 Factory Function

**File:** `src/Oxygen/Content/Import/Async/WindowsFileReader.h`

- [X] Implement `CreateAsyncFileReader(ImportEventLoop&)`
- [X] Currently returns WindowsFileReader on Windows
- [X] Document future Linux (io_uring) implementation

### Deliverables

- ✅ Complete `IAsyncFileReader` abstraction
- ✅ Windows IOCP-based implementation (no ThreadPool blocking for I/O)
- ✅ All 35 Phase 2 unit tests passing (21 FileError + 14 WindowsFileReader)

### Acceptance Criteria

```cpp
ImportEventLoop loop;
auto reader = CreateAsyncFileReader(loop);

co::Run(loop, [&]() -> co::Co<> {
  auto result = co_await reader->ReadFile("test.txt");
  EXPECT_TRUE(result.has_value());
  EXPECT_FALSE(result.value().empty());
  co_return;
});
```

---

## Phase 3: AsyncImportService Shell (Week 3) ✅ COMPLETE

### Objective

Implement the public thread-safe API and import thread lifecycle.

### Tasks

#### 3.1 Job Types

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.h`

- [X] Define `ImportJobId` type (uint64_t)
- [X] Define `ImportCompletionCallback` type (uses existing `ImportReport`)
- [X] Define `ImportProgressCallback` type (uses existing `ImportDiagnostic`)
- [X] Define `ImportCancellationCallback` type
- [X] Define `ImportProgress` struct
- [X] Define `Config` struct

**CRITICAL:** Reuses existing types from `Import/` module:

- `ImportRequest` from `<Oxygen/Content/Import/ImportRequest.h>`
- `ImportReport` from `<Oxygen/Content/Import/ImportReport.h>`
- `ImportDiagnostic` from `<Oxygen/Content/Import/ImportDiagnostics.h>`

#### 3.2 Import Thread Lifecycle

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [X] Spawn import thread in constructor
- [X] Create ImportEventLoop on import thread
- [X] Create IAsyncFileReader on import thread (via `CreateAsyncFileReader`)
- [X] Implement graceful shutdown in destructor
- [X] Use `std::latch` for startup synchronization
- [X] Use separate `shutdown_complete_` flag for shutdown tracking
- [X] Unit test: construct and destruct without crash (4 tests)
- [X] Unit test: multiple construct/destruct cycles

#### 3.3 Thread-Safe Job Submission

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [X] Implement thread-safe request queue (mutex + `std::queue`)
- [X] Implement atomic job ID generation
- [X] Implement `SubmitImport()` posting to import thread via `event_loop_->Post()`
- [X] Unit test: submit from main thread (5 tests)
- [X] Unit test: submit from multiple threads concurrently (1 test)

#### 3.4 Result Callback Dispatch

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [X] Callbacks invoked on import thread (simplified for shell)
- [X] Unit test: callback invoked with correct job ID

#### 3.5 Cancellation API

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [X] Implement `CancelJob()` - returns false for invalid/completed jobs
- [X] Implement `CancelAll()` - cancels pending and marks active for cancel
- [X] Implement `RequestShutdown()` - non-blocking, sets flag
- [X] Unit test: cancel pending job (3 tests)
- [X] Unit test: shutdown tests (2 tests)

#### 3.6 Query API

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [X] Implement `IsAcceptingJobs()`
- [X] Implement `IsJobActive()`
- [X] Implement `PendingJobCount()`
- [X] Implement `InFlightJobCount()`
- [X] Unit tests (2 tests)

### Deliverables

- ✅ Public `AsyncImportService` class
- ✅ Thread-safe submission and cancellation
- ✅ Proper lifecycle management with startup/shutdown synchronization
- ✅ All 17 unit tests passing (64 total with Phase 1+2)

### Acceptance Criteria

```cpp
AsyncImportService service;
std::latch done(1);

auto id = service.SubmitImport(
  ImportRequest{.source_path = "test.fbx"},
  [&](ImportJobId id, const ImportReport& report) {
    // report.success available
    done.count_down();
  });

done.wait();
// Job completed
```

---

## Phase 4: ImportSession + Emitters + Async File I/O Write (Week 4) ⏳ IN PROGRESS

### Objective

Implement ImportSession with lazy emitters and async file write support.

### Background

The new design separates concerns:

- **AsyncImporter**: Job scheduler (import-thread loop)
- **ImportSession**: Per-job state + access to infra (async file reader/writer, ThreadPool)
- **Emitters**: Async I/O writers returning stable indices immediately

### Tasks

#### 4.1 IAsyncFileWriter Interface

**File:** `src/Oxygen/Content/Import/Async/IAsyncFileWriter.h`

- [X] Define `WriteOptions` struct (alignment, create_directories, overwrite, share_write)
- [X] Define `WriteCompletionCallback` type
- [X] Define `IAsyncFileWriter` interface
- [X] `Write(path, data, options)` - coroutine returning bytes written
- [X] `WriteAsync(path, data, options, callback)` - fire-and-forget with callback
- [X] `WriteAt(path, offset, data, options)` - coroutine returning bytes written
- [X] `WriteAtAsync(path, offset, data, options, callback)` - fire-and-forget with callback
- [X] `Flush()` - wait for all pending operations
- [X] `CancelAll()` - cancel pending operations
- [X] `PendingCount()` - return number of pending operations
- [X] Document thread safety

#### 4.2 WindowsFileWriter Implementation

**File:** `src/Oxygen/Content/Import/Async/WindowsFileWriter.h/.cpp`

- [X] Implement using ASIO + IOCP for async writes
- [X] Track pending write count via atomic
- [X] Use `co::SleepFor` for yielding (consistent with OxCo patterns)
- [X] Unit test: write small file (3 tests)
- [X] Unit test: write large file (1 test)
- [X] Unit test: explicit-offset write operations via `WriteAt*`
- [X] Unit test: async `WriteAsync`/`WriteAtAsync` with callbacks
- [X] Unit test: flush waits for pending (2 tests)
- [X] Unit test: cancellation (2 tests)
- [X] Unit test: error handling (4 tests)
- [X] **20 unit tests**

#### 4.3 ImportSession Class

**File:** `src/Oxygen/Content/Import/Async/ImportSession.h/.cpp`

- [X] Define `ImportSession` class
- [X] Constructor takes `ImportRequest`, `IAsyncFileWriter&`
- [X] Own `LooseCookedWriter` for index file
- [X] Thread-safe diagnostics collection with `AddDiagnostic()`
- [X] `HasErrors()` to check for error-level diagnostics
- [X] `Finalize()` coroutine that waits for I/O and writes index
- [X] Unit tests: 14 tests covering construction, diagnostics, finalization

#### 4.4 TextureEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/TextureEmitter.h/.cpp`

- [X] Implement `Emit(CookedTexturePayload) -> uint32_t`
  - Assign stable index immediately (texture index 0 reserved for fallback)
  - Reserve a unique aligned range in `textures.data` (atomic CAS on size)
  - Write optional zero-padding and payload using `WriteAtAsync()`
    (requires `WriteOptions{.share_write=true}`)
  - Add `TextureResourceDesc` entry to in-memory table
- [X] Implement `Count()`, `PendingCount()`, `ErrorCount()`, `DataFileSize()`
- [X] Implement `Finalize() -> co::Co<bool>`
  - Wait for pending writes via `Flush()`
  - Serialize table via `serio::Writer` with packed alignment
  - Write `textures.table` file
- [X] Unit tests: 14 tests covering emission, alignment, finalization, content verification

#### 4.5 BufferEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/BufferEmitter.h/.cpp`

- [X] Same pattern as TextureEmitter for geometry/animation buffers
  - Per-buffer alignment (vertex=16, index=4, etc.)
  - CAS loop for atomic aligned offset reservation
  - Padding writes between buffers
- [X] Emit returns stable index immediately
- [X] Finalize waits for I/O and writes `buffers.table`
- [X] Unit tests: 16 tests covering PAK format compliance
  - Table file packed size verification (32 bytes per entry)
  - Aligned offset verification in table entries
  - Data file content verification with padding
  - Metadata preservation (usage_flags, element_stride, content_hash)

#### 4.6 AssetEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/AssetEmitter.h/.cpp`

- [X] Implement `Emit(AssetKey, AssetType, virtual_path, descriptor_relpath, bytes)`
- [X] Write `*.omat`, `*.ogeo`, `*.oscene` via async I/O (`WriteAsync`)
- [X] Track pending writes and I/O errors
- [X] Implement `Finalize() -> co::Co<bool>` (waits for pending I/O)
- [X] Unit tests for path validation, emission, and finalization

#### 4.7 ImportSession Lazy Emitters

**File:** `src/Oxygen/Content/Import/Async/ImportSession.cpp`

- [X] Implement `TextureEmitter() -> TextureEmitter&` (lazy create)
- [X] Implement `BufferEmitter() -> BufferEmitter&` (lazy create)
- [X] Implement `AssetEmitter() -> AssetEmitter&` (lazy create)
- [X] Unit test: lazy creation on first access

#### 4.8 ImportSession Finalization

**File:** `src/Oxygen/Content/Import/Async/ImportSession.cpp`

- [X] Extend `Finalize()` to orchestrate emitters (if created)
  - Await each emitter's `Finalize()`
  - Only then write `container.index.bin` (LAST)
- [X] Integration test: session + emitters finalize correctly

#### 4.9 Job-Based Execution Model (Required)

This phase replaces the prior “AsyncImporter drives session + importer” model.
There must be **no synchronous importer fallback**.

**Ownership rules (must match design):**

- **AsyncImportService** decides which concrete job to create.
- **AsyncImporter** activates the job and runs it.
- **ImportJob** is a `co::LiveObject` owning a **per-job child nursery**.
- **ImportSession** is owned by the concrete job and is strictly per-job.
- **Pipelines are job-scoped**. Cancellation is expressed by cancelling the
  job nursery (pipelines must not expose Cancel/CancelAll as the primary
  mechanism).

##### 4.9.1 Make ImportJob a LiveObject (base class)

**Files:**

- `src/Oxygen/Content/Import/Async/Detail/ImportJob.h/.cpp`

Tasks:

- [X] Change `detail::ImportJob` to derive from `co::LiveObject`.
- [X] Implement activation using `co::OpenNursery(...)` (job-scoped nursery).
- [X] Implement a single override point for job work (e.g. `ExecuteAsync()`).
- [X] Implement `Stop()` to request stop + cancel the job nursery.
- [X] Guarantee exactly one completion notification (`on_complete`) even when
  the job coroutine is canceled during shutdown.

##### 4.9.2 Define concrete jobs (orchestration lives in jobs)

**Files (new):**

- `src/Oxygen/Content/Import/Async/Jobs/FbxImportJob.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/GlbImportJob.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/TextureImportJob.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/AudioImportJob.h/.cpp` (skeleton)

Tasks:

- [X] Introduce a concrete placeholder job (`DefaultImportJob`) that owns an
  `ImportSession` and exercises session finalization.
- [X] Add format-specific jobs (FBX/GLB/etc.) and move orchestration there.
- [X] Keep cancellation handling unified: cancelling the job nursery completes
  via `on_complete(..., report)` with `success=false` and `import.canceled`.

@note FBX/GLB jobs currently wire geometry only; texture/material/scene wiring
      remains pending.

@note At this phase, non-FBX jobs may remain skeletons, but the plumbing must
      exist so the service can select them and the importer can activate/run
      them.

##### 4.9.3 AsyncImportService selects the concrete job

**Files:**

- `src/Oxygen/Content/Import/Async/AsyncImportService.h/.cpp`

Tasks:

- [X] Add initial job selection in `SubmitImport()` (current: placeholder job).
- [X] Keep the public API stable: callers still submit `ImportRequest` and
  receive `ImportReport` via `on_complete`.

##### 4.9.4 AsyncImporter activates and runs the job

**Files:**

- `src/Oxygen/Content/Import/Async/Detail/AsyncImporter.h/.cpp`

Tasks:

- [X] Replace “single coroutine job execution” with “activate + run job
  LiveObject”.
- [X] Ensure all job work dispatching occurs on the AsyncImporter thread.
- [X] Ensure cancellation requests from the service are forwarded to the
  job’s `Stop()` and reported via `on_complete`.

##### 4.9.5 Align pipeline cancellation semantics (walk the walk)

**Files:**

- `src/Oxygen/Content/Import/Async/Pipelines/BufferPipeline.h/.cpp`
- (and any new pipeline interfaces introduced later)

Tasks:

- [X] Remove `Cancel`/`CancelAll` from pipeline public APIs.
- [X] Ensure pipelines support:
  - `Start(co::Nursery&)`
  - bounded `Submit`/`TrySubmit`
  - `Collect()`
  - `Close()` (stop accepting new work and allow draining)
- [X] Ensure cancellation of in-flight work is done via:
  - job nursery cancellation, and
  - cooperative stop tokens checked by work items and ThreadPool tasks.
- [X] Update any pipeline concepts/specs in the codebase to remove
  `CancelAll()` requirements.

##### 4.9.6 Tests (must cover the new execution model)

**Files:**

- `src/Oxygen/Content/Test/Import/Async/AsyncImporter_test.cpp`
- `src/Oxygen/Content/Test/Import/Async/AsyncImportService_test.cpp`

Tasks:

- [X] Job activation: service selects a job, importer activates and runs it.
- [X] Cancellation:
  - pending job cancellation completes via `on_complete` with
    `import.canceled`.
  - in-flight job cancellation cancels the job nursery and completes via
    `on_complete` with `import.canceled`.
- [X] Ensure job-based tests isolate output via `ImportRequest::cooked_root`
  and cleanup recursively in teardown.

### Deliverables

- ✅ `IAsyncFileWriter` abstraction with Windows implementation
- ✅ `TextureEmitter`, `BufferEmitter`, `AssetEmitter`
- ✅ Stable index assignment with async I/O (explicit-offset `WriteAt*`)
- ✅ `ImportSession` with lazy emitters
- ✅ Session finalization that awaits emitters then writes index last
- ✅ Job-based execution: ImportJob LiveObject + per-job nursery + job selection
  plumbing + cancellation aligned to job nursery
- ⏳ Remaining: format-specific jobs + manifest-driven job wiring

**Notes (implementation update):**

- AsyncImporter now requires an `IAsyncFileWriter` to be configured (wired in
  AsyncImportService on the import thread) so it can construct and finalize an
  ImportSession.
- A small integration test was added to ensure a submitted job produces a
  `container.index.bin` at the job's cooked root.

**Notes (design update - required):**

- The async import system must not call synchronous importers as a fallback.
  Format-specific logic will live in concrete jobs (e.g. `FbxImportJob`).

### Acceptance Criteria

```cpp
// Emitter returns index immediately, I/O is async
TextureEmitter emitter(async_io, layout, root);
auto idx = emitter.Emit(cooked_texture);  // Returns NOW

auto idx2 = emitter.Emit(another_texture);
EXPECT_NE(idx2, idx);

// Finalize waits for I/O and flushes table
co_await emitter.Finalize();
// textures.data and textures.table now on disk
```

---

## Phase 5: Manifest Support (Library-Integrated) (Week 5)

### Objective

Make import manifests a first-class engine feature that expands into standard
import jobs with deterministic behavior and diagnostics.

### Tasks

#### 5.1 Manifest Data Model + Schema

**Files (Integrated):**

- `src/Oxygen/Content/Import/ImportManifest.h/.cpp`
- `src/Oxygen/Content/Import/Internal/ImportManifest_schema.h`

Tasks:

- [X] Define `ImportManifest`, `ImportManifestDefaults`, and job records.
- [X] Encode the full manifest schema (versioned, strict validation).
- [X] Include texture settings parity (multi-source mappings, mip filter space, more normal-map options).

#### 5.2 Manifest Loader + Validation

**Files (Integrated):**

- `src/Oxygen/Content/Import/ImportManifest.h/.cpp`

Tasks:

- [X] Load/validate JSON (schema + semantic checks).
- [X] Resolve paths relative to manifest root or explicit override.
- [X] Normalize paths for deterministic IDs and diagnostics.
- [X] Emit diagnostics with job index + JSON pointer context.

#### 5.3 Job Expansion + Routing

**Files (Integrated):**

- `src/Oxygen/Content/Import/AsyncImportService.h/.cpp`
- `src/Oxygen/Content/Import/ImportManifest.h/.cpp`

Tasks:

- [X] Add `SubmitManifest(...)` API to `AsyncImportService`.
- [X] Move `SubmitManifest` logic from tool to library core.
- [X] Expand manifest jobs into `ImportRequest`/texture jobs with stable order.
- [X] Route `job_type` to concrete jobs (texture, fbx, glb, etc.).
- [X] Ensure manifests do not perform I/O during expansion.

#### 5.4 Reporting + Dry-Run

**Files:**

- `src/Oxygen/Content/Tools/ImportTool/BatchCommand.cpp`

Tasks:

- [X] Support validation-only (dry-run) mode with no cooking (tool layer).
- [X] Write JSON report with per-job status + telemetry (tool layer).
- [X] Support per-job progress tracking and logging.

#### 5.5 Tests

**Files:**

- `src/Oxygen/Content/Test/Import/Async/ImportManifest_test.cpp`
- `src/Oxygen/Content/Test/Import/Async/ImportManifest_batch_test.cpp`

Tasks:

- [ ] Schema validation (valid/invalid, unknown fields, version mismatch).
- [ ] Path resolution and normalization tests.
- [ ] Job expansion order + deterministic IDs.
- [ ] Dry-run behavior (no I/O, diagnostics only).

#### 5.6 Tool Integration (Thin Wrapper)

**Files:**

- `src/Oxygen/Content/Tools/ImportTool/BatchCommand.cpp`

Tasks:

- [ ] Reuse importer manifest API instead of duplicating parsing logic.
- [ ] Keep tool-specific UI/reporting as a thin layer.

### Deliverables

- Importer-owned manifest schema + loader
- Manifest expansion into concrete jobs
- Diagnostics and reporting parity with tool output

---

## Phase 6: TexturePipeline (Pure Compute) (Week 6-7)

### Objective

Implement TexturePipeline as pure compute (no I/O). Pipeline cooks textures
on ThreadPool, returns cooked data to caller who emits via Emitter.

### Key Design Points

- **Pipeline = Pure Compute**: Load → Decode → Transcode (BC7, mips)
- **Pipeline does NOT emit**: Returns `CookedTexture` to caller
- **Caller emits**: `tex_emitter.Emit(cooked)` → index + async I/O
- **Concept-checked**: `static_assert(ResourcePipeline<TexturePipeline>)`

### Tasks

#### 6.0 ImportSession Infrastructure Ownership Refactor

**Files:**

- `src/Oxygen/Content/Import/Async/ImportSession.h/.cpp`
- `src/Oxygen/Content/Import/Async/AsyncImportService.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/*.h/.cpp`

Tasks:

- [X] Update `ImportSession` constructor to take `observer_ptr` (or owning smart
  pointers) for `IAsyncFileReader`, `IAsyncFileWriter`, and `co::ThreadPool`.
- [X] Store infra handles as `oxygen::observer_ptr` (no reference members).
- [X] Add accessors for `FileReader()`, `FileWriter()`, and `ThreadPool()`.
- [X] Update job construction so the job (as ImportSession owner) provides
  infra.
- [X] Update tests to use the new constructor and accessors.

#### 6.1 TexturePipeline Types

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.h`

- [X] Define `FailurePolicy` and `SourceContent` per
      `design/texture_work_pipeline_v2.md` (`SourceBytes`, `TextureSourceSet`,
      `ScratchImage`).
- [X] Define `WorkItem` with fields: `source_id`, `texture_id`, `source_key`,
      `desc`, `packing_policy_id`, `output_format_is_override`,
      `failure_policy`, `source`, `stop_token`.
- [X] Define `WorkResult` with fields: `source_id`, `texture_id`, `source_key`,
      `cooked`, `used_placeholder`, `diagnostics`, `success`.
- [X] Add `Config` struct (queue capacity, worker count) and store as member.

#### 6.2 Pipeline Progress Reporting (Design Task)

**Files:** (design + header updates)

- `design/async_import_pipeline_v2.md`
- `src/Oxygen/Content/Import/Async/ResourcePipeline.h`

Tasks:

- [X] Define `PipelineProgress` fields and invariants in the design doc.
- [X] Add `GetProgress()` to `TexturePipeline` and update the
  `ResourcePipeline` concept.
- [X] Define update points: increment `submitted` on successful submit and
  `completed/failed` on `Collect()`.

#### 6.3 ResourcePipeline Concept

**File:** `src/Oxygen/Content/Import/Async/ResourcePipeline.h`

- [X] Implement the concept to match BufferPipeline-style semantics.
- [X] `static_assert(ResourcePipeline<TexturePipeline>)`.

#### 6.4 TexturePipeline Implementation

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.h/.cpp`

- [X] Add `input_channel_` and `output_channel_` with bounded capacity.
- [X] Track `pending_` and progress counters using the same rules as
  BufferPipeline.
- [X] Implement `Start()` to spawn `Config::worker_count` coroutines.
- [X] Implement `Submit()`/`TrySubmit()` with backpressure and progress updates.
- [X] Implement `Collect()` with sentinel result on closed output channel.
- [X] Implement `HasPending()`, `PendingCount()`, `Close()`, `GetProgress()`.

#### 6.5 TexturePipeline Worker Loop

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.cpp`

- [X] Implement `Worker()` loop patterned after BufferPipeline.
- [X] Early-cancel: if `stop_token` requested, emit `success=false` result.
- [X] Resolve packing policy via `TexturePackingPolicy` helpers (no legacy).
- [X] Build local `TextureImportDesc` (set `source_id`, `stop_token`).
- [X] Cook using the correct overload for `SourceContent`:
  `SourceBytes`, `TextureSourceSet` (cube + array), or `ScratchImage`.
- [X] Implement non-cube multi-source assembly for `TextureSourceSet`
  (array layers), with validation of dimension/format parity.
- [X] Add 3D depth-slice assembly for `TextureSourceSet` in the async
  pipeline (parity with `ImportTexture3D`).
- [X] If `output_format_is_override == false`, preserve decoded format and
  set `bc7_quality = kNone` to match sync path.
- [X] On error: return placeholder when `failure_policy == kPlaceholder` and
  not canceled; otherwise emit diagnostics with `success=false`.

#### 6.6 Reuse Existing Sync Cooking Logic

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.cpp`

- [X] Call `CookTexture(...)` inside `thread_pool_.Run(...)` (pipeline stays
  compute-only).
- [X] Placeholder path maps to fallback texture index `0` and returns
  `used_placeholder = true` (no payload emission, no legacy helpers).
- [X] Add byte-for-byte parity tests against sync cooker output.

#### 6.6a Legacy Cooker Gap Fixes (Make New System Complete)

**Files:**

- `src/Oxygen/Content/Import/TextureCooker.*`
- `src/Oxygen/Content/Import/TextureImportTypes.h`
- `src/Oxygen/Content/Import/TextureSourceAssembly.*`
- `design/texture_work_pipeline_v2.md`

Tasks:

- [X] Fix `HdrHandling::kKeepFloat` to **override** `output_format` to a float
  format when HDR input is detected, instead of failing with
  `kHdrRequiresFloatFormat`.
- [X] Update validation logic so `kKeepFloat` no longer follows the `kError`
  path when `bake_hdr_to_ldr == false`.
- [X] Add regression tests for HDR inputs with `kKeepFloat` verifying the
  float output format and successful cook.
- [X] Implement non-cube multi-source assembly for `TextureSourceSet`
  (2D array layers and pre-authored mips).
- [X] Add validation tests for mismatched dimensions across layers and for
  missing mip levels.
- [X] Add unit tests for non-cube multi-source inputs (array layers),
  including validation failures and successful cooking parity.
- [X] Update the texture pipeline design doc to reflect the corrected HDR
  policy and expanded multi-source support.

#### 6.6b GeometryPipeline (Compute-Only) Implementation + Tests

**Files:**

- `src/Oxygen/Content/Import/Async/GeometryPipeline.h/.cpp`
- `src/Oxygen/Content/Import/Async/ResourcePipeline.h`
- `design/geometry_work_pipeline_v2.md`
- `src/Oxygen/Content/Test/Import/Async/GeometryPipeline_test.cpp`

Tasks:

- [X] Implement `GeometryPipeline` API skeleton (channels, Start/Submit/
  TrySubmit/Collect/Close) with `PipelineProgress` tracking.
- [X] Define data model helpers for LOD validation, attribute masks, and
  material bucketing (no I/O).
- [X] Implement worker loop and cancellation via `WorkItem.stop_token`.
- [X] Implement coordinate conversion policy plumbing (swap Y/Z policy).
- [X] Implement LOD validation and diagnostics (missing positions/indices,
  range validation).
- [X] Implement vertex expansion, bounds computation, and attribute policy
  application (normals/tangents with prerequisite diagnostics).
- [X] Implement submesh bucketing and mesh view layout (sorted by material
  slot, tight ranges).
- [X] Build buffer payloads (vertex/index/aux) honoring alignment and skinned
  mesh requirements; defer content hashing.
- [X] Serialize geometry descriptor bytes (packed, alignment = 1), populate
  headers/variant flags (with placeholder buffer indices).
- [X] Unit tests: basic attribute policy coverage, UV warnings, and descriptor
  header validation.

##### 6.6b Gap Audit (Design Compliance)

The implementation is functional but does not yet fully satisfy
[geometry_work_pipeline_v2.md](geometry_work_pipeline_v2.md). The following
gaps must be closed to declare the design fully implemented:

- [X] **MeshSource policy**: pipeline accepts `TriangleMesh` only; all
  importer-specific meshes must be normalized by adapters before submission.
- [X] **LOD limits + validation**: enforce LOD count range (1..8), per-LOD
  index_count > 0, vertex/index counts fit `uint32_t`, and submesh/view counts
  are non-zero when indices exist. Emit diagnostics for limit violations.
- [X] **Buffer size limits**: enforce `kDataBlobMaxSize` for vertex/index and
  auxiliary buffers; emit diagnostics on overflow.
- [X] **Mesh-type blob serialization**: emit skinned/procedural blobs
  immediately after `MeshDesc` per PAK format; update descriptor layout tests.
  (Current: no mesh-type blob emission in descriptor bytes.)
- [X] **Procedural mesh policy**: support `MeshType::kProcedural` or reject
  with explicit diagnostic per job policy.
  (Current: non-standard/skinned are rejected.)
- [X] **Descriptor finalization API**: expose a helper to patch buffer indices
  into `MeshDesc` and compute `header.content_hash` once indices are known
  (ThreadPool-backed). `Config::with_content_hashing` must be honored.
  (Current: `header.content_hash` stays zero.)
- [X] **Skinned auxiliary buffers**: add inverse bind and joint remap buffers
  (or emit blocking diagnostics if unavailable) to match PAK requirements.
  (Current: only joint indices/weights are emitted.)
- [X] **Name truncation diagnostics**: emit warnings when mesh/LOD/submesh
  names are truncated in packed descriptors.

##### 6.6b Test Gaps

- [X] Add tests for LOD limits and count validation (1..8), including failure
  diagnostics.
- [X] Add tests for buffer size limits (`kDataBlobMaxSize`).
- [X] Add tests for skinned descriptor layout, including mesh-type blob size
  and ordering, and joint buffer alignment.
- [X] Add tests for `header.content_hash` deferral + patching.
- [X] Add tests for name truncation diagnostics.
- [X] Add tests to verify adapters always emit `TriangleMesh` work items.

##### 6.6b.1 Geometry Adapters (Format Bridges)

**Files (new/updated):**

- `src/Oxygen/Content/Import/Async/Adapters/FbxGeometryAdapter.h/.cpp`
- `src/Oxygen/Content/Import/Async/Adapters/GltfGeometryAdapter.h/.cpp`
- `src/Oxygen/Content/Import/Async/Adapters/GeometryAdapterTypes.h`
- `src/Oxygen/Content/Test/Import/Async/GeometryAdapter_*_test.cpp`

Tasks:

- [X] Define adapter data contracts (C++20 value-type adapters, explicit
  `GeometryAdapterInput` / `GeometryAdapterOutput`).
- [X] Define `GeometryAdapter` concept and `BuildWorkItems(...)` API shape.
- [X] Implement FBX adapter using `ufbx`:
  - [X] require triangle faces and build `TriangleRange` per material slot
  - [X] emit `TriangleMesh` for all meshes, including skinned meshes with
    joints/weights
  - [X] handle >4 influences by trimming + renormalizing with diagnostics
- [X] Implement glTF/GLB adapter using `cgltf`:
  - [X] per-primitive `TriangleMesh` (merge only when layouts match)
  - [X] convert indices to `uint32_t` and generate when missing
  - [X] emit diagnostics when indices are missing
  - [X] derive bitangent from tangent.w
  - [X] map materials and detect texture usage for UV warnings
- [X] Wire adapters into `FbxImportJob` / `GlbImportJob` (job submits
  adapter output to `GeometryPipeline`).
- [X] Adapter unit tests:
  - [X] material slot mapping and range ordering
  - [X] tangent/bitangent derivation for glTF
  - [X] skinned mesh detection + joint buffer validation
  - [X] LOD mapping

##### 6.6b.2 Format Adapters (Direct WorkItem Translation)

Goal: evolve FBX/glTF adapters into **format adapters** that parse once and
emit `WorkItem` storage for all pipelines without introducing a new scene
graph or duplicated data model.

Tasks:

- [X] Refactor `FbxGeometryAdapter` / `GltfGeometryAdapter` into
  `FbxAdapter` / `GltfAdapter` that own parse state and emit work items for
  geometry, material, scene, and texture pipelines.
- [X] Ensure `WorkItem` storage is job-owned and referenced via
  `WorkPayloadHandle` in the planner (no payload copying).

#### 6.6c MaterialPipeline (Compute-Only) Implementation + Tests

**Files:**

- `src/Oxygen/Content/Import/Async/MaterialPipeline.h/.cpp`
- `src/Oxygen/Content/Import/Async/ResourcePipeline.h`
- `design/material_work_pipeline_v2.md`
- `src/Oxygen/Content/Test/Import/Async/MaterialPipeline_test.cpp`

Tasks:

- [X] Implement `MaterialPipeline` API with bounded channels and progress.
- [X] Implement worker logic for scalar normalization, domain/alpha mode
  resolution, and ORM detection per design.
- [X] Implement UV transform extension emission and validation paths.
- [X] Serialize `MaterialAssetDesc` + shader refs (packed, alignment = 1).
- [X] Compute `header.content_hash` over descriptor bytes + shader refs.
- [X] Unit tests: ORM policy and texture binding flags, UV transform cases.
- [X] Unit tests: shader stage ordering, truncation warnings, and hash values.

##### 6.6c TODO Follow-ups (v4 UV Transform + Tooling)

- [X] Update `MaterialLoader` tests for v4 UV transform fields.
- [X] Update PakGen tool and schema for v4 UV transform fields.
- [X] Update PakDump tool for v4 UV transform fields.

#### 6.6d ScenePipeline (Compute-Only) Implementation + Tests

**Files:**

- `src/Oxygen/Content/Import/Async/ScenePipeline.h/.cpp`
- `src/Oxygen/Content/Import/Async/ResourcePipeline.h`
- `design/scene_work_pipeline_v2.md`
- `src/Oxygen/Content/Test/Import/Async/ScenePipeline_test.cpp`

Tasks:

- [X] Implement `ScenePipeline` API with bounded channels and progress.
- [X] Implement node traversal, naming, and deterministic IDs.
- [X] Implement node pruning rules with transform preservation.
- [X] Implement component tables (renderables, cameras, lights) and sorting.
- [X] Append environment block (header + records) and validate sizes.
- [X] Compute `header.content_hash` over payload + environment block.
- [ ] Unit tests: pruning policy correctness, component ordering, and hash.
- [ ] Unit tests: environment system validation and error diagnostics.

#### 6.6e Pipeline Conformance + Cancellation Tests

**Files:**

- `src/Oxygen/Content/Test/Import/Async/PipelineConformance_test.cpp`

Tasks:

- [X] Verify all pipelines satisfy `ResourcePipeline` concept invariants.
- [X] Validate progress counters (`submitted`, `completed`, `failed`).
- [X] Cancellation tests: `stop_token` yields `success=false` and no outputs.
- [ ] Backpressure tests: bounded queues block/deny submissions as expected.

#### 6.7 Job-Orchestrated Import Wiring

**Files:**

- `src/Oxygen/Content/Import/Async/Jobs/FbxImportJob.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/GlbImportJob.h/.cpp`
- `src/Oxygen/Content/Import/Async/Jobs/TextureImportJob.h/.cpp`

Tasks:

- [X] FbxImportJob: parse FBX on `ThreadPool()`; use adapter-emitted work
  items; register plan items/dependencies in `ImportPlanner`; submit to
  `TexturePipeline`; collect results and emit via `session.TextureEmitter()`
  with streaming material emission.
- [ ] FbxImportJob: load external texture bytes via `IAsyncFileReader`
  (adapter still performs synchronous file reads for file-backed textures).
- [X] FbxImportJob: submit geometry work to `GeometryPipeline`; emit buffers
  via `BufferEmitter` and descriptors via `AssetEmitter`.
- [X] FbxImportJob: submit material work to `MaterialPipeline`; emit `.omat`
  via `AssetEmitter` once planner dependencies resolve texture indices.
- [X] FbxImportJob: submit scene work to `ScenePipeline`; emit `.oscene` via
  `AssetEmitter` using `geometry_keys`.
- [X] FbxImportJob: build import plan via `ImportPlanner`, await
  `PlanStep.prerequisites`, and submit pipeline work from
  `WorkPayloadHandle` storage.
- [X] GlbImportJob: mirror the FBX flow for glTF/GLB (subset acceptable).
- [X] GlbImportJob: wire geometry, material, and scene pipelines with the
  same emitter flow (subset acceptable).
- [X] TextureImportJob: read bytes via `FileReader()`, submit to pipeline,
  emit via `TextureEmitter()`.
- [X] Integration tests for job wiring and cooked output.

Additional gaps to close:

- [X] ScenePipeline: implement scene cooking in
  `src/Oxygen/Content/Import/Async/Pipelines/ScenePipeline.cpp` (remove the
  `scene.pipeline.not_implemented` diagnostic, build full descriptor bytes,
  compute content hash on ThreadPool).
- [X] FbxImportJob: migrate to planner-driven flow (BuildPlan + ExecutePlan),
  wire textures/materials/scene streaming via planner sinks, and remove the
  legacy collection flow in
  `src/Oxygen/Content/Import/Async/Jobs/FbxImportJob.cpp`.

#### 6.8 MaterialReadinessTracker (Obsolete)

Planner dependency management supersedes `MaterialReadinessTracker`.
This section remains for historical context only.

#### 6.9 Configuration Flow (Need-to-Know)

**Files:**

- `src/Oxygen/Content/Import/Async/AsyncImportService.h/.cpp`
- `src/Oxygen/Content/Import/Async/ImportSession.h/.cpp`
- `src/Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h/.cpp`

Tasks:

- [X] Define how `AsyncImportService::Config` flows into jobs and pipelines.
- [X] Pass only the needed fields into constructors/PODs (avoid over-sharing).
- [ ] Update unit tests to cover config injection.

### Deliverables

- ✅ `TexturePipeline` as pure compute (concept-checked)
- ✅ Submit/Collect pattern for parallel cooking
- ⏳ GeometryPipeline, MaterialPipeline, ScenePipeline complete + tested
- ⏳ Pipeline conformance + cancellation test coverage
- ⏳ Job-driven import wiring (FBX/GLB/Texture jobs)
- ⏳ Config flow defined and injected (need-to-know)
- ✅ Progress reporting API defined

### Acceptance Criteria

```cpp
// Pipeline cooks, emitter emits
auto& pool = *session.ThreadPool();
TexturePipeline tex_pipe(pool, { /* config */ });
auto& tex_emit = session.TextureEmitter();

tex_pipe.Submit(work_item);
auto result = co_await tex_pipe.Collect();

if (result.success) {
  auto idx = tex_emit.Emit(std::move(result.cooked));
  // idx is stable NOW, I/O is async
}

// Full FBX import with parallel textures
AsyncImportService service;
auto report = ImportSync(service, "test_assets/sponza.fbx");
EXPECT_TRUE(report.success);
EXPECT_GT(report.textures_written, 10);
```

### Phase 6 Implementation Notes (Decisions + Open Items)

- **Callback threading**: callbacks run on the import thread; callers marshal to UI.
- **ImportSession owns infra access**: `IAsyncFileReader`, `IAsyncFileWriter`, and `co::ThreadPool` are injected into `ImportSession` via `observer_ptr` or owning smart pointers (no reference members).
- **Infra lifetime**: `ImportSession` does not own global infra by default; it observes infra owned by `AsyncImportService` unless tests inject owned instances.
- **Pipeline progress API**: define the `GetProgress()` shape and aggregation model before wiring progress in jobs.
- **Config flow**: pass only needed config fields to jobs/pipelines; prefer constructor injection over global access.
- **Tests**: update unit tests to use the new `ImportSession` constructor and validate config injection paths.

---

## Phase 7: Integration, Example & Polish (Week 8-9)

### Objective

End-to-end integration, example application, and documentation.

### Tasks

#### 7.1 End-to-End Integration Tests

**File:** `src/Oxygen/Content/Test/Import/Async/Integration/`

- [ ] Test: Simple FBX (cube with 1 texture)
- [ ] Test: Complex FBX (multiple meshes, materials, textures)
- [ ] Test: FBX with embedded textures
- [ ] Test: Standalone texture import (PNG, JPG, HDR)
- [ ] Test: Geometry + material + scene outputs (`.ogeo`, `.omat`, `.oscene`)
- [ ] Test: Re-import same asset (log-structured growth)
- [ ] Test: Cancellation mid-import
- [ ] Test: Multiple concurrent imports

#### 7.2 Verify Log-Structured Growth

**File:** Integration tests

- [ ] Import asset A
- [ ] Re-import asset A with different textures
- [ ] Verify new indices assigned
- [ ] Verify index file points to new data
- [ ] Verify old data is orphaned (not corrupted)

#### 7.3 Performance Benchmarks

**File:** `src/Oxygen/Content/Test/Import/Async/Benchmark/`

- [ ] Benchmark: 8-core texture cooking vs sequential
- [ ] Benchmark: Large FBX (28 textures, 5 meshes)
- [ ] Record baseline and verify speedup

#### 7.4 Example Application

**File:** `Examples/AsyncImport/`

- [ ] Create CMakeLists.txt
- [ ] Implement main.cpp with CLI args
- [ ] Display progress bar (ASCII)
- [ ] Handle Ctrl+C graceful shutdown
- [ ] Add README.md

#### 7.5 Documentation

**Files:** Various

- [ ] Update async_import_pipeline_v2.md with final API
- [ ] Document ImportSession usage
- [ ] Document Emitter pattern
- [ ] Document extending with new pipeline types
- [ ] Add code examples to design doc

#### 7.6 Cleanup

- [ ] Remove obsolete code paths
- [ ] Final code review
- [ ] ASAN/TSAN clean run
- [ ] Update CHANGELOG

#### 7.7 Legacy Cooker Gaps (Deferred from 6.6a)

- [ ] Implement 3D depth-slice assembly for `TextureSourceSet` in the async
  pipeline (parity with `ImportTexture3D`).
- [ ] Add validation tests for subresource ordering (layer-major, mip-inner).

#### 7.8 PAK Format Propagation (Post-Pipeline Changes)

**Files:**

- `src/Oxygen/Data/PakFormat.h`
- `tools/PakGen/`
- `tools/PakDump/`
- `tools/Inspector/`

Tasks:

- [ ] If PAK format or asset versions change (including a new v5), update
  `PakFormat.h` and any serializers/deserializers.
- [ ] Update PakGen to emit the new/changed descriptors and tables.
- [ ] Update PakDump to parse and display new/changed PAK vNext data.
- [ ] Update Inspector tooling to reflect new asset versions and fields.
- [ ] Add golden-file regression tests for PakGen and PakDump with new format.

### Deliverables

- Full integration test suite
- Performance benchmarks with recorded baseline
- Example application with progress UI
- Complete documentation
- Clean codebase

### Acceptance Criteria

```bash
# Example runs successfully
./async_import_example models/sponza.fbx cooked/

# Progress shown:
# [Textures] 15/28 [======----] 53%
# [Meshes]   3/5   [======----] 60%

# Ctrl+C causes graceful shutdown
# Speedup: 3-5x on 8-core vs sequential
```

---

## Key Design Principles

1. **Pipelines = Pure Compute**: No I/O in pipelines
2. **Emitters = Async I/O**: Return stable index immediately, queue I/O
3. **One Lazy Emitter Per Type Per Session**: TextureEmitter, BufferEmitter, AssetEmitter
4. **Log-Structured Growth**: Re-import grows data files, index file stays accurate
5. **ImportSession Owns Emitters**: Session.Finalize() waits for I/O, writes index

---

## Dependencies

### External

- Boost.Asio (already in workspace)
- No new external dependencies

### Internal

- OxCo library (ThreadPool, Channel, Nursery, LiveObject)
- Content/Import (existing sync importers)
- Data (PAK format, assets)

---

## Risk Mitigation

### Risk: Async Write Complexity

**Mitigation:** Use explicit-offset writes (`WriteAt*`) with a strict
non-overlapping-range contract. Emitter.Emit() performs stable index assignment
and range reservation synchronously, then queues async writes.

### Risk: Texture Dedup Parity with Sync Path

**Mitigation:** Extensive unit tests comparing sync vs async output byte-for-
byte. Use sync path as oracle.

### Risk: I/O Errors During Async Writes

**Mitigation:** Emitters track errors via atomic counter. Session.Finalize()
checks error count and includes diagnostics in report.

---

## Definition of Done

Each phase is complete when:

1. All tasks checked off
2. All unit tests passing
3. All integration tests passing
4. Code reviewed
5. Documentation updated
6. No memory leaks (ASAN clean)
7. No thread sanitizer warnings

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md) - Architecture
- [async_file_io.md](async_file_io.md) - File I/O design
