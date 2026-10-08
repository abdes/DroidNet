//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Framebuffer;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
struct ViewOutline;
namespace internal {
  template <typename Payload> class PerViewStructuredPublisher;
} // namespace internal

//! Draws a view's selection outline over its post-processed output.
/*!
 Two steps: the outlined draws are rasterized into a per-view RG8 mask (level,
 nearest-surface flag) with the depth prepass vertex shader, then a fullscreen
 pass draws an anti-aliased band around the mask into the output, dimmed where
 the outlined surface is hidden. The output is display-referred, so exposure
 and grading never see the outline.
*/
class SelectionOutlinePass {
public:
  struct Inputs {
    //! The view output the band is composed into.
    observer_ptr<const graphics::Framebuffer> target;
    //! Scene depth and its SRV for the occlusion test; without them every
    //! outlined surface counts as visible.
    observer_ptr<const graphics::Texture> scene_depth;
    ShaderVisibleIndex scene_depth_srv { kInvalidShaderVisibleIndex };
    bool reverse_z { true };
  };

  OXGN_VRTX_API explicit SelectionOutlinePass(Renderer& renderer);
  OXGN_VRTX_API ~SelectionOutlinePass();

  OXYGEN_MAKE_NON_COPYABLE(SelectionOutlinePass)
  OXYGEN_MAKE_NON_MOVABLE(SelectionOutlinePass)

  //! Records the outline for the current view; returns the outlined draw
  //! count, zero when nothing of the outline is rendered.
  OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const ViewOutline& outline,
    const Inputs& inputs) -> std::uint32_t;

  //! Releases the mask kept for a removed view.
  OXGN_VRTX_API auto RemoveView(ViewId view_id) -> void;

private:
  struct MaskConstants;
  struct CompositeConstants;
  struct ViewMask {
    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Framebuffer> framebuffer;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  };

  auto EnsureMask(ViewId view_id, std::uint32_t width, std::uint32_t height)
    -> ViewMask*;
  auto RetireMask(ViewMask& mask) -> void;
  auto BeginConstantsFrame(const RenderContext& ctx) -> void;

  Renderer& renderer_;
  std::unordered_map<ViewId, ViewMask> masks_;
  std::unique_ptr<internal::PerViewStructuredPublisher<MaskConstants>>
    mask_constants_;
  std::unique_ptr<internal::PerViewStructuredPublisher<CompositeConstants>>
    composite_constants_;
  std::optional<frame::SequenceNumber> constants_frame_;
};

} // namespace oxygen::vortex
