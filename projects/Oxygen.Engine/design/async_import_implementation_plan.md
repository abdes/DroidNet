# Async Import Pipeline - Implementation Plan

**Status:** In Progress
**Date:** 2026-01-14
**Estimate:** 6-8 weeks (one engineer)
**Last Updated:** Phase 4 rewritten for ImportSession + Emitters

---

## Overview

This plan implements the async import pipeline in 6 phases:

| Phase | Name | Status | Key Deliverables |
|-------|------|--------|------------------|
| 1 | Foundation | ✅ COMPLETE | EventLoop, ThreadPool, ThreadNotification |
| 2 | Async File I/O | ✅ COMPLETE | IAsyncFileReader, WindowsFileReader |
| 3 | AsyncImportService | ✅ COMPLETE | Thread-safe API, job lifecycle |
| 4 | ImportSession + Emitters | ⏳ IN PROGRESS | Async writes, emitters, stable indices (lazy session-owned emitters pending) |
| 5 | TexturePipeline | ❌ NOT STARTED | Pure compute pipeline, FbxImporter::ImportAsync |
| 6 | Integration & Polish | ❌ NOT STARTED | End-to-end tests, example, docs |

**Unit test coverage (high-level):** WindowsFileWriter, ImportSession, TextureEmitter, BufferEmitter, AssetEmitter. (Run `ctest` for current totals.)

---

## ⚠️ CRITICAL: Lessons Learned ⚠️

These lessons were learned through painful debugging. **DO NOT IGNORE.**

### 1. NEVER Reinvent Types That Already Exist

The `oxygen::content::import` namespace ALREADY contains:

| Type | Header | Key Members |
|------|--------|-------------|
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
  if (shutdown_.exchange(true)) return;  // BUG: Returns if RequestShutdown was called!
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

4. **Alignment is critical for GPU uploads:**
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
```

---

## Phase 1: Foundation (Week 1) ✅ COMPLETE

### Objective

Establish the async infrastructure: event loop, ThreadNotification, and
basic threading.

### Tasks

#### 1.1 Import Event Loop

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h/.cpp`

- [x] Create `ImportEventLoop` class wrapping ASIO io_context
- [x] Implement `Run()`, `Stop()`, `Post()` methods
- [x] Add work guard to prevent premature exit
- [x] Unit test: event loop starts, posts callback, stops

#### 1.2 EventLoopTraits Specialization

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h`

- [x] Specialize `co::EventLoopTraits<ImportEventLoop>`
- [x] Implement `EventLoopId()`, `Run()`, `Stop()`, `IsRunning()`
- [x] Unit test: traits work with `co::Run()`

#### 1.3 ThreadNotification Specialization

**File:** `src/Oxygen/Content/Import/Async/ImportEventLoop.h`

- [x] Specialize `co::ThreadNotification<ImportEventLoop>`
- [x] Implement using `asio::post()` or equivalent
- [x] Unit test: notification posts from worker thread to event loop

#### 1.4 ThreadPool Integration

**File:** Unit test only

- [x] Construct `co::ThreadPool` with `ImportEventLoop`
- [x] Verify `ThreadPool::Run()` works correctly
- [x] Verify results return via ThreadNotification
- [x] Unit test: CPU-bound work offloads and returns

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

- [x] Define `FileError` enum
- [x] Define `FileErrorInfo` struct with `ToString()`, `IsError()`
- [x] Implement Windows error mapping via `MapSystemError()`
- [x] Implement POSIX error mapping via `MapSystemError()`
- [x] Unit test: error mapping coverage (21 tests)

#### 2.2 IAsyncFileReader Interface

**File:** `src/Oxygen/Content/Import/Async/IAsyncFileReader.h`

- [x] Define `ReadOptions` struct (offset, max_bytes, size_hint, alignment)
- [x] Define `FileInfo` struct (size, last_modified, is_directory, is_symlink)
- [x] Define `IAsyncFileReader` interface with Co<Result<T>> returns
- [x] Document thread safety and cancellation

#### 2.3 WindowsFileReader Implementation

**File:** `src/Oxygen/Content/Import/Async/WindowsFileReader.h/.cpp`

- [x] Implement `ReadFile()` using ASIO random_access_handle + IOCP
- [x] Implement `GetFileInfo()` via std::filesystem
- [x] Implement `Exists()` via std::filesystem
- [x] Unit test: read existing file (small and large)
- [x] Unit test: read non-existent file (error)
- [x] Unit test: read with offset and max_bytes
- [x] Unit test: GetFileInfo for file and directory
- [x] Unit test: Exists for file, directory, and non-existent
- [x] 14 WindowsFileReader tests passing

#### 2.4 Factory Function

**File:** `src/Oxygen/Content/Import/Async/WindowsFileReader.h`

- [x] Implement `CreateAsyncFileReader(ImportEventLoop&)`
- [x] Currently returns WindowsFileReader on Windows
- [x] Document future Linux (io_uring) implementation

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

- [x] Define `ImportJobId` type (uint64_t)
- [x] Define `ImportCompletionCallback` type (uses existing `ImportReport`)
- [x] Define `ImportProgressCallback` type (uses existing `ImportDiagnostic`)
- [x] Define `ImportCancellationCallback` type
- [x] Define `ImportProgress` struct
- [x] Define `Config` struct

**CRITICAL:** Reuses existing types from `Import/` module:
- `ImportRequest` from `<Oxygen/Content/Import/ImportRequest.h>`
- `ImportReport` from `<Oxygen/Content/Import/ImportReport.h>`
- `ImportDiagnostic` from `<Oxygen/Content/Import/ImportDiagnostics.h>`

#### 3.2 Import Thread Lifecycle

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [x] Spawn import thread in constructor
- [x] Create ImportEventLoop on import thread
- [x] Create IAsyncFileReader on import thread (via `CreateAsyncFileReader`)
- [x] Implement graceful shutdown in destructor
- [x] Use `std::latch` for startup synchronization
- [x] Use separate `shutdown_complete_` flag for shutdown tracking
- [x] Unit test: construct and destruct without crash (4 tests)
- [x] Unit test: multiple construct/destruct cycles

#### 3.3 Thread-Safe Job Submission

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [x] Implement thread-safe request queue (mutex + `std::queue`)
- [x] Implement atomic job ID generation
- [x] Implement `SubmitImport()` posting to import thread via `event_loop_->Post()`
- [x] Unit test: submit from main thread (5 tests)
- [x] Unit test: submit from multiple threads concurrently (1 test)

#### 3.4 Result Callback Dispatch

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [x] Callbacks invoked on import thread (simplified for shell)
- [x] Unit test: callback invoked with correct job ID

#### 3.5 Cancellation API

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [x] Implement `CancelJob()` - returns false for invalid/completed jobs
- [x] Implement `CancelAll()` - cancels pending and marks active for cancel
- [x] Implement `RequestShutdown()` - non-blocking, sets flag
- [x] Unit test: cancel pending job (3 tests)
- [x] Unit test: shutdown tests (2 tests)

#### 3.6 Query API

**File:** `src/Oxygen/Content/Import/Async/AsyncImportService.cpp`

- [x] Implement `IsAcceptingJobs()`
- [x] Implement `IsJobActive()`
- [x] Implement `PendingJobCount()`
- [x] Implement `InFlightJobCount()`
- [x] Unit tests (2 tests)

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
- **AsyncImporter**: Shared compute infrastructure (pipelines, ThreadPool)
- **ImportSession**: Per-job state (emitters, diagnostics, LooseCookedWriter)
- **Emitters**: Async I/O writers returning stable indices immediately

### Tasks

#### 4.1 IAsyncFileWriter Interface

**File:** `src/Oxygen/Content/Import/Async/IAsyncFileWriter.h`

- [x] Define `WriteOptions` struct (alignment, create_directories, overwrite, share_write)
- [x] Define `WriteCompletionCallback` type
- [x] Define `IAsyncFileWriter` interface
- [x] `Write(path, data, options)` - coroutine returning bytes written
- [x] `WriteAsync(path, data, options, callback)` - fire-and-forget with callback
- [x] `WriteAt(path, offset, data, options)` - coroutine returning bytes written
- [x] `WriteAtAsync(path, offset, data, options, callback)` - fire-and-forget with callback
- [x] `Flush()` - wait for all pending operations
- [x] `CancelAll()` - cancel pending operations
- [x] `PendingCount()` - return number of pending operations
- [x] Document thread safety

#### 4.2 WindowsFileWriter Implementation

**File:** `src/Oxygen/Content/Import/Async/WindowsFileWriter.h/.cpp`

- [x] Implement using ASIO + IOCP for async writes
- [x] Track pending write count via atomic
- [x] Use `co::SleepFor` for yielding (consistent with OxCo patterns)
- [x] Unit test: write small file (3 tests)
- [x] Unit test: write large file (1 test)
- [x] Unit test: explicit-offset write operations via `WriteAt*`
- [x] Unit test: async `WriteAsync`/`WriteAtAsync` with callbacks
- [x] Unit test: flush waits for pending (2 tests)
- [x] Unit test: cancellation (2 tests)
- [x] Unit test: error handling (4 tests)
- [x] **20 unit tests**

#### 4.3 ImportSession Class

**File:** `src/Oxygen/Content/Import/Async/ImportSession.h/.cpp`

- [x] Define `ImportSession` class
- [x] Constructor takes `ImportRequest`, `IAsyncFileWriter&`
- [x] Own `LooseCookedWriter` for index file
- [x] Thread-safe diagnostics collection with `AddDiagnostic()`
- [x] `HasErrors()` to check for error-level diagnostics
- [x] `Finalize()` coroutine that waits for I/O and writes index
- [x] Unit tests: 14 tests covering construction, diagnostics, finalization

#### 4.4 TextureEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/TextureEmitter.h/.cpp`

- [x] Implement `Emit(CookedTexturePayload) -> uint32_t`
  - Assign stable index immediately (texture index 0 reserved for fallback)
  - Reserve a unique aligned range in `textures.data` (atomic CAS on size)
  - Write optional zero-padding and payload using `WriteAtAsync()`
    (requires `WriteOptions{.share_write=true}`)
  - Add `TextureResourceDesc` entry to in-memory table
- [x] Implement `Count()`, `PendingCount()`, `ErrorCount()`, `DataFileSize()`
- [x] Implement `Finalize() -> co::Co<bool>`
  - Wait for pending writes via `Flush()`
  - Serialize table via `serio::Writer` with packed alignment
  - Write `textures.table` file
- [x] Unit tests: 14 tests covering emission, alignment, finalization, content verification

#### 4.5 BufferEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/BufferEmitter.h/.cpp`

- [x] Same pattern as TextureEmitter for geometry/animation buffers
  - Per-buffer alignment (vertex=16, index=4, etc.)
  - CAS loop for atomic aligned offset reservation
  - Padding writes between buffers
- [x] Emit returns stable index immediately
- [x] Finalize waits for I/O and writes `buffers.table`
- [x] Unit tests: 16 tests covering PAK format compliance
  - Table file packed size verification (32 bytes per entry)
  - Aligned offset verification in table entries
  - Data file content verification with padding
  - Metadata preservation (usage_flags, element_stride, content_hash)

#### 4.6 AssetEmitter ✅ COMPLETE

**File:** `src/Oxygen/Content/Import/Async/Emitters/AssetEmitter.h/.cpp`

- [x] Implement `Emit(AssetKey, AssetType, virtual_path, descriptor_relpath, bytes)`
- [x] Write `*.omat`, `*.ogeo`, `*.oscene` via async I/O (`WriteAsync`)
- [x] Track pending writes and I/O errors
- [x] Implement `Finalize() -> co::Co<bool>` (waits for pending I/O)
- [x] Unit tests for path validation, emission, and finalization

#### 4.7 ImportSession Lazy Emitters

**File:** `src/Oxygen/Content/Import/Async/ImportSession.cpp`

- [ ] Implement `TextureEmitter() -> TextureEmitter&` (lazy create)
- [ ] Implement `BufferEmitter() -> BufferEmitter&` (lazy create)
- [ ] Implement `AssetEmitter() -> AssetEmitter&` (lazy create)
- [ ] Unit test: lazy creation on first access

#### 4.8 ImportSession Finalization

**File:** `src/Oxygen/Content/Import/Async/ImportSession.cpp`

- [ ] Extend `Finalize()` to orchestrate emitters (if created)
  - Await each emitter's `Finalize()`
  - Only then write `container.index.bin` (LAST)
- [ ] Integration test: session + emitters finalize correctly

#### 4.9 Wire ImportSession to AsyncImporter

**File:** `src/Oxygen/Content/Import/Async/detail/AsyncImporter.cpp`

- [ ] Create `ImportSession` per job
- [ ] Pass session to `Importer::ImportAsync()`
- [ ] Call `session.Finalize()` after ImportAsync returns
- [ ] Integration test: job creates session, finalizes

### Deliverables

- ✅ `IAsyncFileWriter` abstraction with Windows implementation
- ✅ `TextureEmitter`, `BufferEmitter`, `AssetEmitter`
- ✅ Stable index assignment with async I/O (explicit-offset `WriteAt*`)
- ⏳ `ImportSession` with lazy emitters (not implemented yet)
- ⏳ Session finalization that awaits emitters then writes index last
- ⏳ Wiring ImportSession into AsyncImporter job execution

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

## Phase 5: TexturePipeline (Pure Compute) (Week 5-6)

### Objective

Implement TexturePipeline as pure compute (no I/O). Pipeline cooks textures
on ThreadPool, returns cooked data to caller who emits via Emitter.

### Key Design Points

- **Pipeline = Pure Compute**: Load → Decode → Transcode (BC7, mips)
- **Pipeline does NOT emit**: Returns `CookedTexture` to caller
- **Caller emits**: `tex_emitter.Emit(cooked)` → index + async I/O
- **Concept-checked**: `static_assert(ResourcePipeline<TexturePipeline>)`

### Tasks

#### 5.1 TexturePipeline Types

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.h`

- [ ] Define `TexturePipeline::WorkItem`
  - `source_id` (correlation key)
  - `source` (file path OR embedded bytes)
  - `desc` (TextureImportDesc)
- [ ] Define `TexturePipeline::WorkResult`
  - `source_id` (echoed)
  - `cooked` (CookedTexture payload)
  - `diagnostics` (vector)
  - `success` (bool)
- [ ] Define `PipelineProgress` struct

#### 5.2 ResourcePipeline Concept

**File:** `src/Oxygen/Content/Import/Async/ResourcePipeline.h`

- [ ] Define `ResourcePipeline<T>` concept
  - `T::WorkItem`, `T::WorkResult`
  - `Submit(WorkItem)`, `Collect() -> co::Co<WorkResult>`
  - `HasPending()`, `PendingCount()`
  - `GetProgress()`, `CancelAll()`
- [ ] `static_assert(ResourcePipeline<TexturePipeline>)`

#### 5.3 TexturePipeline Implementation

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.h/.cpp`

- [ ] Bounded work queue (`co::Channel<WorkItem>`)
- [ ] Result queue (`co::Channel<WorkResult>`)
- [ ] Implement `Submit(WorkItem)` - non-blocking enqueue
- [ ] Implement `Collect() -> co::Co<WorkResult>` - await result
- [ ] Implement `HasPending()`, `PendingCount()`
- [ ] Implement `GetProgress()`, `CancelAll()`
- [ ] Unit test: submit and collect

#### 5.4 TexturePipeline Worker Loop

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.cpp`

- [ ] Worker coroutine reads from work queue
- [ ] Load texture (file via IAsyncFileReader, or embedded bytes)
- [ ] Decode image (ThreadPool CPU work)
- [ ] Transcode (mips, BC7 on ThreadPool)
- [ ] Push WorkResult to result queue
- [ ] Unit test: texture cooks correctly

#### 5.5 Reuse Existing Sync Cooking Logic

**File:** `src/Oxygen/Content/Import/Async/TexturePipeline.cpp`

- [ ] Call existing `CookTexture()` on ThreadPool
- [ ] Wrap sync helpers, don't rewrite
- [ ] Unit test: output matches sync path

#### 5.6 AsyncImporter Owns Pipeline

**File:** `src/Oxygen/Content/Import/Async/detail/AsyncImporter.h/.cpp`

- [ ] Add `TexturePipeline texture_pipeline_` member
- [ ] Expose via `Textures() -> TexturePipeline&`
- [ ] Initialize in constructor with ThreadPool reference
- [ ] Unit test: pipeline accessible

#### 5.7 Enhanced Importer Interface

**File:** `src/Oxygen/Content/Import/Importer.h`

- [ ] Add `ImportAsync(request, session, importer) -> co::Co<void>`
- [ ] Default: offload sync `Import()` to ThreadPool
- [ ] Document override pattern

#### 5.8 FbxImporter::ImportAsync

**File:** `src/Oxygen/Content/Import/fbx/FbxImporter.cpp`

- [ ] Override `ImportAsync()`
- [ ] Phase 1: Parse FBX on ThreadPool
- [ ] Phase 2: Submit all textures to pipeline
- [ ] Phase 3: Concurrent streams with `co::Nursery`
  - Stream A: Collect textures → emit → check material readiness
  - Stream B: Process meshes on ThreadPool → emit
  - Stream C: Bake animations on ThreadPool → emit
- [ ] Phase 4: `nursery.Join()`
- [ ] Phase 5: Emit scene descriptor
- [ ] Integration test: FBX with textures, materials, meshes

#### 5.9 MaterialReadinessTracker

**File:** `src/Oxygen/Content/Import/Async/MaterialReadinessTracker.h/.cpp`

- [ ] Build dependency graph: material → required texture source_ids
- [ ] Implement `MarkTextureReady(source_id) -> vector<size_t>`
- [ ] Returns material indices that are now ready to emit
- [ ] Unit test: dependency tracking

### Deliverables

- ✅ `TexturePipeline` as pure compute (concept-checked)
- ✅ Submit/Collect pattern for parallel cooking
- ✅ `FbxImporter::ImportAsync()` with streaming emission
- ✅ `MaterialReadinessTracker` for dependency resolution
- ✅ Full parallelism on 8+ core systems

### Acceptance Criteria

```cpp
// Pipeline cooks, emitter emits
auto& tex_pipe = importer.Textures();
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

---

## Phase 6: Integration, Example & Polish (Week 7-8)

### Objective

End-to-end integration, example application, and documentation.

### Tasks

#### 6.1 End-to-End Integration Tests

**File:** `src/Oxygen/Content/Test/Import/Async/Integration/`

- [ ] Test: Simple FBX (cube with 1 texture)
- [ ] Test: Complex FBX (multiple meshes, materials, textures)
- [ ] Test: FBX with embedded textures
- [ ] Test: Standalone texture import (PNG, JPG, HDR)
- [ ] Test: Re-import same asset (log-structured growth)
- [ ] Test: Cancellation mid-import
- [ ] Test: Multiple concurrent imports

#### 6.2 Verify Log-Structured Growth

**File:** Integration tests

- [ ] Import asset A
- [ ] Re-import asset A with different textures
- [ ] Verify new indices assigned
- [ ] Verify index file points to new data
- [ ] Verify old data is orphaned (not corrupted)

#### 6.3 Performance Benchmarks

**File:** `src/Oxygen/Content/Test/Import/Async/Benchmark/`

- [ ] Benchmark: 8-core texture cooking vs sequential
- [ ] Benchmark: Large FBX (28 textures, 5 meshes)
- [ ] Record baseline and verify speedup

#### 6.4 Example Application

**File:** `Examples/AsyncImport/`

- [ ] Create CMakeLists.txt
- [ ] Implement main.cpp with CLI args
- [ ] Display progress bar (ASCII)
- [ ] Handle Ctrl+C graceful shutdown
- [ ] Add README.md

#### 6.5 Documentation

**Files:** Various

- [ ] Update async_import_pipeline_v2.md with final API
- [ ] Document ImportSession usage
- [ ] Document Emitter pattern
- [ ] Document extending with new pipeline types
- [ ] Add code examples to design doc

#### 6.6 Cleanup

- [ ] Remove obsolete code paths
- [ ] Final code review
- [ ] ASAN/TSAN clean run
- [ ] Update CHANGELOG

### Deliverables

- ✅ Full integration test suite
- ✅ Performance benchmarks with recorded baseline
- ✅ Example application with progress UI
- ✅ Complete documentation
- ✅ Clean codebase

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

## Testing Summary by Phase

Test totals fluctuate as work lands. Prefer using `ctest` (or CI) for the
authoritative count.

- Phase 1-3: Unit tests exist and are tracked as ✅ complete phases.
- Phase 4: Unit tests exist for the writer + session + emitters; integration
  tests for session+importer wiring are still pending.

---

## Architecture Summary

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ AsyncImportService (thread-safe public API)                                 │
│   SubmitImport() → ImportJobId                                              │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│ Import Thread (ASIO event loop)                                             │
│                                                                             │
│  AsyncImporter (shared compute infrastructure)                              │
│    ├── TexturePipeline (pure compute, Submit/Collect)                       │
│    ├── ThreadPool (8+ workers)                                              │
│    └── IAsyncFileIO (read + write)                                          │
│                                                                             │
│  ImportSession (per-job state)                                              │
│    ├── TextureEmitter → textures.data, textures.table                       │
│    ├── BufferEmitter  → buffers.data, buffers.table                         │
│    ├── AssetEmitter   → *.omat, *.ogeo, *.oscene                            │
│    ├── DiagnosticsBag                                                       │
│    └── LooseCookedWriter → container.index.bin (LAST)                       │
└─────────────────────────────────────────────────────────────────────────────┘
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
