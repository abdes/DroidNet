//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Hzb/HzbPyramidBuilder.h>
#include <Oxygen/Vortex/Types/ScreenHzbFrameBindings.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::graphics {
class CommandRecorder;
class Texture;
} // namespace oxygen::graphics

namespace oxygen::vortex {

struct RenderContext;
struct SceneTexturesConfig;
class Renderer;
class SceneTextures;

class ScreenHzbModule {
public:
  struct Output {
    std::shared_ptr<const graphics::Texture> closest_texture;
    std::shared_ptr<const graphics::Texture> furthest_texture;
    ScreenHzbFrameBindings bindings {};
    bool available { false };
  };

  //! Furthest-only pyramid for occlusion culling; never published.
  struct OcclusionPyramid {
    std::shared_ptr<const graphics::Texture> texture;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
    std::uint32_t width { 0U };
    std::uint32_t height { 0U };
    std::uint32_t mip_count { 0U };
  };

  OXGN_VRTX_API explicit ScreenHzbModule(
    Renderer& renderer, const SceneTexturesConfig& scene_textures_config);
  OXGN_VRTX_API ~ScreenHzbModule();

  ScreenHzbModule(const ScreenHzbModule&) = delete;
  auto operator=(const ScreenHzbModule&) -> ScreenHzbModule& = delete;
  ScreenHzbModule(ScreenHzbModule&&) = delete;
  auto operator=(ScreenHzbModule&&) -> ScreenHzbModule& = delete;

  OXGN_VRTX_API void Execute(RenderContext& ctx,
    graphics::CommandRecorder& recorder, SceneTextures& scene_textures);
  //! Records the occlusion pyramid of the current view of `ctx` from `source`.
  /*!
   The pyramid lives in the view's state, outside the HZB history, and is
   recreated only when its extent changes. It is left in `kShaderResource`.

   @return The pyramid, or nothing when it could not be built.
  */
  OXGN_VRTX_API auto BuildOcclusionPyramid(RenderContext& ctx,
    graphics::CommandRecorder& recorder,
    const HzbPyramidBuilder::Source& source) -> std::optional<OcclusionPyramid>;
  OXGN_VRTX_API void OnFrameStart();
  OXGN_VRTX_API void RemoveViewState(ViewId view_id);
  [[nodiscard]] OXGN_VRTX_API auto GetCurrentOutput() const -> const Output&;
  [[nodiscard]] OXGN_VRTX_API auto GetPreviousOutput() const -> const Output&;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  Output current_output_ {};
  Output previous_output_ {};
  ViewId output_view_id_ { kInvalidViewId };
};

} // namespace oxygen::vortex
