//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>

#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class Texture;
}

namespace oxygen::vortex {

struct RenderContext;
struct SceneTexturesConfig;
class Renderer;
class SceneTextures;

class ScreenHzbModule {
public:
  struct Output {
    std::shared_ptr<const graphics::Texture> closest_texture {};
    std::shared_ptr<const graphics::Texture> furthest_texture {};
    ScreenHzbFrameBindings bindings {};
    bool available { false };
  };

  OXGN_VRTX_API explicit ScreenHzbModule(
    Renderer& renderer, const SceneTexturesConfig& scene_textures_config);
  OXGN_VRTX_API ~ScreenHzbModule();

  ScreenHzbModule(const ScreenHzbModule&) = delete;
  auto operator=(const ScreenHzbModule&) -> ScreenHzbModule& = delete;
  ScreenHzbModule(ScreenHzbModule&&) = delete;
  auto operator=(ScreenHzbModule&&) -> ScreenHzbModule& = delete;

  OXGN_VRTX_API void Execute(RenderContext& ctx, SceneTextures& scene_textures);
  OXGN_VRTX_API void OnFrameStart();
  [[nodiscard]] OXGN_VRTX_API auto GetCurrentOutput() const -> const Output&;
  [[nodiscard]] OXGN_VRTX_API auto GetPreviousOutput() const -> const Output&;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Output current_output_ {};
  Output previous_output_ {};
};

} // namespace oxygen::vortex
