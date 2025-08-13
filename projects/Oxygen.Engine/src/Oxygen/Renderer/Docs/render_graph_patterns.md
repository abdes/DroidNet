# RenderGraph Patterns

Covers current and prototype approaches for assembling passes as coroutines.

## Current Implementation

Plain coroutine that directly constructs pass objects, invokes
`PrepareResources` then `Execute`, and registers them via
`RenderContext::RegisterPass`.

Reasons this remains sufficient:

* Compile-time pass indexing already supplies cheap typed lookup.
* Control flow (loops, branches) uses native C++.
* No need yet for automatic resource lifetime derivation or topological sorting.

## Prototype: Metadata DSL (Exploratory)

The README contains an example mini-DSL using a `PassBuilder` awaitable to
record pass metadata (reads/writes). Status: not integrated with runtime code.
Use only for experimentation.

Potential future roles:

* Validation of declared resource dependencies.
* Automatic scheduling / reordering once more passes exist.
* Offline visualization or logging of graph structure.

## When to Introduce a Formal Graph

| Signal | Rationale |
|--------|-----------|
| > ~6–8 passes with conditional fan-out | Harder to reason about ordering manually |
| Need for transient resource aliasing | Requires lifetime analysis |
| Multiple frame-lagged producer/consumer chains | Hard to track manually |
| Cross-frame history passes (TAA, SSR, etc.) | Need dependency tagging |

## Anti-Goals (Now)

* Generic runtime DAG editing UI.
* Serialization of pass graphs.
* Shader reflection driven automatic binding.

Related: [render pass lifecycle](render_pass_lifecycle.md), [render context &
pass registry](render_graph.md).
