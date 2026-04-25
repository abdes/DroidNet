//===----------------------------------------------------------------------===//
// Compatibility shell for the editor's per-view render graph hook. The legacy
// pass objects moved into the Vortex renderer, so this class now only keeps the
// callback shape used by ViewRenderer.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <memory>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/RenderContext.h>

namespace oxygen::graphics {
  class Framebuffer;
  class CommandRecorder;
} // namespace oxygen::graphics

namespace oxygen::interop::module {

  class RenderGraph final {
  public:
    RenderGraph() noexcept = default;
    ~RenderGraph() noexcept = default;

    auto SetupRenderPasses() -> void {}

    // Accessors
    auto& GetRenderContext() noexcept { return render_context_; }

    // Helpers for per-frame attachment management. Examples frequently need to
    // assign the current swapchain framebuffer to the render-context and wire
    // the pass configs to the back-buffer textures. These convenience helpers
    // centralize that logic so examples only call a single API point.
    auto ClearBackbufferReferences() -> void;

    auto PrepareForRenderFrame(
      oxygen::observer_ptr<const oxygen::graphics::Framebuffer> fb) -> void;

    // Execute the configured pass list (DepthPrePass, ShaderPass,
    // TransparentPass) using the supplied recorder. Implemented as a coroutine
    // to match the renderer's usage pattern.
    auto RunPasses(const oxygen::vortex::RenderContext& ctx,
      oxygen::graphics::CommandRecorder& recorder)
      -> oxygen::co::Co<>;

  private:
    // Shared per-frame render context used by the editor module.
    oxygen::vortex::RenderContext render_context_{};
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
