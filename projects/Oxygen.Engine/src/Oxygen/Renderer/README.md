# Oxygen Renderer Design Overview

## Philosophy and Architecture

Oxygen's rendering system is built around a code-driven, algorithmic RenderGraph. Each pass is a function (callable object, lambda, or coroutine) that can be enabled, disabled, or replaced at runtime. Passes encapsulate both data and behavior, supporting conditional execution, debug/diagnostic passes, and dynamic pipeline construction. The Renderer orchestrates resource lifetime, caching, streaming, and threading, while the RenderGraph coordinates the flow of render items and resources between passes.

### Key Principles

- RenderGraph is not a generic data-driven DAG, but a code-driven pipeline.
- Passes are modular, composable, and coroutine-friendly.
- Renderer manages high-level resource orchestration; RenderGraph manages data flow.
- Modern C++ (lambdas, coroutines, std::function) is leveraged for flexibility.
- The pipeline is built in code, not in data.

This approach enables robust, scalable, and maintainable rendering, supporting advanced features and future extensibility without unnecessary complexity.

---

## Responsibilities Split

### ResourceRegistry

- Registers, replaces, and removes GPU resources (buffers, textures, etc.).
- Maintains strong references for resource lifetime management.
- Explicit eviction: resources are only removed when explicitly unregistered.
- Caches resource views (SRV/UAV) keyed by resource and view description.
- No automatic LRU or usage-based eviction (policy-free, low-level).

### RenderController

- Backend-specific factory for GPU resources and surfaces.
- Manages frame lifecycle and device/context.
- Does not manage high-level resource usage or scene logic.

### Renderer

- Backend-agnostic (uses only `graphics::common`).
- Holds a weak reference to its RenderController.
- Manages mapping from mesh assets to GPU resources (vertex/index buffers, etc.).
- Implements LRU or explicit eviction policy for mesh resources.
- Requests eviction from the ResourceRegistry of its RenderController as needed.
- Provides API to get buffer(s) for a mesh, abstracting resource management from the rest of the engine.
- In the future, will coordinate with AssetLoader/Manager for streaming and hot-reload.

---

## Lifetime and Ownership

- **ResourceRegistry**: Owns and tracks GPU resources for the lifetime of the registry (typically per RenderController).
- **RenderController**: Created for a specific surface/window; owns device/context and its ResourceRegistry.
- **Renderer**: Created by the application; references (but does not own) a RenderController; manages mesh-to-resource mapping and eviction policy.
- **Application**: Owns both Renderer and RenderController, and is responsible for their creation and destruction.

---

## Design Objectives

- Backend-agnostic and portable renderer.
- Explicit, robust, and scalable resource management.
- Policy-free ResourceRegistry; only tracks what is registered.
- Renderer is the right place for per-mesh resource management and eviction policy.
- Future extensibility for asset streaming and hot-reload.

---

## Best Practices

- Always check RenderController validity (weak_ptr lock) before issuing commands from Renderer.
- Keep resource management logic simple and explicit; start with manual or LRU eviction.
- Ensure thread safety for background streaming or multi-threaded rendering.
- Make buffer access and eviction APIs clear and robust.

---

## Renderer API Sketch

```cpp
class Renderer
{
public:
  //— Lazy fetch: if we haven't created GPU buffers for 'mesh' yet,
  //  we'll pull the MeshAsset from the AssetManager, create & register
  //  the vertex/index buffers, cache them, and return.
  //
  //  Throws or returns nullptr if mesh can't be found/loaded.
  std::shared_ptr<Buffer> GetVertexBuffer(const MeshAsset& mesh);
  std::shared_ptr<Buffer> GetIndexBuffer (const MeshAsset& mesh);

  //— Optional explicit unload if you know a mesh is retired
  // for the asset manager use for example
  void UnregisterMesh(const MeshAsset& mesh);

  //— Sweep out any mesh resources that haven't been used
  // Ask the policy which meshes to evict, then remove them
  void EvictUnusedMeshResources(size_t currentFrame);

  template <typename RenderGraphCoroutine>
  co::Co<> ExecuteRenderGraph(RenderGraphCoroutine&& graphCoroutine, RenderContext& ctx) {
      co_await std::forward<RenderGraphCoroutine>(graphCoroutine)(ctx);
  }

private:
  // internal map: MeshAssetID → GPU buffers
  std::unordered_map<MeshID, MeshGpuResources> mesh_resources_;

  // helper that does the on-demand create/register logic
  MeshGpuResources& EnsureMeshResources(const MeshAsset& mesh);
};

MeshGpuResources& Renderer::EnsureMeshResources(const MeshAsset& mesh)
{
  auto key = mesh.GetID();
  auto it = mesh_resources_.find(key);
  if (it != mesh_resources_.end())
    return it->second;

  // first time we see this mesh: load or request from AssetManager
  auto loadedMesh = AssetManager::Instance().GetMesh(mesh);
  if (!loadedMesh)
    throw std::runtime_error("Mesh not found: " + mesh.GetName());

  // create GPU buffers
  MeshGpuResources gpu;
  gpu.vertex_buffer = CreateVertexBuffer(loadedMesh->vertices());
  gpu.index_buffer  = CreateIndexBuffer (loadedMesh->indices());
  // (optionally build SRVs, allocate descriptors…)

  auto [ins, inserted] = mesh_resources_.emplace(key, std::move(gpu));
  return ins->second;
}

std::shared_ptr<Buffer> Renderer::GetVertexBuffer(const MeshAsset& mesh)
{
  return EnsureMeshResources(mesh).vertex_buffer;
}

std::shared_ptr<Buffer> Renderer::GetIndexBuffer(const MeshAsset& mesh)
{
  return EnsureMeshResources(mesh).index_buffer;
}
```

### Pluggable Eviction Policy

```cpp
// IEvictionPolicy.h
#pragma once
#include <vector>
#include <unordered_map>

// forward declarations
using MeshID = size_t;
struct MeshGpuResources;

class IEvictionPolicy
{
public:
  virtual ~IEvictionPolicy() = default;

  // Called whenever a mesh is fetched or drawn.
  virtual void   OnMeshAccess(MeshID id) = 0;

  // Called when the Renderer wants to evict stale entries.
  // Return the list of mesh IDs you want removed.
  virtual std::vector<MeshID>
      SelectResourcesToEvict(
        const std::unordered_map<MeshID, MeshGpuResources>& currentResources,
        size_t currentFrame) = 0;

  // Optional: called when a mesh is explicitly unregistered/unloaded
  virtual void   OnMeshRemoved(MeshID id) = 0;
};


void Renderer::EvictUnusedMeshResources(size_t currentFrame)
{
  auto toEvict = evictionPolicy_->SelectResourcesToEvict(mesh_resources_, currentFrame);
  for (auto id : toEvict)
  {
    mesh_resources_.erase(id);
    evictionPolicy_->OnMeshRemoved(id);
  }
}
```

#### Simple LRU Policy

```cpp
// LruEvictionPolicy.h
#pragma once
#include "IEvictionPolicy.h"
#include <unordered_map>

class LruEvictionPolicy : public IEvictionPolicy
{
public:
  LruEvictionPolicy(size_t maxAgeFrames)
    : maxAge(maxAgeFrames) {}

  void OnMeshAccess(MeshID id) override
  {
    lastUsed[id] = currentFrame;
  }

  std::vector<MeshID>
    SelectResourcesToEvict(
      const std::unordered_map<MeshID, MeshGpuResources>& currentResources,
      size_t frame) override
  {
    currentFrame = frame;
    std::vector<MeshID> evict;
    for (auto& [id, _] : currentResources)
    {
      auto it = lastUsed.find(id);
      if (it == lastUsed.end() || frame - it->second > maxAge)
        evict.push_back(id);
    }
    return evict;
  }

  void OnMeshRemoved(MeshID id) override
  {
    lastUsed.erase(id);
  }

private:
  size_t currentFrame = 0;
  size_t maxAge;
  std::unordered_map<MeshID, size_t> lastUsed;
};
```

---

## Mesh-to-Resources Map

```cpp
struct MeshGpuResources {
    std::shared_ptr<Buffer> vertex_buffer;
    std::shared_ptr<Buffer> index_buffer;
    // Optionally, descriptor indices, views, etc.
};

// Keyed by MeshAsset* (or a unique mesh identifier)
std::unordered_map<const MeshAsset*, MeshGpuResources> mesh_resource_map_;
```

- On first use, the Renderer creates and registers the GPU buffers for a mesh and stores them in the map.
- On eviction or mesh unload, the Renderer removes the entry and unregisters the resources from the ResourceRegistry.
- The map enables fast lookup for draw calls and resource management.

---

## RenderGraph Context and Data Flow

### Context Structure

Only data that is truly global to the entire render graph—engine-wide and application-wide data that is shared across passes—should be included in the render context. Backend resources and per-pass configuration are owned/configured by each pass, not by the render context.

#### Engine Data

- Frame constants (time, frame index, random seeds)
- Profiling/timing context

#### Application Data

- Camera parameters (view/projection matrices, camera position, frustum)
- Scene constants (lighting environment, fog, global exposure, etc.)
- Render item lists (opaque, transparent, decals, particles, etc.)
- Light lists (from Scene)
- Pass enable/disable flags

---

### Graphics Backend Resources Table

| Resource/Capability | Provider | Purpose | Lifetime | Access |
|--------------------|----------|---------|----------|--------|
| G-buffer textures (albedo, normals, depth, etc.) | `Texture` via `Framebuffer`/`ResourceRegistry` | Per-pixel scene data for deferred shading, post-processing, material evaluation | Lifetime of framebuffer or pass | `ResourceRegistry`, framebuffer APIs |
| Shadow maps | `Texture` via `ResourceRegistry` | Depth from light's perspective for shadow testing | Shadow pass lifetime | `ResourceRegistry` |
| Light lists/tiles buffers (Forward+) | `Buffer` via `ResourceRegistry` | Per-tile/clustered light indices for Forward+/clustered shading | Pass duration | `ResourceRegistry` |
| Scene constant buffers | `Buffer` via `ResourceRegistry` | Per-frame/scene constants for shaders | Frame/scene lifetime | `ResourceRegistry` |
| Pipeline state objects | `GraphicsPipelineDesc`, `ComputePipelineDesc` | GPU state for rendering/compute | Pipeline lifetime | Pipeline state APIs |
| Descriptor tables/root signatures | `DescriptorTableBinding`, `RootBindingDesc`, etc. | Bindless resource access | Pass/frame | `DescriptorAllocator`, `ResourceRegistry`, pipeline state APIs |
| Output render targets | `Framebuffer`, `Texture` via `RenderController` | Final rendered image | Rendering context lifetime | `RenderController`, framebuffer APIs |
| Resource registry/allocator handles | `ResourceRegistry`, `DescriptorAllocator` | Centralized GPU resource/descriptor management | Rendering context lifetime | `RenderController` |
| Device object creation | `Graphics` | Factory for persistent GPU resources/queues | App lifetime | `Graphics::Create*`, `GetCommandQueue`, etc. |
| Command queue/list pooling | `Graphics` | Pools for rendering/compute work | See above | See above |
| Per-frame resource management | `RenderController` | Per-frame resources, descriptor allocators, registry | See above | `RenderController` |
| Command recording, state transitions | `CommandRecorder` | Command list recording, state setup, transitions | Per frame/task | `CommandList`, `CommandQueue` |
| Command submission/batching | `RenderController` | Batching, frame sync, presentation | See above | `Graphics`, `CommandQueue` |
| Shader management | `Graphics` | Compiled/cached shaders by ID | See above | `GetShader` |

---

### Pass Input/Output Summary

**Outputs:**

- DepthPrePass: Depth buffer/texture, possibly early Z statistics
- LightCullingPass: Per-tile/clustered light lists, light grid/indices buffer
- OpaquePass: G-buffer outputs (if deferred), color buffer, normal buffer, material buffer
- TransparentPass: Accumulation buffer, transparency resolve buffer
- PostProcessPass: Final color buffer, tone-mapped output, bloom buffer, etc.
- DebugOverlayPass: Debug overlay texture, visualization buffer

**Inputs:**

- LightCullingPass: Needs depth buffer from DepthPrePass, camera/scene constants, light list
- OpaquePass: Needs light lists from LightCullingPass, depth buffer from DepthPrePass, camera/scene constants, render item list
- TransparentPass: Needs color/depth buffers from OpaquePass, camera/scene constants, transparent render item list
- PostProcessPass: Needs color buffer from OpaquePass or TransparentPass, possibly depth buffer, scene constants
- DebugOverlayPass: Needs final color buffer from PostProcessPass, debug/selection info from application context

---

## RenderGraph Example: Pass Pointer Mechanism

A RenderGraph in Oxygen is implemented as a coroutine, where each pass is created/configured, executed, and then registered in the context. The context acts as a registry of pass pointers, enabling explicit, type-safe, and modular pass-to-pass data flow.

```cpp
struct RenderContext {
    // Use oxygen type system, or define a compile-time list of all known pass
    // types, and use the index in that list.
    std::unordered_map<oxygen::TypeId, RenderPass*> pass_ptrs;

    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Framebuffer> framebuffer;
    std::shared_ptr<Buffer> scene_constants;
    std::vector<const RenderItem*> opaque_draw_list;
    std::vector<const RenderItem*> transparent_draw_list;
    std::vector<Light> light_list;
    std::shared_ptr<CommandRecorder> command_recorder;
    // ...other fields as needed...

    template <typename PassT>
    PassT* GetPass() const {
        auto it = pass_ptrs.find(typeid(PassT));
        return it != pass_ptrs.end() ? static_cast<PassT*>(it->second) : nullptr;
    }
    template <typename PassT>
    void RegisterPass(PassT* pass) {
        pass_ptrs[typeid(PassT)] = pass;
    }
};

co::Co<> ForwardPlusRenderGraph(RenderContext& ctx) {
    // Depth pre-pass: create/configure, execute, register
    auto depth_pre_pass_config = std::make_shared<DepthPrePassConfig>();
    depth_pre_pass_config->draw_list = ctx.opaque_draw_list;
    depth_pre_pass_config->depth_texture = ctx.framebuffer->GetDepthTexture();
    depth_pre_pass_config->framebuffer = ctx.framebuffer;
    depth_pre_pass_config->scene_constants = ctx.scene_constants;
    depth_pre_pass_config->debug_name = "DepthPrePass";
    auto depth_pre_pass = std::make_unique<DepthPrePass>(ctx.renderer, depth_pre_pass_config);
    co_await depth_pre_pass->PrepareResources(*ctx.command_recorder);
    co_await depth_pre_pass->Execute(*ctx.command_recorder);
    ctx.RegisterPass(depth_pre_pass.get());

    // Light culling pass: create/configure, execute, register
    auto light_culling_config = std::make_shared<LightCullingPassConfig>();
    light_culling_config->depth_buffer = depth_pre_pass_config->depth_texture;
    light_culling_config->scene_constants = ctx.scene_constants;
    light_culling_config->light_list = ctx.light_list;
    auto light_culling_pass = std::make_unique<LightCullingPass>(ctx.renderer, light_culling_config);
    co_await light_culling_pass->PrepareResources(*ctx.command_recorder);
    co_await light_culling_pass->Execute(*ctx.command_recorder);
    ctx.RegisterPass(light_culling_pass.get());

    // Opaque pass: create/configure, execute, register
    auto opaque_config = std::make_shared<OpaquePassConfig>();
    opaque_config->light_lists = light_culling_pass->GetLightLists();
    opaque_config->depth_buffer = depth_pre_pass_config->depth_texture;
    opaque_config->scene_constants = ctx.scene_constants;
    opaque_config->draw_list = ctx.opaque_draw_list;
    auto opaque_pass = std::make_unique<OpaquePass>(ctx.renderer, opaque_config);
    co_await opaque_pass->PrepareResources(*ctx.command_recorder);
    co_await opaque_pass->Execute(*ctx.command_recorder);
    ctx.RegisterPass(opaque_pass.get());
    // ...repeat for TransparentPass, PostProcessPass, DebugOverlayPass...
}

// In a pass:
void OpaquePass::Run(RenderContext& ctx) {
    auto* light_culling = ctx.GetPass<LightCullingPass>();
    if (light_culling) {
        // Access light_culling->GetLightList() or similar
    }
    // ...
}
```

**Pattern rationale:**

- Each pass is constructed/configured with its own config object, not via the global context.
- After execution, the pass pointer is registered in the context for downstream passes.
- Downstream passes retrieve previous passes via `ctx.GetPass<T>()` and access their outputs as needed.
- This pattern keeps the context clean and makes dependencies explicit.
- Minimal context pollution: Only pass pointers are stored, not all outputs.
- Explicit dependencies: Passes must request the previous pass they depend on, and will decide what to do if they were present/absent, successful or not.
- Type safety: Passes can expose only the necessary interface for consumers.
- Extensibility: New passes can be added without changing the context structure.

## Enhancement - Mini-DSL for pass metadata (Oxygen OxCo idiomatic, custom Awaitable)

```cpp
//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/OxCo/Co.h>
#include <string>
#include <vector>
#include <functional>
#include <iostream>

//----------------------------------------------------------------------------//
// 1) RenderContext (Oxygen style)
//----------------------------------------------------------------------------//
struct RenderContext {
    // camera, frame index, lists, etc.
};

//----------------------------------------------------------------------------//
// 2) Pass descriptor + builder DSL (Oxygen style, custom Awaitable)
//----------------------------------------------------------------------------//
struct PassDesc {
    std::string name;
    std::vector<std::string> reads, writes;
    std::function<void(RenderContext&)> fn;
};

struct PassGraph {
    std::vector<PassDesc> passes;
};

// Awaitable that records the pass into the graph when co_await'ed
class PassBuilder {
public:
    explicit PassBuilder(std::string n) { desc_.name = std::move(n); }
    PassBuilder& Reads(std::string r) {
        desc_.reads.push_back(std::move(r));
        return *this;
    }
    PassBuilder& Writes(std::string w) {
        desc_.writes.push_back(std::move(w));
        return *this;
    }
    PassBuilder& Execute(std::function<void(RenderContext&)> f) {
        desc_.fn = std::move(f);
        return *this;
    }
    PassDesc&& Build() { return std::move(desc_); }

    // Custom Awaiter for OxCo: records pass into PassGraph
    struct Awaiter {
        PassBuilder* builder;
        PassGraph* graph;
        bool await_ready() const noexcept { return true; }
        void await_suspend(std::coroutine_handle<>) const noexcept {}
        void await_resume() const noexcept {
            graph->passes.push_back(builder->Build());
        }
    };

    // Provide operator co_await for idiomatic OxCo usage
    Awaiter operator co_await(PassGraph& graph) && {
        return Awaiter{this, &graph};
    }
private:
    PassDesc desc_;
};

//----------------------------------------------------------------------------//
// 3) Render-graph as coroutine using PassBuilder (Oxygen style)
//----------------------------------------------------------------------------//
oxygen::co::Co<> MyForwardPlus(RenderContext& ctx, PassGraph& graph) {
    co_await std::move(PassBuilder("DepthPrePass"))
        .Reads("SceneConstants")
        .Writes("DepthTexture")
        .Execute([](RenderContext& ctx){
            std::cout << "  [DepthPrePass] building depth buffer\n";
            // ... Oxygen draw calls ...
        }).co_await(graph);

    co_await std::move(PassBuilder("LightCulling"))
        .Reads("DepthTexture")
        .Reads("LightList")
        .Writes("LightGrid")
        .Execute([](RenderContext& ctx){
            std::cout << "  [LightCulling] computing clustered lights\n";
            // ... Oxygen compute calls ...
        }).co_await(graph);

    co_await std::move(PassBuilder("OpaquePass"))
        .Reads("SceneConstants")
        .Reads("LightGrid")
        .Writes("GBuffer")
        .Execute([](RenderContext& ctx){
            std::cout << "  [OpaquePass] rasterizing opaque geometry\n";
            // ... Oxygen draw calls ...
        }).co_await(graph);

    co_return;
}

//----------------------------------------------------------------------------//
// 4) Driver: collect & execute (Oxygen style)
//----------------------------------------------------------------------------//
void RunGraph(RenderContext& ctx) {
    PassGraph graph;
    auto co = MyForwardPlus(ctx, graph);
    co.await_resume(); // run coroutine to completion
    for (auto& p : graph.passes) {
        std::cout << "[RunPass] " << p.name << "\n";
        p.fn(ctx);
    }
}
```

**Notes:**

- This version does not override or extend the OxCo Promise type.
- Pass metadata is recorded by a custom Awaiter (`PassBuilder::Awaiter`) that mutates the `PassGraph` when `co_await`ed.
- Usage: `co_await std::move(PassBuilder(...)).co_await(graph);` is idiomatic and safe for OxCo.
- All coroutine machinery (cancellation, exceptions, etc.) is handled by OxCo.
