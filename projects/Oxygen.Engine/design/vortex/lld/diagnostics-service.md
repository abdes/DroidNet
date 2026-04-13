# DiagnosticsService LLD

**Phase:** 5A — Remaining Services
**Deliverable:** D.14
**Status:** `ready`

## 1. Scope and Context

### 1.1 What This Covers

`DiagnosticsService` — the cross-cutting service owning the
diagnostics-only CPU/GPU seam: GPU debug modes, ImGui panel infrastructure,
GPU timeline telemetry sinks, debug primitive rendering, and shader debug
mode routing.

### 1.2 Ownership Boundaries

DiagnosticsService **owns:**

- GPU debug overlay and visualization passes
- ImGui panel registration and rendering
- GPU debug primitives (lines, crosses, markers)
- GPU timeline telemetry data and sinks (Tracy backend)
- ShaderDebugMode storage and compatibility mapping
- DebugFrameBindings typed publication

DiagnosticsService **must NOT own:**

- Frame ordering or stage dispatch
- Scene-texture allocation
- Other services' domain logic
- Alternate root-binding conventions

### 1.3 Architectural Authority

- [ARCHITECTURE.md §8](../ARCHITECTURE.md) — subsystem services
- Design principle: diagnostics-only seam, no production-path coupling

## 2. Interface Contracts

### 2.1 File Placement

```text
src/Oxygen/Vortex/
└── Services/
    └── Diagnostics/
        ├── DiagnosticsService.h
        ├── DiagnosticsService.cpp
        ├── Internal/
        │   ├── GpuDebugRenderer.h/.cpp
        │   ├── ImGuiPanelManager.h/.cpp
        │   └── GpuTimelineCollector.h/.cpp
        ├── Passes/
        │   ├── DebugOverlayPass.h/.cpp
        │   └── GpuDebugPrimitivePass.h/.cpp
        └── Types/
            ├── ShaderDebugMode.h
            ├── DiagnosticsPanel.h
            └── GpuTimelineSink.h
```

### 2.2 Public API

```cpp
namespace oxygen::vortex {

class DiagnosticsService : public ISubsystemService {
 public:
  explicit DiagnosticsService(Renderer& renderer);
  ~DiagnosticsService() override;

  void Initialize(graphics::IGraphics& gfx,
                  const RendererConfig& config) override;
  void OnFrameStart(const FrameContext& frame) override;
  void Shutdown() override;

  /// Publish diagnostics-owned bindings into RenderContext.
  void PublishBindings(RenderContext& ctx);

  /// Execute debug visualization passes.
  void Execute(RenderContext& ctx,
               const SceneTextures& scene_textures);

  /// Clear GPU debug buffers (call before frame's draw calls).
  void ClearGpuDebugBuffers(RenderContext& ctx);

  /// Draw accumulated GPU debug primitives (call after scene rendering).
  void DrawGpuDebugPrimitives(RenderContext& ctx);

  /// Set active shader debug mode (GBuffer viz, wireframe, etc.)
  void SetShaderDebugMode(ShaderDebugMode mode);

  /// Register an ImGui diagnostics panel.
  void RegisterPanel(std::unique_ptr<DiagnosticsPanel> panel);

  /// Add a GPU timeline telemetry sink.
  void AddTimelineSink(std::shared_ptr<GpuTimelineSink> sink);

  /// Enable/disable GPU profiling scopes.
  void SetGpuProfilingEnabled(bool enabled);

 private:
  Renderer& renderer_;
  ShaderDebugMode debug_mode_{ShaderDebugMode::kNone};
  bool gpu_profiling_enabled_{false};

  std::unique_ptr<GpuDebugRenderer> debug_renderer_;
  std::unique_ptr<ImGuiPanelManager> panel_manager_;
  std::unique_ptr<GpuTimelineCollector> timeline_collector_;
  std::vector<std::unique_ptr<DiagnosticsPanel>> panels_;
  std::vector<std::shared_ptr<GpuTimelineSink>> sinks_;
};

}  // namespace oxygen::vortex
```

### 2.3 ShaderDebugMode

```cpp
enum class ShaderDebugMode : std::uint32_t {
  kNone = 0,                // Normal rendering
  kGBufferNormals,          // Visualize GBufferA (normals)
  kGBufferBaseColor,        // Visualize GBufferC (base color)
  kGBufferMetallic,         // Visualize GBufferB.r (metallic)
  kGBufferRoughness,        // Visualize GBufferB.b (roughness)
  kSceneDepth,              // Visualize depth buffer
  kVelocity,                // Visualize motion vectors
  kLightComplexity,         // Visualize light count per pixel
  kWireframe,               // Wireframe overlay
  kOverdraw,                // Overdraw heatmap
};
```

### 2.4 DiagnosticsPanel (Abstract Base)

```cpp
class DiagnosticsPanel {
 public:
  virtual ~DiagnosticsPanel() = default;
  virtual auto GetName() const -> std::string_view = 0;
  virtual void OnImGui() = 0;  // Called within ImGui frame
};
```

### 2.5 GpuTimelineSink (Abstract Base)

```cpp
class GpuTimelineSink {
 public:
  virtual ~GpuTimelineSink() = default;
  virtual void OnGpuScope(std::string_view name,
                           uint64_t begin_ticks,
                           uint64_t end_ticks) = 0;
};
```

## 3. Data Flow and Dependencies

### 3.1 Inputs

| Source | Data | Purpose |
| ------ | ---- | ------- |
| All SceneTextures products | GBuffers, depth, color, velocity | Debug visualization |
| GPU timestamp queries | Begin/end ticks per scope | Timeline telemetry |
| ImGui input state | Mouse, keyboard | Panel interaction |

### 3.2 Outputs

| Product | Consumer | Delivery |
| ------- | -------- | -------- |
| DebugFrameBindings | Shader debug modes | Via ViewFrameBindings |
| Debug overlay | Back buffer | Rendered after scene |
| ImGui draw data | ImGui renderer | Rendered last |
| Telemetry data | Timeline sinks (Tracy) | Callback |

## 4. Resource Management

| Resource | Lifetime | Notes |
| -------- | -------- | ----- |
| GPU debug primitive buffer | Per frame | Lines, crosses, markers |
| Debug overlay RTV | Per frame | Same as back buffer or separate target |
| Timestamp query heap | Persistent | Recycled across frames |
| ImGui vertex/index buffers | Per frame | Dynamic upload |

## 5. Shader Contracts

### 5.1 Debug Visualization Shader

```hlsl
// Services/Diagnostics/DebugVisualization.hlsl

#include "../../Shared/FullscreenTriangle.hlsli"
#include "../../Contracts/SceneTextures.hlsli"

cbuffer DebugConstants : register(b1) {
  uint DebugMode;
};

float4 DebugVisualizationPS(FullscreenVSOutput input) : SV_Target {
  switch (DebugMode) {
    case 1:  // GBuffer normals
      return float4(DecodeGBufferNormal(SampleGBuffer(0, input.uv, bindings)) * 0.5 + 0.5, 1);
    case 2:  // Base color
      return SampleGBuffer(2, input.uv, bindings);
    case 3:  // Metallic
      return SampleGBuffer(1, input.uv, bindings).rrrr;
    case 4:  // Roughness
      return SampleGBuffer(1, input.uv, bindings).bbbb;
    case 5:  // Depth
      float d = SampleSceneDepth(input.uv, bindings);
      return float4(d, d, d, 1);
    default:
      return float4(1, 0, 1, 1);  // Magenta = unsupported mode
  }
}
```

### 5.2 Debug Primitive Shader

```hlsl
// Services/Diagnostics/DebugPrimitive.hlsl
// Renders line/cross/marker primitives from structured buffer.
```

### 5.3 Catalog Registration

| Entrypoint | Profile | Notes |
| ---------- | ------- | ----- |
| `VortexDebugVisualizationVS` | vs_6_0 | Fullscreen triangle |
| `VortexDebugVisualizationPS` | ps_6_0 | Mode-switched GBuffer viz |
| `VortexDebugPrimitiveVS` | vs_6_0 | Line/cross geometry |
| `VortexDebugPrimitivePS` | ps_6_0 | Solid color |

## 6. Stage Integration

### 6.1 Dispatch Points

DiagnosticsService has multiple dispatch points, not a single stage:

- **OnFrameStart:** `ClearGpuDebugBuffers()`, `PublishBindings()`
- **After stage 23:** `Execute()` (debug overlays), `DrawGpuDebugPrimitives()`
- **ImGui phase:** Panel rendering within ImGui frame

### 6.2 Null-Safe Behavior

When null: no debug overlays, no profiling, no ImGui panels. Production
rendering is completely unaffected.

### 6.3 Capability Gate

Requires `kDiagnosticsAndProfiling`.

## 7. Tracy Integration

### 7.1 GPU Profiling Scopes

When enabled, DiagnosticsService instruments each render stage with Tracy
GPU profiling zones:

```cpp
// In each stage module:
TracyD3D12Zone(ctx.GetTracyContext(), cmd_list, "Stage9_BasePass");
```

DiagnosticsService owns:

- Tracy context management
- GPU timestamp query allocation and readback
- Scope naming convention enforcement
- Timeline sink callback dispatch

### 7.2 Frame Boundary Markers

```cpp
TracyD3D12Collect(tracy_ctx);  // At OnFrameEnd
FrameMark;                       // CPU frame marker
```

## 8. Testability Approach

1. **Debug mode validation:** Set each ShaderDebugMode → verify overlay
   renders expected visualization (normals as RGB, depth as grayscale, etc.)
2. **ImGui panel:** Register a test panel → verify it appears in overlay.
3. **Profiling scopes:** Enable GPU profiling → verify Tracy receives scope
   data (names, timings).
4. **No-diagnostics baseline:** Disable DiagnosticsService → verify render
   output is unaffected.

## 9. Open Questions

1. **Debug mode integration:** Should debug modes override SceneColor or
   render to a separate overlay? Current design: fullscreen overlay drawn
   after scene rendering, replacing the final output temporarily.
