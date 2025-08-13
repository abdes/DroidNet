# Render Context & Pass Registry

Describes `RenderContext` structure and the fixed compile-time pass registry
(`KnownPassTypes`).

## Purpose

Provide per-frame shared data (frame counters, draw lists, pointer to
scene/material constant buffers) and O(1) typed access to previously executed
passes.

## Compile-Time Pass List

```text
using KnownPassTypes = PassTypeList<DepthPrePass, ShaderPass>;
constexpr std::size_t kNumPassTypes = KnownPassTypes::size;
```

Compile-time indexing ensures:

* Type safety (`static_assert` if missing).
* No string / RTTI hashing during frame execution.
* Contiguous storage: `std::array<RenderPass*, kNumPassTypes> known_passes`.

## Registration & Lookup

```text
template <typename PassT> PassT* GetPass();
template <typename PassT> void RegisterPass(PassT* pass);
```

Registration occurs inside each pass's `DoExecute` after successful GPU work so
downstream passes must tolerate a null pointer if a dependency was skipped
(future passes not yet added).

## Renderer Interaction

`Renderer::PreExecute` sets internal pointers (`renderer`, `render_controller`),
validates required buffers (scene constants), and leaves draw list spans
provided by caller intact. `PostExecute` clears pass registry and nulls
pointers.

## Extending Pass Types

Append new pass types at the end of `KnownPassTypes` to keep indices stable
(binary compatibility). Avoid reordering.

## Not in Context

Per-pass configuration objects, PSO descriptions, and intermediate resource
handles remain owned by pass instances rather than being stored globally.

Related: [render pass lifecycle](render_pass_lifecycle.md), [rendergraph
patterns](render_graph_patterns.md).
