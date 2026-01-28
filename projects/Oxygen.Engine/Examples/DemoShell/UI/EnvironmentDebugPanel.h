//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <glm/vec3.hpp>

#include <Oxygen/Base/ObserverPtr.h>

#include <Oxygen/Scene/SceneNode.h>

#include "DemoShell/Services/FileBrowserService.h"
#include "DemoShell/Services/SkyboxService.h"

// Forward declarations
namespace oxygen::scene {
class Scene;
class SceneNode;
} // namespace oxygen::scene

namespace oxygen::engine {
class Renderer;
} // namespace oxygen::engine

namespace oxygen::scene::environment {
class SkyAtmosphere;
class SkySphere;
class SkyLight;
// NOTE: Fog class not used - use Aerial Perspective instead.
class PostProcessVolume;
} // namespace oxygen::scene::environment

namespace oxygen::engine::internal {
class SkyAtmosphereLutManager;
} // namespace oxygen::engine::internal

namespace oxygen::examples::ui {

//! Atmosphere flags for debug UI control.
//! Must match AtmosphereFlags in EnvironmentDynamicData.h
enum class AtmosphereDebugFlags : uint32_t {
  kNone = 0x0,
  kUseLut = 0x1,
  kVisualizeLut = 0x2,
  kForceAnalytic = 0x4,
};

//! Configuration for the environment debug panel.
struct EnvironmentDebugConfig {
  //! Scene to inspect/modify (can be null when no scene is loaded).
  std::shared_ptr<oxygen::scene::Scene> scene { nullptr };

  //! File browser service for picking skybox images.
  observer_ptr<FileBrowserService> file_browser_service { nullptr };

  //! Skybox service for loading/equipping skyboxes.
  observer_ptr<SkyboxService> skybox_service { nullptr };

  //! Renderer to query LUT state from.
  observer_ptr<oxygen::engine::Renderer> renderer { nullptr };

  //! Callback invoked when atmosphere parameters change (triggers LUT regen).
  std::function<void()> on_atmosphere_params_changed {};

  //! Callback invoked when exposure changes.
  std::function<void()> on_exposure_changed {};
};

//! Comprehensive environment system debug panel.
/*!
 Provides ImGui controls for all scene environment systems:

 - **SkyAtmosphere**: Physical sky parameters, LUT status, aerial perspective
 - **SkySphere**: Background source, cubemap, solid color
 - **SkyLight**: IBL source, tint, intensity
 - **PostProcessVolume**: Exposure, bloom, color grading
 - **Sun**: Primary sun component controls

 ### Design Goals

 1. **Read-Modify-Write Safety**: Changes are applied during the correct frame
    phase (OnSceneMutation or OnGameplay) to avoid race conditions.

 2. **State Consistency**: The panel caches current values and only writes back
    when user makes changes, preventing feedback loops.

 3. **Modular Sections**: Each environment system has its own collapsible
    section for clean organization.

 4. **Debug Information**: Shows internal renderer state like LUT validity,
    sun selection, and frame timing.

 ### Usage

 ```cpp
 EnvironmentDebugPanel panel;

 // In initialization:
 EnvironmentDebugConfig config;
 config.scene = scene_;
 config.renderer = &renderer;
 panel.Initialize(config);

 // In OnGuiUpdate:
 panel.Draw();

 // In OnSceneMutation (if HasPendingChanges()):
 panel.ApplyPendingChanges();
 ```
*/
class EnvironmentDebugPanel {
public:
  EnvironmentDebugPanel() = default;
  ~EnvironmentDebugPanel() = default;

  EnvironmentDebugPanel(const EnvironmentDebugPanel&) = delete;
  auto operator=(const EnvironmentDebugPanel&)
    -> EnvironmentDebugPanel& = delete;
  EnvironmentDebugPanel(EnvironmentDebugPanel&&) = default;
  auto operator=(EnvironmentDebugPanel&&) -> EnvironmentDebugPanel& = default;

  //! Initialize or update the panel configuration.
  void Initialize(const EnvironmentDebugConfig& config);

  //! Update configuration (e.g., when scene changes).
  void UpdateConfig(const EnvironmentDebugConfig& config);

  //! Draw the ImGui panel. Call during OnGuiUpdate.
  void Draw();

  //! Draws the panel content without creating a window.
  void DrawContents();

  //! Returns true if there are pending changes to apply.
  [[nodiscard]] auto HasPendingChanges() const -> bool;

  //! Apply pending changes to the scene. Call during OnSceneMutation.
  void ApplyPendingChanges();

  //! Request that the panel resync its cached state from the scene.
  /*!
   This is useful when an external system (e.g. an async skybox load) modifies
   the scene environment outside of `ApplyPendingChanges`.
  */
  void RequestResync() { needs_sync_ = true; }

  //! Update the skybox status text shown in the UI.
  void SetSkyboxLoadStatus(std::string_view status, int face_size,
    oxygen::content::ResourceKey resource_key);

  //! Get the current sky light parameters from the UI cache.
  [[nodiscard]] auto GetSkyLightParams() const -> SkyboxService::SkyLightParams;

  //! Get current atmosphere debug flags for renderer.
  [[nodiscard]] auto GetAtmosphereFlags() const -> uint32_t;

private:
  //=== UI Drawing Methods ===-----------------------------------------------//
  void DrawSkyAtmosphereSection();
  void DrawSkySphereSection();
  void DrawSkyLightSection();
  // NOTE: Fog UI removed - use Aerial Perspective instead. Real fog TBD.
  void DrawPostProcessSection();
  void DrawSunSection();
  void DrawRendererDebugSection();

  //=== Helper Methods ===---------------------------------------------------//
  struct SunUiSettings {
    bool enabled { true };
    float azimuth_deg { 90.0F };
    float elevation_deg { 30.0F };
    glm::vec3 color_rgb { 1.0F, 1.0F, 1.0F };
    float intensity_lux { 10.0F };
    bool use_temperature { false };
    float temperature_kelvin { 6500.0F };
    float disk_radius_deg { 0.268F };
  };

  void SyncFromScene();
  void SyncDebugFlagsFromRenderer();
  void MarkDirty();
  //! Resets cached sun UI state to Sun component defaults.
  void ResetSunUiToDefaults();
  //! Finds a directional light node to use as sun when FromScene is selected.
  auto FindSunLightCandidate() const -> std::optional<scene::SceneNode>;
  //! Ensures the cached sun light node is valid or refreshes it if needed.
  void UpdateSunLightCandidate();
  //! Ensures a synthetic sun directional light exists in the scene.
  void EnsureSyntheticSunLight();
  //! Removes the synthetic sun light if it was created by this panel.
  void DestroySyntheticSunLight();
  //! Returns a reference to the cached settings for the chosen sun source.
  auto GetSunSettingsForSource(int source) -> SunUiSettings&;
  //! Loads cached settings for the chosen sun source into the UI state.
  void LoadSunSettingsFromProfile(int source);
  //! Saves UI state into the cached settings for the chosen sun source.
  void SaveSunSettingsToProfile(int source);

  //=== Configuration ===----------------------------------------------------//
  EnvironmentDebugConfig config_ {};
  bool initialized_ { false };

  //=== Cached State ===-----------------------------------------------------//
  // SkyAtmosphere
  bool sky_atmo_enabled_ { false };
  float planet_radius_km_ { 6360.0F };
  float atmosphere_height_km_ { 80.0F };
  glm::vec3 ground_albedo_ { 0.1F, 0.1F, 0.1F };
  float rayleigh_scale_height_km_ { 8.0F };
  float mie_scale_height_km_ { 1.2F };
  float mie_anisotropy_ { 0.8F };
  float multi_scattering_ { 1.0F };
  bool sun_disk_enabled_ { true };
  float sun_disk_radius_deg_ { 0.268F };
  float aerial_perspective_scale_ { 1.0F };
  float aerial_scattering_strength_ { 1.0F };

  // SkySphere
  bool sky_sphere_enabled_ { false };
  int sky_sphere_source_ { 0 }; // 0=Cubemap, 1=SolidColor
  glm::vec3 sky_sphere_solid_color_ { 0.2F, 0.3F, 0.5F };
  float sky_sphere_intensity_ { 1.0F };
  float sky_sphere_rotation_deg_ { 0.0F };

  // Skybox load UI (disk -> synthetic cubemap)
  std::array<char, 260> skybox_path_ {};
  int skybox_layout_idx_ { 0 };
  int skybox_output_format_idx_ { 0 };
  int skybox_face_size_ { 512 };
  bool skybox_flip_y_ { false };
  bool skybox_tonemap_hdr_to_ldr_ { false };
  float skybox_hdr_exposure_ev_ { 0.0F };
  std::string skybox_status_message_ {};
  int skybox_last_face_size_ { 0 };
  oxygen::content::ResourceKey skybox_last_resource_key_ { 0U };
  observer_ptr<FileBrowserService> file_browser_ { nullptr };

  // SkyLight
  bool sky_light_enabled_ { false };
  int sky_light_source_ { 0 }; // 0=CapturedScene, 1=SpecifiedCubemap
  glm::vec3 sky_light_tint_ { 1.0F, 1.0F, 1.0F };
  float sky_light_intensity_ { 1.0F };
  float sky_light_diffuse_ { 1.0F };
  float sky_light_specular_ { 1.0F };

  // Sun component
  bool sun_present_ { false };
  bool sun_enabled_ { true };
  int sun_source_ { 0 }; // 0=FromScene, 1=Synthetic
  float sun_azimuth_deg_ { 90.0F };
  float sun_elevation_deg_ { 30.0F };
  glm::vec3 sun_color_rgb_ { 1.0F, 1.0F, 1.0F };
  float sun_intensity_lux_ { 10.0F };
  bool sun_use_temperature_ { false };
  float sun_temperature_kelvin_ { 6500.0F };
  float sun_component_disk_radius_deg_ { 0.268F };
  scene::SceneNode sun_light_node_ {};
  bool sun_light_available_ { false };
  scene::SceneNode synthetic_sun_light_node_ {};
  bool synthetic_sun_light_created_ { false };

  SunUiSettings sun_scene_settings_ {};
  SunUiSettings sun_synthetic_settings_ {};

  // NOTE: Fog member variables removed - use Aerial Perspective instead.
  // Real volumetric fog system to be implemented in the future.

  // PostProcess
  bool post_process_enabled_ { false };
  int tone_mapper_ { 0 }; // 0=ACES, 1=Reinhard, 2=None
  int exposure_mode_ { 0 }; // 0=Manual, 1=Auto
  float exposure_compensation_ev_ { 0.0F };
  float auto_exposure_min_ev_ { -4.0F };
  float auto_exposure_max_ev_ { 16.0F };
  float auto_exposure_speed_up_ { 3.0F };
  float auto_exposure_speed_down_ { 1.0F };
  float bloom_intensity_ { 0.0F };
  float bloom_threshold_ { 1.0F };
  float saturation_ { 1.0F };
  float contrast_ { 1.0F };
  float vignette_ { 0.0F };

  // Atmosphere Debug Flags
  bool use_lut_ { true };
  bool visualize_lut_ { false };
  bool force_analytic_ { false };

  //=== Pending Change Tracking ===------------------------------------------//
  bool pending_changes_ { false };
  bool needs_sync_ { true };
};

} // namespace oxygen::examples::ui
