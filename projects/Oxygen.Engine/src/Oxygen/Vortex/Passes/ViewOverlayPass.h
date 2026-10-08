//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
struct ViewOverlay;
struct ViewOverlayLine;
struct ViewOverlayVertex;
namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal
namespace upload {
  class TransientStructuredBuffer;
} // namespace upload

//! Draws a view's editor overlay geometry over its post-processed output.
/*!
 Each layer draws its triangles, then its lines expanded to constant-width
 screen quads, alpha blended into the output without depth writes. The scene
 layer samples the scene depth and dims what scene geometry hides; the top
 layer is drawn afterwards over everything.
*/
class ViewOverlayPass {
public:
  struct Inputs {
    //! The view output the overlay is drawn into.
    observer_ptr<const graphics::Framebuffer> target;
    //! Scene depth and its SRV for the occlusion test; without them the scene
    //! layer counts as visible everywhere.
    observer_ptr<const graphics::Texture> scene_depth;
    ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
  };

  OXGN_VRTX_API explicit ViewOverlayPass(Renderer& renderer);
  OXGN_VRTX_API ~ViewOverlayPass();

  OXYGEN_MAKE_NON_COPYABLE(ViewOverlayPass)
  OXYGEN_MAKE_NON_MOVABLE(ViewOverlayPass)

  //! Records the overlay for the current view; returns the number of draws,
  //! zero when nothing of it is rendered.
  OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const ViewOverlay& overlay,
    const Inputs& inputs) -> std::uint32_t;

private:
  struct Constants;

  auto BeginFrame(const RenderContext& ctx) -> void;

  Renderer& renderer_;
  std::unique_ptr<internal::PerViewStructuredPublisher<Constants>> constants_;
  std::unique_ptr<upload::TransientStructuredBuffer> triangles_;
  std::unique_ptr<upload::TransientStructuredBuffer> lines_;
  std::optional<frame::SequenceNumber> frame_;
};

} // namespace oxygen::vortex
