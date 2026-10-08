//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/mat4x4.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Types/ViewPick.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class GpuTextureReadback;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;

//! Renders on-demand pick requests and resolves their readbacks.
/*!
 A request draws the current view's pickable draws (main-view-visible opaque,
 masked and translucent geometry, alpha-tested where masked) into an RG32 UINT
 target the size of the pick rectangle: draw index plus one and device depth,
 depth-tested so each pixel keeps the nearest surface. The target is copied to
 a readback and resolved into per-node hits when it lands, a few frames later.
*/
class ViewPickPass {
public:
  OXGN_VRTX_API explicit ViewPickPass(Renderer& renderer);
  //! Pending requests complete cancelled.
  OXGN_VRTX_API ~ViewPickPass();

  OXYGEN_MAKE_NON_COPYABLE(ViewPickPass)
  OXYGEN_MAKE_NON_MOVABLE(ViewPickPass)

  //! Records `request` for the current view; it completes failed when it
  //! cannot be recorded.
  OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    std::shared_ptr<ViewPickRequest> request) -> void;

  //! Completes every request whose readback has landed.
  OXGN_VRTX_API auto Poll() -> void;

  //! Pick texels, row by row (draw index plus one, raw depth bits), resolved
  //! into the hits of a result. Exposed for focused tests.
  struct PickImage {
    std::span<const std::uint32_t> texels;
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    //! Distance between rows, in 32-bit words.
    std::size_t row_stride { 0U };
  };
  struct PickGeometry {
    ViewPickRect rect {};
    ViewPort viewport {};
    glm::mat4 inv_view_proj { 1.0F };
    bool reverse_z { true };
  };
  [[nodiscard]] OXGN_VRTX_API static auto ResolveHits(const PickImage& image,
    std::span<const PreparedSceneFrame::DrawSource> sources,
    const PickGeometry& geometry) -> ViewPickResult;

private:
  struct PendingPick {
    std::shared_ptr<ViewPickRequest> request;
    std::shared_ptr<graphics::GpuTextureReadback> readback;
    std::vector<PreparedSceneFrame::DrawSource> sources;
    PickGeometry geometry {};
  };

  Renderer& renderer_;
  std::vector<PendingPick> pending_;
};

} // namespace oxygen::vortex
