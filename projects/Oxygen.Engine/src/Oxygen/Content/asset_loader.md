# Asynchronous Asset Loading and GPU Upload System

## Overview

This system provides a modern, efficient, and scalable pipeline for loading, processing, and uploading assets (geometry, textures, etc.) in a C++ game engine. It leverages C++20 coroutines, the [Corral](https://github.com/hudson-trading/corral) coroutine library, and a ThreadPool for CPU-bound work, as well as a dedicated GPU copy engine for asynchronous GPU uploads.

## Features

- **Fully Asynchronous Asset Pipeline**: All stages (disk I/O, decoding, packing, buffer prep, GPU upload) are non-blocking and coroutine-driven.
- **ThreadPool Integration**: Heavy CPU work (file I/O, decoding, packing) is parallelized using a ThreadPool, maximizing CPU utilization.
- **Coroutine-based API**: Asset loading and upload are expressed as coroutines, providing a clean, readable async/await style.
- **GPU Copy Engine**: GPU uploads are batched and submitted via a dedicated copy queue/thread, allowing for efficient, non-blocking resource transfers.
- **Completion Notification**: Coroutines are resumed only when their work (including GPU upload) is fully complete.
- **Composable**: The system is modular and can be extended to support new asset types or processing steps.

## Architecture

### 1. ThreadPool

- A pool of worker threads for CPU-bound tasks.
- Used for file reading, asset decoding, buffer packing, and any heavy computation.
- Integrated with Corral via `co_threadpool` awaitable.

### 2. Coroutines (Corral)

- Each asset load is a coroutine that yields at each async step.
- Corral's `co_threadpool` and `co_background` are used to offload work to the ThreadPool.
- Coroutines can be chained for multi-stage processing.

### 3. GPU Upload Queue (Copy Engine)

- A thread-safe queue of upload requests.
- A dedicated thread or coroutine dequeues requests and submits them to the GPU using the copy engine (e.g., Direct3D12/Vulkan copy queue).
- Uses fences/events to notify coroutines when uploads are complete.

### 4. Asset Pipeline Flow

1. **Disk I/O**: Read asset file asynchronously on the ThreadPool.
2. **Decoding/Processing**: Decode and process asset data (e.g., decompress, parse, convert) on the ThreadPool.
3. **Buffer Preparation**: Pack data into GPU-friendly buffers on the ThreadPool.
4. **GPU Upload**: Submit buffer to the GPU copy engine; coroutine waits for upload completion.
5. **Completion**: Coroutine resumes and returns the ready-to-use GPU resource.

## Example Usage

```cpp
AssetKey mesh_key{/*guid*/..., /*version*/1, /*type*/AssetType::Mesh, /*variant*/0};
corral::task<GpuBuffer> load_and_upload_asset_async(ThreadPool& pool, GpuUploadQueue& uploadQueue, const AssetKey& key) {
    // Step 1: Async file read and decode
    AssetData asset = co_await co_threadpool(pool, [key] { return read_and_decode_asset(key); });

    // Step 2: Buffer preparation
    Buffer buffer = co_await co_threadpool(pool, [asset] { return pack_and_prepare_buffer(asset); });

    // Step 3: GPU upload
    co_await upload_to_gpu_async(uploadQueue, buffer);

    // Step 4: Return GPU handle
    co_return buffer.gpu_handle;
}
```

## Implementation Details

### ThreadPool Integration

- Use Corral's `co_threadpool(pool, lambda)` to offload any CPU-bound work.
- The coroutine will yield and resume when the work is done.

### GPU Upload Queue

- Implement a thread-safe queue for upload requests:
  - Each request contains source data, destination GPU buffer, size, and a Corral event for completion.
- A dedicated thread/coroutine dequeues requests and submits them to the GPU copy engine.
- After the GPU signals upload completion (via fence or callback), the event is set, resuming the waiting coroutine.

### Example: GPU Upload Awaitable

```cpp
corral::task<void> upload_to_gpu_async(GpuUploadQueue& queue, Buffer& buffer) {
    corral::event done;
    queue.enqueue({buffer.data, buffer.gpu_handle, buffer.size, done});
    co_await done; // Wait for upload completion
}
```

### Error Handling

- Each coroutine step can throw exceptions; errors propagate naturally through the coroutine chain.
- Use try/catch blocks within coroutines for custom error handling or fallback logic.

### Extensibility

- Add new asset types by implementing new coroutine chains.
- Add new processing steps by inserting additional `co_threadpool` or async steps.
- The system is agnostic to the underlying graphics API (Direct3D12, Vulkan, etc.) as long as the copy engine interface is respected.

## Detailed Component Specifications

### UploadQueue

The `UploadQueue` is responsible for managing and scheduling GPU upload operations, ensuring that data is transferred efficiently and asynchronously using the GPU copy engine. It provides:

- **Thread-safe enqueueing** of upload requests from any thread or coroutine.
- **Dedicated upload thread/coroutine** that dequeues requests and submits them to the GPU copy queue.
- **Completion notification** using `corral::event` or similar, so coroutines can `co_await` upload completion.
- **Batching** (optional): Multiple uploads can be batched for efficiency.
- **Fence or callback integration**: Ensures the upload is complete before signaling.

#### Example API

```cpp
struct UploadRequest {
    void* src;                // Pointer to CPU-side data
    GpuBuffer dst;            // Destination GPU buffer
    size_t size;              // Size in bytes
    size_t dstOffset;         // Offset in GPU buffer
    corral::event completion; // Event to signal when upload is done
};

class UploadQueue {
public:
    void enqueue(const UploadRequest& req);
    void run(); // Dedicated thread/coroutine for processing uploads
};
```

#### Example Usage in Coroutine

```cpp
corral::task<void> upload_to_gpu_async(UploadQueue& queue, void* src, GpuBuffer dst, size_t size, size_t dstOffset) {
    corral::event done;
    queue.enqueue({src, dst, size, dstOffset, done});
    co_await done;
}
```

### AssetLoader

The `AssetLoader` orchestrates the full asset pipeline, from disk I/O to GPU upload. It:

- **Splits assets into chunks** (for large assets, e.g., gLTF buffers or images)
- **Schedules disk reads and decoding** on the ThreadPool using `co_threadpool`
- **Prepares GPU buffers** (staging, layout, etc.)
- **Enqueues upload requests** to the `UploadQueue`
- **Handles dependencies** (e.g., textures, buffer views, mesh primitives)
- **Returns a handle or structure representing the loaded asset**

#### Example API

```cpp
class AssetLoader {
public:
    AssetLoader(ThreadPool& pool, UploadQueue& uploadQueue);
    corral::task<GltfModel> load_gltf_async(const AssetKey& key);
};
```

### Asset Chunking Example: Loading a gLTF Model

Suppose a gLTF model contains a large buffer (e.g., vertex data) and several images. The loader can process and upload these in chunks:

```cpp
corral::task<GpuBuffer> load_gltf_buffer_chunked(ThreadPool& pool, UploadQueue& uploadQueue, const std::string& bufferPath, size_t chunkSize) {
    size_t fileSize = get_file_size(bufferPath);
    GpuBuffer gpuBuffer = create_gpu_buffer(fileSize);

    for (size_t offset = 0; offset < fileSize; offset += chunkSize) {
        size_t thisChunk = std::min(chunkSize, fileSize - offset);
        // Read chunk from disk on ThreadPool
        std::vector<uint8_t> data = co_await co_threadpool(pool, [=] { return read_file_chunk(bufferPath, offset, thisChunk); });
        // Upload chunk to GPU
        co_await upload_to_gpu_async(uploadQueue, data.data(), gpuBuffer, thisChunk, offset);
    }
    co_return gpuBuffer;
}
```

#### Full AssetLoader Example (gLTF)

```cpp
corral::task<GltfModel> AssetLoader::load_gltf_async(const AssetKey& key) {
    // Parse glTF JSON and discover buffer/image URIs
    GltfManifest manifest = co_await co_threadpool(pool, [=] { return parse_gltf_manifest(key); });

    // Load all buffers in parallel (chunked)
    std::vector<corral::task<GpuBuffer>> bufferTasks;
    for (const auto& buffer : manifest.buffers) {
        bufferTasks.push_back(load_gltf_buffer_chunked(pool, uploadQueue, buffer.uri, 4 * 1024 * 1024)); // 4MB chunks
    }
    std::vector<GpuBuffer> gpuBuffers = co_await corral::when_all(bufferTasks);

    // Load images similarly (possibly chunked, or as a whole)
    // ...

    // Assemble the model
    GltfModel model = assemble_gltf_model(manifest, gpuBuffers /*, images, ...*/);
    co_return model;
}
```

---

## Benefits

- **Performance**: Maximizes CPU and GPU utilization, minimizes stalls.
- **Responsiveness**: Main/game thread is never blocked by asset loading or uploads.
- **Simplicity**: Coroutine-based code is easy to read, write, and maintain.
- **Scalability**: Handles many concurrent asset loads efficiently.

## Dependencies

- [Corral coroutine library](https://github.com/hudson-trading/corral)
- C++20 compiler
- ThreadPool implementation (can use std::thread or a third-party pool)
- Graphics API with copy engine support (Direct3D12, Vulkan, etc.)

## Example: Asset Pipeline Diagram

```
[Disk I/O] --(ThreadPool)--> [Decode/Process] --(ThreadPool)--> [Buffer Prep] --(ThreadPool)--> [GPU Upload] --(Copy Engine)--> [Ready]
```

# 🧩 Asset Management Subsystem Architecture

This design outlines a modular, asynchronous, and streaming-aware asset management system built around C++ coroutines and a thread pool. It emphasizes separation of concerns, pluggability, and GPU resource decoupling—aligned with best practices from modern game engines.

---

## 🧱 High-Level Architecture

```text
+---------------------+
|     Asset Manager   |  <-- Central API surface, dispatches coroutines
+---------------------+
         |
         v
+---------------------+       +---------------------+
|  Asset Type Loaders |<----->|   Asset Cache       |
| (Geometry, Shaders) |       +---------------------+
         |
         v
+---------------------+
| Asset I/O Scheduler |  <-- Async file I/O via coroutines/cooperative concurrency
+---------------------+
         |
         v
+---------------------+
|   Thread Pool       |  <-- CPU-bound processing: decode, convert, pack
+---------------------+
         |
         v
+---------------------+
|  GPU Upload Queue   |  <-- Async GPU upload, signals coroutine on completion
+---------------------+
```

**Note:**

- The ThreadPool is not used for file I/O, which is handled efficiently with coroutines and cooperative concurrency.
- The ThreadPool is justified by the need to perform CPU-intensive asset transformation (e.g., decoding, format conversion, buffer packing) and to prepare/upload data to the GPU without blocking the main engine thread.
- The GPU Upload Queue is decoupled from asset loading and transformation, ensuring that uploads are batched and submitted asynchronously.

---

## 🧠 Core Components

### 1. 🧩 Asset Manager

- Central entry point for loading, tracking, and unloading assets.
- Coroutine-based async API:

  ```cpp
  co_await assetManager.LoadAsync<ModelAsset>("knight.model");
  ```

- Responsibilities:
  - Route requests to appropriate loaders
  - Manage asset lifecycle and reference counting
  - Handle hot-reloading and versioning
  - Maintain registry of loaded assets and their states

---

### 2. 🔌 Asset Type Loaders

- One loader per asset type (e.g., geometry, texture, shader, material).
- Each loader:
  - Parses and deserializes its asset format
  - Produces CPU-side asset representations
  - Optionally schedules GPU upload tasks
- Registered via:

  ```cpp
  assetManager.RegisterLoader<GeometryAsset>(std::make_unique<GeometryLoader>());
  ```

---

### 3. 🔄 Asset I/O Scheduler

- Manages asynchronous file I/O and decompression using coroutines and a thread pool.
- Handles:
  - File reads
  - Format decoding (e.g., DDS, glTF, custom binary)
  - Dependency resolution (e.g., material → texture)
- Supports prioritization and cancellation for streaming

---

### 4. 🚀 GPU Upload Queue

- Dedicated queue for GPU resource creation (textures, buffers, etc.).
- Decoupled from asset loading to:
  - Avoid blocking I/O threads
  - Batch uploads efficiently
  - Respect residency budgets and upload bandwidth
- Integrated with the renderer’s frame graph or resource allocator

---

### 5. 🧠 Asset Cache and Deduplication

- Central cache maps asset keys to loaded instances:

  ```cpp
  std::unordered_map<AssetKey, std::shared_ptr<Asset>>
  ```

- Deduplication strategies:
  - File-level: same path or GUID → same asset
  - Resource-level: identical texture data → same GPU resource
- Cache supports:
  - Reference counting
  - LRU or usage-based eviction
  - Hot-reload invalidation

---

## 🧬 Asset Identification

### Asset Keys

- Use a structured key for flexibility and versioning:

  ```cpp
  struct AssetKey {
      uint64_t guid;     // Stable across builds
      uint32_t variant;  // Project-defined mask/flag (not interpreted by engine)
      uint8_t version;   // Project-defined version (up to 256 versions)
      uint8_t type;      // AssetType enum value (up to 256 types)
      uint16_t reserved; // Reserved for future use or alignment
  };
  static_assert(sizeof(AssetKey) == 16);
  ```

- The 'variant' field is a 32-bit project-defined mask/flag, not interpreted by the engine, and not used for LODs.
- LODs are always built-in to geometry assets.
- Geometry = one or more LODs (indexed 0..N-1), each LOD is a Mesh, each Mesh is one or more MeshViews (sub-meshes).

---

## 🔄 Lifecycle Flow

1. Client calls `LoadAsync("tree.model")`
2. AssetManager checks cache → miss
3. Dispatches to GeometryLoader
4. GeometryLoader parses file, resolves dependencies (e.g., materials)
5. Asset I/O Scheduler loads textures and metadata
6. GPU Upload Queue schedules texture and buffer uploads
7. Once complete, asset is cached and returned

---

## ✅ Design Principles

| Principle              | Implementation Strategy |
|------------------------|--------------------------|
| 🔗 Separation of Concerns | Distinct roles for I/O, parsing, GPU upload |
| 🔄 Asynchronous        | Coroutine-based API with thread pool execution |
| 🔌 Pluggability        | Registerable loaders per asset type |
| 🧠 Caching             | Centralized cache with deduplication |
| 🧱 Scalability         | Streaming-aware, priority-based queues |
| 🧍 Ownership           | AssetManager owns CPU-side assets; renderer owns GPU resources |
| 🔍 Observability       | Asset state tracking, hot-reload hooks, debug names |

---

## 🛠 Additional Considerations

- GPU residency tracking: integrate with a residency manager for eviction and prefetching.
- Streaming prioritization: use distance-to-camera or importance heuristics.
- Dependency graph: track asset dependencies for hot-reloading and unloading.
- Editor integration: expose asset states, references, and live reload feedback.

---
