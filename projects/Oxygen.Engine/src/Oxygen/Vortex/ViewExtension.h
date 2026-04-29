//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Vortex/CompositionView.h>
#include <Oxygen/Vortex/RenderContext.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::engine {
class FrameContext;
} // namespace oxygen::engine

namespace oxygen::graphics {
class CommandRecorder;
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct ViewFamilyAssembledContext {
  engine::FrameContext& frame_context;
  RenderContext& render_context;
};

struct ViewSetupContext {
  RenderContext& render_context;
};

struct ViewRenderGpuContext {
  RenderContext& render_context;
};

struct PostCompositionContext {
  engine::FrameContext& frame_context;
  CompositionView::SurfaceRouteId surface_id {
    CompositionView::kDefaultSurfaceRoute
  };
  graphics::Framebuffer& target;
  graphics::CommandRecorder& recorder;
};

class IViewExtension {
public:
  OXGN_VRTX_API virtual ~IViewExtension();

  virtual auto OnFamilyAssembled(const ViewFamilyAssembledContext&) -> void { }
  virtual auto OnViewSetup(const ViewSetupContext&) -> void { }
  virtual auto OnPreRenderViewGpu(const ViewRenderGpuContext&) -> void { }
  virtual auto OnPostRenderViewGpu(const ViewRenderGpuContext&) -> void { }
  virtual auto OnPostComposition(const PostCompositionContext&) -> void { }
};

using ViewExtensionPtr = std::shared_ptr<IViewExtension>;

} // namespace oxygen::vortex
