//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>
#include <Oxygen/Vortex/Upload/TransientStructuredBuffer.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {
class Renderer;
class DepthPrepassMeshProcessor;
namespace occlusion::internal {
  class DrawCullPass;
  class IndirectListBuilder;
} // namespace occlusion::internal
namespace internal {
  class RetainedTexturePool;
}

namespace shadows {

  //! Camera-space caster depth, created only for active contact-shadow
  //! requests.
  //!
  //! Records before the camera's view constants are final, so its shaders
  //! read only the pass inputs record: the view's matrices at record time and
  //! its published draw bindings.
  class ContactShadowCasterDepthPass final {
  public:
    OXGN_VRTX_API explicit ContactShadowCasterDepthPass(Renderer& renderer);
    OXGN_VRTX_API ~ContactShadowCasterDepthPass();

    OXYGEN_MAKE_NON_COPYABLE(ContactShadowCasterDepthPass)
    OXYGEN_MAKE_NON_MOVABLE(ContactShadowCasterDepthPass)

    OXGN_VRTX_API void OnFrameStart(
      frame::SequenceNumber sequence, frame::Slot slot);
    OXGN_VRTX_API void RemoveView(ViewId view_id);
    [[nodiscard]] OXGN_VRTX_API auto Record(
      const PreparedViewShadowInput& input, ShadowFrameBindings& bindings)
      -> std::shared_ptr<graphics::Texture>;

  private:
    Renderer& renderer_;
    frame::SequenceNumber sequence_ { 0U };
    frame::Slot slot_ { frame::kInvalidSlot };
    upload::TransientStructuredBuffer pass_constants_buffer_;
    std::unique_ptr<vortex::internal::RetainedTexturePool> textures_;
    std::unique_ptr<DepthPrepassMeshProcessor> mesh_processor_;
    std::unique_ptr<occlusion::internal::DrawCullPass> draw_cull_;
    std::unique_ptr<occlusion::internal::IndirectListBuilder> list_builder_;
  };

} // namespace shadows
} // namespace oxygen::vortex
