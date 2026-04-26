//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Framebuffer;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
class Renderer;
class SceneTextures;
class TranslucencyMeshProcessor;

struct TranslucencyExecutionResult {
  bool requested { false };
  bool executed { false };
  std::uint32_t draw_count { 0U };
};

class TranslucencyModule {
public:
  OXGN_VRTX_API explicit TranslucencyModule(Renderer& renderer);
  OXGN_VRTX_API ~TranslucencyModule();

  TranslucencyModule(const TranslucencyModule&) = delete;
  auto operator=(const TranslucencyModule&) -> TranslucencyModule& = delete;
  TranslucencyModule(TranslucencyModule&&) = delete;
  auto operator=(TranslucencyModule&&) -> TranslucencyModule& = delete;

  OXGN_VRTX_API auto Execute(RenderContext& ctx, SceneTextures& scene_textures)
    -> TranslucencyExecutionResult;

private:
  Renderer& renderer_;
  std::unique_ptr<TranslucencyMeshProcessor> mesh_processor_;
  std::shared_ptr<oxygen::graphics::Framebuffer> framebuffer_ {};
};

} // namespace oxygen::vortex
