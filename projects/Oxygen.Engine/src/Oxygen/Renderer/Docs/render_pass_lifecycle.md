# Render Pass Lifecycle

Applies to all subclasses of `RenderPass` (e.g. `DepthPrePass`, `ShaderPass`).

```text
ValidateConfig() -> (maybe) CreatePipelineStateDesc() -> DoPrepareResources() -> DoExecute()
```

## Stages

| Stage | Method | Purpose | Notes |
|-------|--------|---------|-------|
| Validation | `ValidateConfig()` | Sanity-check config & required context (e.g. depth or color texture availability). | Called inside `PrepareResources`. Must not record GPU commands. |
| Pipeline Build Decision | `NeedRebuildPipelineState()` + `CreatePipelineStateDesc()` | Determine if PSO description must be (re)built (format / sample count changes). | Descriptor cached in `last_built_pso_desc_`. Actual PSO caching occurs inside `CommandRecorder::SetPipelineState`. |
| Resource Preparation | `DoPrepareResources(CommandRecorder&)` | Explicit resource state transitions & descriptor / view setup. | Must flush barriers after requiring final states. No draw/dispatch here. |
| Execution | `DoExecute(CommandRecorder&)` | Bind PSO, root bindings (scene/material constants), issue draw calls. | Base class pre-binds PSO + scene CBV, subclass handles render target setup & draws. |
| Registration | `Context().RegisterPass(this)` | Make pass discoverable by downstream passes via fixed index registry. | Performed by subclass inside `DoExecute` after successful work. |

## Root Binding Conventions

`enum class RenderPass::RootBindings { kBindlessTableSrv = 0, kSceneConstantsCbv
= 1, kMaterialConstantsCbv = 2, kDrawIndexConstant = 3 };`

* DepthPrePass uses indices 0,1,2*,3 (*MaterialConstants included for consistency but unused).
* ShaderPass uses 0,1,2,3 (binds material constants only if `material_constants`
  is non-null).

All passes now support multi-draw items through root constant binding at index 3.
Before each draw call, `BindDrawIndexConstant()` sets the current draw index,
allowing shaders to access the correct `DrawResourceIndices` entry via `g_DrawIndex`.

See: [bindless conventions](bindless_conventions.md).

## Error Handling

Exceptions thrown in `DoExecute` are logged and re-thrown by the base class.
Callers of graph coroutines should surface or handle these as frame failures.

## Registration Timing

Pass registration happens only after successful execution so downstream passes
must handle `GetPass<RequiredPass>() == nullptr` defensively in future
extensions.

Related docs: [render context & pass
registry](render_graph.md), [passes](passes/).
