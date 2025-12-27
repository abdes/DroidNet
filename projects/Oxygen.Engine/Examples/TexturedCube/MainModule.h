//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/EngineModule.h>
#include <Oxygen/Core/PhaseRegistry.h>
#include <Oxygen/Input/InputSystem.h>
#include <Oxygen/Scene/Scene.h>

#include "../Common/AsyncEngineApp.h"
#include "../Common/SingleViewExample.h"

namespace oxygen::examples::textured_cube {

class MainModule final : public common::SingleViewExample {
  OXYGEN_TYPED(MainModule)

public:
  using Base = oxygen::examples::common::SingleViewExample;

  enum class TextureIndexMode : std::uint8_t {
    kFallback = 0,
    kForcedError = 1,
    kCustom = 2,
  };

  explicit MainModule(const oxygen::examples::common::AsyncEngineApp& app);

  [[nodiscard]] auto GetName() const noexcept -> std::string_view override
  {
    return "MainModule";
  }

  [[nodiscard]] auto GetPriority() const noexcept
    -> oxygen::engine::ModulePriority override
  {
    return engine::ModulePriority { 500 };
  }

  [[nodiscard]] auto GetSupportedPhases() const noexcept
    -> oxygen::engine::ModulePhaseMask override
  {
    using namespace core;
    return engine::MakeModuleMask<PhaseId::kFrameStart, PhaseId::kSceneMutation,
      PhaseId::kGameplay, PhaseId::kGuiUpdate, PhaseId::kPreRender,
      PhaseId::kCompositing, PhaseId::kFrameEnd>();
  }

  ~MainModule() override = default;

  OXYGEN_MAKE_NON_COPYABLE(MainModule);
  OXYGEN_MAKE_NON_MOVABLE(MainModule);

  auto OnAttached(oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept
    -> bool override;
  auto OnShutdown() noexcept -> void override;

  auto OnFrameStart(oxygen::engine::FrameContext& context) -> void override;
  auto OnExampleFrameStart(engine::FrameContext& context) -> void override;
  auto OnSceneMutation(engine::FrameContext& context) -> co::Co<> override;
  auto OnGameplay(engine::FrameContext& context) -> co::Co<> override;
  auto OnGuiUpdate(engine::FrameContext& context) -> co::Co<> override;
  auto OnPreRender(engine::FrameContext& context) -> co::Co<> override;
  auto OnCompositing(engine::FrameContext& context) -> co::Co<> override;
  auto OnFrameEnd(engine::FrameContext& context) -> void override;

protected:
  auto BuildDefaultWindowProperties() const
    -> platform::window::Properties override;

private:
  auto InitInputBindings() noexcept -> bool;
  auto EnsureMainCamera(int width, int height) -> void;
  auto ApplyOrbitAndZoom() -> void;
  auto DrawDebugOverlay(engine::FrameContext& context) -> void;

  std::shared_ptr<scene::Scene> scene_;
  scene::SceneNode main_camera_;
  scene::SceneNode cube_node_;

  std::shared_ptr<oxygen::input::Action> zoom_in_action_;
  std::shared_ptr<oxygen::input::Action> zoom_out_action_;
  std::shared_ptr<oxygen::input::Action> rmb_action_;
  std::shared_ptr<oxygen::input::Action> orbit_action_;

  std::shared_ptr<oxygen::input::InputMappingContext> camera_controls_ctx_;

  TextureIndexMode texture_index_mode_ { TextureIndexMode::kForcedError };
  std::uint32_t custom_texture_resource_index_ { 0U };
  oxygen::content::ResourceKey custom_texture_key_ { 0U };
  oxygen::content::ResourceKey forced_error_key_ { 0U };
  glm::vec2 uv_scale_ { 1.0f, 1.0f };
  glm::vec2 uv_offset_ { 0.0f, 0.0f };
  bool cube_needs_rebuild_ { true };

  std::shared_ptr<const oxygen::data::MaterialAsset> cube_material_;
  std::shared_ptr<oxygen::data::GeometryAsset> cube_geometry_;
  std::vector<std::shared_ptr<oxygen::data::GeometryAsset>>
    retired_cube_geometries_;

  std::array<char, 512> png_path_ {};
  bool png_load_requested_ { false };
  std::string png_status_message_ {};
  int png_last_width_ { 0 };
  int png_last_height_ { 0 };

  glm::vec3 camera_target_ { 0.0f, 0.0f, 0.0f };
  float orbit_yaw_rad_ { -glm::half_pi<float>() };
  float orbit_pitch_rad_ { 0.0f };
  float orbit_distance_ { 6.0f };
  float orbit_sensitivity_ { 0.01f };
  float zoom_step_ { 0.75f };
  float min_cam_distance_ { 1.25f };
  float max_cam_distance_ { 40.0f };
};

} // namespace oxygen::examples::textured_cube
