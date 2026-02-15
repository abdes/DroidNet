//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Types/Color.h>

#include "DemoShell/Runtime/CompositionView.h"

namespace oxygen {
class Graphics;
namespace graphics {
  class Texture;
  class Framebuffer;
} // namespace graphics
} // namespace oxygen

namespace oxygen::examples::internal {

class ViewLifecycleAccessTag;

namespace access {
  struct ViewLifecycleTagFactory {
    static auto Get() noexcept -> ViewLifecycleAccessTag;
  };
} // namespace access

class ViewLifecycleAccessTag {
  friend struct access::ViewLifecycleTagFactory;
  ViewLifecycleAccessTag() noexcept = default;
};

//! Runtime state for one active composition view.
class CompositionViewImpl {
public:
  CompositionViewImpl() = default;
  ~CompositionViewImpl() = default;
  OXYGEN_DEFAULT_COPYABLE(CompositionViewImpl)
  OXYGEN_DEFAULT_MOVABLE(CompositionViewImpl)

  void PrepareForRender(const CompositionView& descriptor,
    uint32_t submission_order, frame::SequenceNumber frame_seq,
    Graphics& graphics, ViewLifecycleAccessTag tag);

  [[nodiscard]] auto GetDescriptor() const noexcept -> const CompositionView&
  {
    return descriptor_;
  }
  [[nodiscard]] auto GetSubmissionOrder() const noexcept -> uint32_t
  {
    return submission_order_;
  }
  [[nodiscard]] auto GetLastSeenFrame() const noexcept -> frame::SequenceNumber
  {
    return last_seen_frame_;
  }
  [[nodiscard]] auto GetRenderTargetWidth() const noexcept -> uint32_t
  {
    return render_target_width_;
  }
  [[nodiscard]] auto GetRenderTargetHeight() const noexcept -> uint32_t
  {
    return render_target_height_;
  }
  [[nodiscard]] auto UsesHdrRenderTargets() const noexcept -> bool
  {
    return uses_hdr_render_targets_;
  }
  [[nodiscard]] auto GetClearColor() const noexcept -> const graphics::Color&
  {
    return clear_color_;
  }

  [[nodiscard]] auto GetHdrTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&
  {
    return hdr_texture_;
  }
  [[nodiscard]] auto GetHdrFramebuffer() const noexcept
    -> const std::shared_ptr<graphics::Framebuffer>&
  {
    return hdr_framebuffer_;
  }
  [[nodiscard]] auto GetSdrTexture() const noexcept
    -> const std::shared_ptr<graphics::Texture>&
  {
    return sdr_texture_;
  }
  [[nodiscard]] auto GetSdrFramebuffer() const noexcept
    -> const std::shared_ptr<graphics::Framebuffer>&
  {
    return sdr_framebuffer_;
  }

  [[nodiscard]] auto GetPublishedViewId() const noexcept -> ViewId
  {
    return published_view_id_;
  }
  auto SetPublishedViewId(ViewId id, ViewLifecycleAccessTag /*tag*/) noexcept
    -> void
  {
    published_view_id_ = id;
  }

private:
  void EnsureResources(Graphics& graphics, ViewLifecycleAccessTag tag);

  CompositionView descriptor_;
  uint32_t submission_order_ { 0 };
  frame::SequenceNumber last_seen_frame_;

  // GPU resources.
  std::shared_ptr<graphics::Texture> hdr_texture_;
  std::shared_ptr<graphics::Framebuffer> hdr_framebuffer_;
  std::shared_ptr<graphics::Texture> sdr_texture_;
  std::shared_ptr<graphics::Framebuffer> sdr_framebuffer_;

  uint32_t render_target_width_ { 0 };
  uint32_t render_target_height_ { 0 };
  bool uses_hdr_render_targets_ { false };
  graphics::Color clear_color_ { 0.0F, 0.0F, 0.0F, 1.0F };

  // Publication/runtime linkage.
  ViewId published_view_id_ { kInvalidViewId };
};

} // namespace oxygen::examples::internal
