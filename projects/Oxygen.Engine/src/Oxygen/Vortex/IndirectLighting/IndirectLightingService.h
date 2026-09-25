//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#include <memory>

#include <Oxygen/Vortex/api_export.h>
namespace oxygen::graphics {
class Framebuffer;
class CommandRecorder;
}
namespace oxygen::vortex {
class Renderer;
struct RenderContext;
class SceneTextures;
struct EnvironmentFrameBindings;
class IndirectLightingService final {
public:
  explicit IndirectLightingService(Renderer& renderer)
    : renderer_(renderer)
  {
  }
  OXGN_VRTX_API auto Record(RenderContext& ctx,
    graphics::CommandRecorder& recorder, const SceneTextures& textures,
    const EnvironmentFrameBindings& bindings) -> bool;

private:
  Renderer& renderer_;
  std::shared_ptr<graphics::Framebuffer> framebuffer_;
};
} // namespace oxygen::vortex
