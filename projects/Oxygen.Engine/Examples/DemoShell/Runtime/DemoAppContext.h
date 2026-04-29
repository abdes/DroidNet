//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/RendererConfig.h>
#include <Oxygen/Graphics/Common/Queues.h>

namespace oxygen {

class Platform;
class Graphics;
class AsyncEngine;

namespace engine {
  class InputSystem;
} // namespace oxygen::engine
namespace vortex {
  class Renderer;
} // namespace oxygen::vortex

} // namespace oxygen

namespace oxygen::examples {

//! Aggregated application state used by example event loops.
/*! Holds platform, graphics, engine, and module pointers shared across demo
    examples. Modules can inspect immutable configuration (e.g., fullscreen or
    headless) and observe engine subsystems via observer_ptr.
*/
class DemoAppContext {
public:
  bool headless { false };
  bool fullscreen { false };
  bool with_atmosphere { false };
  bool with_height_fog { false };
  bool with_local_fog { false };
  bool with_volumetric_fog { false };
  bool with_occlusion { false };
  bool with_translucency { false };
  bool vortex_local_fog_into_volumetric { true };
  bool vortex_volumetric_directional_shadows { true };
  bool vortex_volumetric_temporal_reprojection { true };
  float vortex_local_fog_volumetric_max_density { 0.01F };
  float vortex_local_fog_emissive_scale { 1.0F };
  float vortex_volumetric_fog_emissive_scale { 1.0F };
  float vortex_sky_light_volumetric_scattering_intensity { 1.0F };
  float vortex_aerial_scattering_strength { 1.0F };
  float vortex_aerial_start_depth_m { 100.0F };
  std::string startup_scene_name;
  std::string startup_skybox_path;
  int startup_skybox_layout { 0 };
  int startup_skybox_output_format { 0 };
  int startup_skybox_face_size { 512 };
  bool startup_skybox_flip_y { false };
  bool startup_skybox_tonemap_hdr_to_ldr { false };
  float startup_skybox_hdr_exposure_ev { 0.0F };
  float startup_sky_sphere_intensity { 1.0F };
  float startup_sky_light_intensity_mul { 1.0F };
  float startup_sky_light_diffuse { 1.0F };
  float startup_sky_light_specular { 1.0F };
  bool startup_sky_light_real_time_capture_enabled { false };
  glm::vec3 startup_sky_light_tint { 1.0F, 1.0F, 1.0F };
  bool startup_sky_light_lifecycle_proof_enabled { false };
  std::uint32_t startup_sky_light_lifecycle_disable_frame { 0U };
  std::uint32_t startup_sky_light_lifecycle_enable_frame { 0U };
  DirectionalShadowImplementationPolicy directional_shadow_policy {
    DirectionalShadowImplementationPolicy::kConventionalOnly
  };

  // Graphics queues setup shared across subsystems.
  graphics::SharedTransferQueueStrategy queue_strategy;

  // Core systems.
  std::shared_ptr<Platform> platform;
  std::weak_ptr<Graphics> gfx_weak;
  std::shared_ptr<AsyncEngine> engine;

  // Observed modules (non-owning).
  observer_ptr<vortex::Renderer> renderer;
  observer_ptr<engine::InputSystem> input_system;

  //! Flag toggled to request loop continue/stop.
  std::atomic_bool running { false };
};

} // namespace oxygen::examples
