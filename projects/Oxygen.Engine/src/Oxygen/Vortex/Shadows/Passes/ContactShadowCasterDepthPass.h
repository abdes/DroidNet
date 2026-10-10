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
  class ContactShadowCasterDepthPass final {
  public:
    explicit ContactShadowCasterDepthPass(Renderer& renderer);
    ~ContactShadowCasterDepthPass();

    OXYGEN_MAKE_NON_COPYABLE(ContactShadowCasterDepthPass)
    OXYGEN_MAKE_NON_MOVABLE(ContactShadowCasterDepthPass)

    void OnFrameStart(frame::SequenceNumber sequence, frame::Slot slot);
    void RemoveView(ViewId view_id);
    [[nodiscard]] auto Record(const PreparedViewShadowInput& input,
      ShadowFrameBindings& bindings) -> std::shared_ptr<graphics::Texture>;

  private:
    Renderer& renderer_;
    frame::SequenceNumber sequence_ { 0U };
    frame::Slot slot_ { frame::kInvalidSlot };
    std::unique_ptr<vortex::internal::RetainedTexturePool> textures_;
    std::unique_ptr<DepthPrepassMeshProcessor> mesh_processor_;
    std::unique_ptr<occlusion::internal::DrawCullPass> draw_cull_;
    std::unique_ptr<occlusion::internal::IndirectListBuilder> list_builder_;
  };

} // namespace shadows
} // namespace oxygen::vortex
