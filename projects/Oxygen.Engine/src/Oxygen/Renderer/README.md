# Oxygen Renderer – Design Overview & Guide Map

This README is the entry point to the Renderer module design. It gives a terse
overview and links to focused documents. Each linked file owns a single concern;
cross‑references replace duplication.

## 1. Philosophy & High‑Level Architecture

Oxygen uses a code‑driven (not data-file authored) RenderGraph built from
coroutines. Each render pass is a small composable type whose lifecycle is:
validate → prepare resources (explicit transitions) → execute GPU work →
(optionally) register for downstream access. The graph itself is ordinary C++
control flow so conditional execution, feature toggles, debug passes, and hot
replacement are trivial.

Key principles:

* Code-built pipeline (no generic runtime DAG builder required now).
* Passes are coroutine friendly and modular.
* Renderer = mesh GPU resource manager + graph driver; RenderContext = per-frame
  shared data + pass registry.
* Explicit resource state management; bindless-friendly root layout.
* Minimal global context; passes own their configuration.

See: [philosophy & responsibilities](Docs/responsibilities.md), [render pass
lifecycle](Docs/render_pass_lifecycle.md).

## 2. Core Responsibilities (Quick Map)

| Concern | Owner | Detail |
|---------|-------|--------|
| GPU resource objects & views | ResourceRegistry | Allocation, view caching, no implicit eviction |
| Frame orchestration, descriptor allocators | RenderController | Backend specific, provides CommandRecorder(s) |
| Mesh → GPU buffers, eviction | Renderer | Lazy creation + LRU policy (pluggable) |
| Per-frame shared data | RenderContext | Frame constants, draw lists, scene/material constant buffers |
| Pass logic | RenderPass subclasses | DepthPrePass, ShaderPass (more later) |
| Pass registry (typed) | RenderContext | Fixed compile-time list (`KnownPassTypes`) |

Details: [responsibilities & ownership](Docs/responsibilities.md), [mesh
resource management](Docs/gpu_resource_management.md), [render context & pass
registry](Docs/render_graph.md).

## 3. Current Concrete Passes

Implemented today:

* DepthPrePass – depth-only population (+ required for later light / visibility
  stages).
* ShaderPass – color shading pass (forward style) using scene & (optional)
  material constants.

Design docs: [DepthPrePass](Docs/passes/depth_pre_pass.md),
[ShaderPass](Docs/passes/shader_pass.md). Both inherit the common [render pass
lifecycle](Docs/render_pass_lifecycle.md).

## 4. Data Units

* RenderItem: immutable per-frame snapshot of a renderable entity (mesh/material
  pointers, transforms, flags). See [Render Items](Docs/render_items.md).
* View abstraction (camera snapshot + render-specific state) – planned
  integration; see [View abstraction](Docs/view_abstraction.md).

## 5. RenderGraph Construction

Graphs are plain coroutines; pass registration uses a fixed type list
(`KnownPassTypes`) instead of the older `unordered_map<TypeId>` prototype shown
in early sketches. A lightweight DSL concept for pass metadata is exploratory
only. See [rendergraph patterns](Docs/render_graph_patterns.md).

## 6. Bindless & Root Signature Conventions

Root binding order (enum `RenderPass::RootBindings`): 0 – Bindless SRV table
(indices / structured buffers) 1 – Scene constants (CBV, b1 space0) 2 – Material
constants (CBV, b2 space0 – only present in passes that need it)

DepthPrePass currently uses only (0,1). ShaderPass uses (0,1,2). Details &
future extension guidance: [bindless conventions](Docs/bindless_conventions.md).

## 7. Data Flow & Pass IO Summary

Consolidated pass input/output expectations and evolution path: [data
flow](Docs/passes/data_flow.md).

## 8. Migration / Integration Tracking

Ongoing assimilation with Scene & Data systems tracked separately: [integration
& migration plan](Docs/render_integration_plan.md).

## 9. Mesh Resource Caching & Eviction

Lazy creation of vertex/index buffers + upload staging occurs inside
`Renderer::EnsureMeshResources`. A default LRU (frame age) policy is provided;
policies are pluggable. See [mesh resource
management](Docs/gpu_resource_management.md).

## 10. Where to Start

1. Read [responsibilities](Docs/responsibilities.md)
2. Inspect [render_pass_lifecycle](Docs/render_pass_lifecycle.md)
3. Review existing passes under `Docs/passes/`
4. Consult [render_items](Docs/render_items.md) for feeding draw lists
5. Extend with a new pass using the patterns in
   [render_graph_patterns](Docs/render_graph_patterns.md)

---

## Historical / Prototype Note

Earlier documentation used a dynamic `unordered_map<TypeId, RenderPass*>` inside
`RenderContext`. The current implementation replaces this with a compile‑time
`KnownPassTypes` array for O(1) indexed lookup and stronger type safety.
Prototype examples in older discussions should be interpreted accordingly.

---

## Appendix: Minimal Renderer API Sketch (Reference)

```cpp
class Renderer {
public:
  std::shared_ptr<Buffer> GetVertexBuffer(const MeshAsset& mesh);
  std::shared_ptr<Buffer> GetIndexBuffer (const MeshAsset& mesh);
  void UnregisterMesh(const MeshAsset& mesh);
  void EvictUnusedMeshResources(size_t currentFrame);
  template <typename RenderGraphCoroutine>
  co::Co<> ExecuteRenderGraph(RenderGraphCoroutine&& graphCoroutine, RenderContext& ctx) {
      co_await std::forward<RenderGraphCoroutine>(graphCoroutine)(ctx);
  }
private:
  std::unordered_map<MeshID, MeshGpuResources> mesh_resources_;
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

### Pluggable Eviction Policy (Excerpt)

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

#### Simple LRU Policy (Excerpt)

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

## Mesh-to-Resources Map (Excerpt)

```cpp
struct MeshGpuResources {
    std::shared_ptr<Buffer> vertex_buffer;
    std::shared_ptr<Buffer> index_buffer;
    // Optionally, descriptor indices, views, etc.
};

// Keyed by MeshAsset* (or a unique mesh identifier)
std::unordered_map<const MeshAsset*, MeshGpuResources> mesh_resource_map_;
```

* On first use, the Renderer creates and registers the GPU buffers for a mesh
  and stores them in the map.
* On eviction or mesh unload, the Renderer removes the entry and unregisters the
  resources from the ResourceRegistry.
* The map enables fast lookup for draw calls and resource management.

---

## RenderGraph Example: Pass Pointer Mechanism (Prototype Reference)

A RenderGraph in Oxygen is implemented as a coroutine, where each pass is
created/configured, executed, and then registered in the context. The context
acts as a registry of pass pointers, enabling explicit, type-safe, and modular
pass-to-pass data flow.

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

For the current implementation details and differences from this early
prototype, see [render context & pass
registry](Docs/render_graph.md) and [rendergraph
patterns](Docs/render_graph_patterns.md).

## Enhancement – Mini-DSL Prototype (Exploratory)

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

See [render graph patterns](Docs/render_graph_patterns.md) for context, status,
and guidance before adopting this pattern.
