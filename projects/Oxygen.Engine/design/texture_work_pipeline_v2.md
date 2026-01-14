# Texture Work Pipeline (v2)

**Status:** Approved Design
**Date:** 2026-01-14
**Parent:** [async_import_pipeline_v2.md](async_import_pipeline_v2.md)

---

## Overview

The Texture Work Pipeline parallelizes texture cooking within a single import
job. It is owned by `AsyncImporter` and runs inside the import thread's event
loop, using `co::ThreadPool` for CPU-bound work.

This document focuses on the internal texture parallelization strategy,
complementing the main async import architecture.

---

## Goals

1. **Parallel texture cooking**: Multiple textures cook concurrently within
   one import job.

2. **Block-level BC7 parallelism**: BC7 encoding uses ThreadPool for
   fine-grained parallelism.

3. **Backpressure**: Bounded work queue prevents memory exhaustion.

4. **Single-writer commit**: Emission state is mutated only by collector
   coroutine (no locks for dedup/write).

5. **Cooperative cancellation**: Job cancellation propagates to texture tasks.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Import Job Coroutine                                │
│                                                                             │
│  ┌─────────────────┐                           ┌─────────────────────────┐ │
│  │   Submitter     │                           │     Collector           │ │
│  │   Coroutine     │                           │     Coroutine           │ │
│  │                 │                           │                         │ │
│  │  For each tex:  │    TextureWorkResult      │  Receive results        │ │
│  │  Send request ──┼─────────────────────────▶ │  Commit to emission     │ │
│  │  to work queue  │    via results_channel    │  state (single-writer)  │ │
│  └────────┬────────┘                           └─────────────────────────┘ │
│           │                                                                 │
│           │ TextureWorkRequest                                              │
│           ▼                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────┐  │
│  │                    Bounded Work Queue                                 │  │
│  │                    co::Channel<TextureWorkRequest>                    │  │
│  └───────────────────────────────┬──────────────────────────────────────┘  │
│                                  │                                          │
│           ┌──────────────────────┼───────────────────────┐                  │
│           │                      │                       │                  │
│           ▼                      ▼                       ▼                  │
│  ┌─────────────────┐   ┌─────────────────┐     ┌─────────────────┐         │
│  │  Worker Task 0  │   │  Worker Task 1  │     │  Worker Task N  │         │
│  │                 │   │                 │ ... │                 │         │
│  │  Receive req    │   │  Receive req    │     │  Receive req    │         │
│  │  Read file      │   │  Read file      │     │  Read file      │         │
│  │  Cook texture   │   │  Cook texture   │     │  Cook texture   │         │
│  │  Send result    │   │  Send result    │     │  Send result    │         │
│  └────────┬────────┘   └────────┬────────┘     └────────┬────────┘         │
│           │                      │                       │                  │
│           │                      ▼                       │                  │
│           │            ┌─────────────────┐               │                  │
│           └───────────▶│   ThreadPool    │◀──────────────┘                  │
│                        │   (BC7 encode)  │                                  │
│                        └─────────────────┘                                  │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Data Types

### TextureWorkRequest

```cpp
struct TextureWorkRequest final {
  //! Correlation IDs for result routing.
  ImportJobId job_id = 0;
  uint32_t request_id = 0;

  //! Results channel writer (per-job, not global).
  co::Channel<TextureWorkResult>::Writer* results_writer = nullptr;

  //! Identity for deduplication (matches sync path).
  std::string texture_id;
  const void* source_key = nullptr;  // For index_by_file_texture lookup

  //! Source specification.
  std::filesystem::path source_file_path;  // File-backed texture
  std::span<const std::byte> embedded_bytes;  // Embedded texture
  std::shared_ptr<const void> embedded_owner;  // Keeps embedded data alive

  //! Debug information.
  std::string source_debug_id;
  bool is_embedded = false;

  //! Cooking configuration.
  emit::CookerConfig config{};
};
```

### TextureWorkResult

```cpp
enum class TextureWorkStatus : uint8_t {
  kSuccess = 0,
  kPlaceholder,  // Fallback texture used
  kCancelled,
  kFailed,
};

struct TextureWorkResult final {
  ImportJobId job_id = 0;
  uint32_t request_id = 0;

  std::string texture_id;
  const void* source_key = nullptr;

  TextureWorkStatus status = TextureWorkStatus::kFailed;

  //! On success: cooked data ready for commit.
  std::optional<emit::CookedEmissionResult> cooked;

  //! On failure: error description.
  std::string error_message;
};
```

---

## Worker Behavior

Each worker is a coroutine on the import event loop:

```cpp
auto TexturePipeline::WorkerLoop(uint32_t worker_index) -> co::Co<> {
  LOG_F(INFO, "Texture worker {} started", worker_index);

  while (auto request = co_await work_queue_.Receive()) {
    TextureWorkResult result{
      .job_id = request->job_id,
      .request_id = request->request_id,
      .texture_id = request->texture_id,
      .source_key = request->source_key,
    };

    try {
      // Step 1: Get source bytes (async file read or embedded)
      std::vector<std::byte> file_bytes;
      std::span<const std::byte> source_bytes;

      if (request->is_embedded) {
        source_bytes = request->embedded_bytes;
      } else {
        // Async file read
        auto read_result = co_await file_reader_->ReadFile(
          request->source_file_path);
        if (!read_result) {
          result.status = TextureWorkStatus::kFailed;
          result.error_message = read_result.error().message();
          co_await request->results_writer->Send(std::move(result));
          continue;
        }
        file_bytes = std::move(*read_result);
        source_bytes = file_bytes;
      }

      // Step 2: Cook texture (uses ThreadPool internally)
      auto desc = emit::MakeImportDescFromConfig(request->config);
      auto& policy = emit::GetPackingPolicy(request->config);

      auto cook_result = co_await CookTextureAsync(
        source_bytes, desc, policy, thread_pool_);

      if (!cook_result) {
        result.status = TextureWorkStatus::kFailed;
        result.error_message = to_string(cook_result.error());
        co_await request->results_writer->Send(std::move(result));
        continue;
      }

      // Step 3: Package for emission
      result.status = TextureWorkStatus::kSuccess;
      result.cooked = emit::ToCookedEmissionResult(
        request->texture_id, *cook_result);

    } catch (const std::exception& e) {
      result.status = TextureWorkStatus::kFailed;
      result.error_message = e.what();
    }

    co_await request->results_writer->Send(std::move(result));
  }

  LOG_F(INFO, "Texture worker {} exited", worker_index);
  co_return;
}
```

---

## Submitter + Collector Pattern

For each import job, two coroutines run in parallel:

### Submitter

Discovers textures and submits work requests:

```cpp
auto SubmitTextureWork(
  co::Nursery& nursery,
  const std::vector<TextureRef>& textures,
  TexturePipeline& pipeline,
  co::Channel<TextureWorkResult>& results_channel,
  ImportJobId job_id
) -> co::Co<uint32_t> {

  uint32_t expected_count = 0;
  auto& writer = results_channel.GetWriter();

  for (const auto& tex : textures) {
    TextureWorkRequest request{
      .job_id = job_id,
      .request_id = expected_count,
      .results_writer = &writer,
      .texture_id = tex.id,
      .source_key = tex.source_key,
      .source_file_path = tex.path,
      .is_embedded = tex.is_embedded,
      .embedded_bytes = tex.embedded_bytes,
      .embedded_owner = tex.embedded_owner,
      .config = tex.config,
    };

    // Send may suspend if queue is full (backpressure)
    bool sent = co_await pipeline.Enqueue(std::move(request));
    if (!sent) {
      // Pipeline closed; abort submission
      break;
    }
    ++expected_count;
  }

  co_return expected_count;
}
```

### Collector

Receives results and commits to emission state:

```cpp
auto CollectTextureResults(
  co::Channel<TextureWorkResult>& results_channel,
  emit::TextureEmissionState& state,
  uint32_t expected_count
) -> co::Co<std::vector<TextureCommitResult>> {

  std::vector<TextureCommitResult> commits;
  commits.reserve(expected_count);
  uint32_t received = 0;

  while (received < expected_count) {
    auto result = co_await results_channel.Receive();
    if (!result) break;  // Channel closed

    TextureCommitResult commit{
      .request_id = result->request_id,
      .texture_id = result->texture_id,
    };

    if (result->status == TextureWorkStatus::kSuccess && result->cooked) {
      // Single-writer commit (no mutex needed)
      commit.resource_index = emit::CommitTexture(state, *result->cooked);
      commit.success = true;
    } else {
      commit.success = false;
      commit.error = result->error_message;
      // Use fallback texture
      commit.resource_index = state.fallback_index;
    }

    commits.push_back(commit);
    ++received;
  }

  co_return commits;
}
```

### Coordination

```cpp
auto ProcessTexturesAsync(
  const std::vector<TextureRef>& textures,
  TexturePipeline& pipeline,
  emit::TextureEmissionState& state,
  ImportJobId job_id
) -> co::Co<std::vector<TextureCommitResult>> {

  // Create per-job results channel
  co::Channel<TextureWorkResult> results_channel;

  uint32_t expected_count = 0;
  std::vector<TextureCommitResult> commits;

  OXCO_WITH_NURSERY(nursery) {
    // Start submitter
    nursery.Start([&]() -> co::Co<> {
      expected_count = co_await SubmitTextureWork(
        nursery, textures, pipeline, results_channel, job_id);
      results_channel.Close();  // Signal collector we're done
      co_return;
    });

    // Start collector
    nursery.Start([&]() -> co::Co<> {
      commits = co_await CollectTextureResults(
        results_channel, state, expected_count);
      co_return;
    });

    co_return co::kJoin;
  };

  co_return commits;
}
```

---

## Cancellation

### Job Cancellation

When a job's cancel event triggers:

1. The job's nursery is cancelled
2. Submitter stops sending requests
3. Results channel is closed
4. Workers see closed channel, skip remaining work
5. Any in-flight ThreadPool tasks check CancelToken

### Pipeline Shutdown

When AsyncImporter stops:

1. Work queue is closed
2. All workers exit their receive loop
3. Worker tasks complete
4. Pipeline nursery closes

---

## Commit Behavior (Matching Sync Path)

The collector performs the same commit logic as the sync path:

```cpp
auto CommitTexture(
  emit::TextureEmissionState& state,
  const emit::CookedEmissionResult& cooked
) -> uint32_t {

  // 1. Ensure fallback exists
  emit::EnsureFallbackTexture(state);

  // 2. Fast reuse: check by source key
  if (auto* idx = state.index_by_file_texture.find(cooked.source_key)) {
    return *idx;
  }

  // 3. Fast reuse: check by texture ID
  if (auto* idx = state.index_by_texture_id.find(cooked.texture_id)) {
    return *idx;
  }

  // 4. Compute signature for dedup
  auto signature = util::MakeTextureSignatureFromStoredHash(cooked.desc);

  // 5. Dedup by signature
  if (auto* idx = state.index_by_signature.find(signature)) {
    // Reuse existing, update indices
    state.index_by_file_texture[cooked.source_key] = *idx;
    state.index_by_texture_id[cooked.texture_id] = *idx;
    return *idx;
  }

  // 6. New texture: append payload
  uint32_t data_offset = emit::AppendResource(state.appender, cooked.payload);

  // 7. Push descriptor to table
  uint32_t table_index = state.table.size();
  state.table.push_back(cooked.desc);
  state.table.back().data_offset = data_offset;

  // 8. Update all indices
  state.index_by_file_texture[cooked.source_key] = table_index;
  state.index_by_texture_id[cooked.texture_id] = table_index;
  state.index_by_signature[signature] = table_index;

  return table_index;
}
```

---

## Configuration

```cpp
struct TexturePipeline::Config {
  //! Number of worker coroutines draining the work queue.
  uint32_t worker_count = 2;

  //! Bounded capacity for work queue (backpressure).
  uint32_t work_queue_capacity = 64;
};
```

**Tuning Guidelines:**

- `worker_count`: Match to ThreadPool size or slightly less. Each worker
  can have one ThreadPool task in flight.

- `work_queue_capacity`: Higher = more requests buffered, more memory.
  Lower = more backpressure, less memory. 64 is a reasonable default.

---

## Progress Reporting

The collector reports progress as textures complete:

```cpp
auto CollectTextureResultsWithProgress(
  co::Channel<TextureWorkResult>& results_channel,
  emit::TextureEmissionState& state,
  uint32_t expected_count,
  ImportProgressCallback& progress_callback,
  ImportJobId job_id
) -> co::Co<std::vector<TextureCommitResult>> {

  std::vector<TextureCommitResult> commits;
  commits.reserve(expected_count);
  uint32_t received = 0;
  std::vector<ImportDiagnostic> pending_diagnostics;

  while (received < expected_count) {
    auto result = co_await results_channel.Receive();
    if (!result) break;

    TextureCommitResult commit = ProcessResult(*result, state);
    commits.push_back(commit);
    ++received;

    // Collect diagnostics for failed textures
    if (!commit.success) {
      pending_diagnostics.push_back({
        .severity = ImportSeverity::kWarning,
        .message = fmt::format("Texture {} failed: {}",
          result->texture_id, result->error_message),
      });
    }

    // Report incremental progress
    if (progress_callback) {
      progress_callback(ImportProgress{
        .job_id = job_id,
        .phase = ImportProgress::Phase::kTextures,
        .phase_progress = static_cast<float>(received) / expected_count,
        .overall_progress = ComputeOverall(Phase::kTextures, received, expected_count),
        .message = fmt::format("Processing texture {}/{}", received, expected_count),
        .items_completed = received,
        .items_total = expected_count,
        .new_diagnostics = std::move(pending_diagnostics),
      });
      pending_diagnostics.clear();
    }
  }

  co_return commits;
}
```

---

## Streaming Emission (Advanced)

For large scenes, materials can emit as soon as their textures are ready:

### Dependency Graph

```cpp
struct TextureToMaterialDeps {
  //! Maps texture index → materials waiting for it.
  std::unordered_map<TextureIndex, std::vector<MaterialIndex>> waiters;

  //! Maps material index → count of textures still pending.
  std::unordered_map<MaterialIndex, uint32_t> pending_counts;
};

auto BuildDependencyGraph(const SceneData& scene) -> TextureToMaterialDeps {
  TextureToMaterialDeps deps;

  for (size_t mat_idx = 0; mat_idx < scene.materials.size(); ++mat_idx) {
    const auto& mat = scene.materials[mat_idx];
    uint32_t count = 0;

    for (auto tex_idx : mat.texture_indices) {
      deps.waiters[tex_idx].push_back(mat_idx);
      ++count;
    }

    deps.pending_counts[mat_idx] = count;
  }

  return deps;
}
```

### Streaming Collector

```cpp
auto CollectWithStreamingEmission(
  co::Channel<TextureWorkResult>& results_channel,
  emit::TextureEmissionState& tex_state,
  emit::MaterialEmissionState& mat_state,
  const SceneData& scene,
  TextureToMaterialDeps& deps,
  uint32_t expected_count
) -> co::Co<void> {

  uint32_t received = 0;

  while (received < expected_count) {
    auto result = co_await results_channel.Receive();
    if (!result) break;

    // Commit texture
    uint32_t tex_table_idx = CommitTexture(tex_state, *result);
    ++received;

    // Update material dependencies
    if (auto it = deps.waiters.find(result->texture_id);
        it != deps.waiters.end()) {
      for (auto mat_idx : it->second) {
        auto& count = deps.pending_counts[mat_idx];
        --count;

        // If all textures ready, emit material now
        if (count == 0) {
          co_await EmitMaterial(scene.materials[mat_idx], mat_state);
        }
      }
    }
  }
}
```

### When to Use

| Scene Size | Strategy | Rationale |
|------------|----------|-----------|
| <20 textures | Barrier | Simpler, overhead negligible |
| 20-100 textures | Optional | Measure real benefit |
| >100 textures | Streaming | Clear throughput win |
| Progressive preview | Streaming | Required for UX |

---

## Integration Checklist

- [ ] TextureWorkRequest/Result types defined
- [ ] TexturePipeline LiveObject implemented
- [ ] Worker loop with async file read + CookTextureAsync
- [ ] Submitter/Collector pattern in FBX async path
- [ ] CommitTexture matching sync behavior
- [ ] Cancellation propagation tested
- [ ] Backpressure behavior tested

---

## See Also

- [async_import_pipeline_v2.md](async_import_pipeline_v2.md) - Main architecture
- [async_file_io.md](async_file_io.md) - File I/O abstraction
