Render Graph: Strategies & AliasLifetimeAnalysis

This folder contains render-graph related code for the AsyncEngine sample, including:

- Strategy interfaces: `IGraphOptimization`, `IAnalysisPass` and `DiagnosticsSink`.
- A default `SharedReadOnlyPromotionStrategy` that promotes per-view read-only resources to shared resources when safe.
- `AliasLifetimeAnalysis` — a small value-style wrapper around the integration-specific `ResourceAliasValidator` which exposes a compact API for registering resources/usages, running lifetime analysis, and collecting hazards and safe alias candidates.

Registering optimization strategies

The `RenderGraphBuilder` exposes a small registration API to allow injection of optimization strategies at graph build time. Strategies are owned by the builder and executed during the build pipeline.

- Add/register a strategy:
  - Call `RegisterOptimizationStrategy(std::make_unique<YourStrategy>(...))` on the `RenderGraphBuilder` instance before `Build()`.
  - The builder registers a default `SharedReadOnlyPromotionStrategy` in `BeginGraph()`; additional strategies can be appended or the default cleared with `ClearOptimizationStrategies()`.

Strategy contract (brief)

- `IGraphOptimization`:
  - Method: `void apply(BuildContext& ctx, DiagnosticsSink& diag)` — run optimization or analysis using the provided `BuildContext` (builder, render_graph, frame_context) and report warnings/errors via the `DiagnosticsSink`.
  - Strategies should be fast and safe to call in the build pipeline. Avoid long-running operations or heavy allocations where possible.

Diagnostics

- `DiagnosticsSink` is a lightweight interface that allows strategies to report `ValidationError` entries (errors or warnings). The builder wires a `ValidationDiagnosticsSink` into strategy execution so reported diagnostics are captured in the final `RenderGraph` validation result.

Using `AliasLifetimeAnalysis` from the builder (pattern)

The recommended pattern followed in `RenderGraphBuilder` is:

1. Create an `AliasLifetimeAnalysis` instance and initialize it with the `GraphicsLayerIntegration` from the current `FrameContext`:
   - `alias_analysis.Initialize(frame_context_->AcquireGraphics().get());`

2. Register resources while descriptors are still owned by the builder:
   - For each resource: `alias_analysis.AddResource(handle, *desc);`

3. Register usages (reads/writes) for passes transferred into the final `RenderGraph`:
   - `alias_analysis.AddUsage(resource_handle, pass_handle, state, is_write, view_index);`

4. After scheduling (once topological execution order is known), provide the order and run analysis:
   - `alias_analysis.SetTopologicalOrder(topo_map);`
   - `alias_analysis.AnalyzeLifetimes();`

5. Collect validation hazards and safe alias candidates:
   - `auto result = alias_analysis.ValidateAndCollect();`
   - Inspect `result.hazards` to add errors to the `RenderGraph` validation result.

Notes and rationale

- The `AliasLifetimeAnalysis` wrapper hides the integration-specific validator implementation and provides a testable, value-returning interface for phases and future strategy-driven analyses.
- Strategies should use `BuildContext` and `DiagnosticsSink` to access builder state and report issues without bringing heavy coupling into headers.

If you want, I can add a minimal unit test demonstrating `AliasLifetimeAnalysis` usage and a tiny example strategy implementation.
