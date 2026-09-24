//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Vortex/Shadows/Types/FrameShadowInputs.h>
#include <Oxygen/Vortex/Types/ShadowFrameBindings.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {
class Renderer;
class DepthPrepassMeshProcessor;
namespace internal {
  class RetainedTexturePool;
}

namespace shadows {

//! Camera-space caster depth, created only for active contact-shadow requests.
class ContactShadowCasterDepthPass final {
public:
  explicit ContactShadowCasterDepthPass(Renderer& renderer);
  ~ContactShadowCasterDepthPass();

  void OnFrameStart(frame::SequenceNumber sequence);
  void RemoveView(ViewId view_id);
  [[nodiscard]] auto Record(const PreparedViewShadowInput& input,
    ShadowFrameBindings& bindings) -> std::shared_ptr<graphics::Texture>;

private:
  Renderer& renderer_;
  frame::SequenceNumber sequence_ { 0U };
  std::unique_ptr<vortex::internal::RetainedTexturePool> textures_;
  std::unique_ptr<DepthPrepassMeshProcessor> mesh_processor_;
};

} // namespace shadows
} // namespace oxygen::vortex
