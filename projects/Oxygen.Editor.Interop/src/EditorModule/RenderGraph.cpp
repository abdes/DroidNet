//===----------------------------------------------------------------------===//
// Compatibility shell for the editor's per-view render graph hook.
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include "EditorModule/RenderGraph.h"

namespace oxygen::interop::module {

  auto RenderGraph::ClearBackbufferReferences() -> void {
    DLOG_SCOPE_FUNCTION(3);

    render_context_.pass_target.reset();
  }

  auto RenderGraph::PrepareForRenderFrame(
    oxygen::observer_ptr<const oxygen::graphics::Framebuffer> fb) -> void {
    DLOG_SCOPE_FUNCTION(3);

    if (!fb) {
      return;
    }

    render_context_.pass_target = fb;
  }

  auto RenderGraph::RunPasses(const oxygen::vortex::RenderContext& ctx,
    oxygen::graphics::CommandRecorder& recorder)
    -> co::Co<> {
    (void)ctx;
    (void)recorder;
    co_return;
  }

} // namespace oxygen::interop::module
