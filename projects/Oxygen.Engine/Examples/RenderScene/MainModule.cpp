//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "MainModule.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <numbers>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>

#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/StringUtils.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/LooseCookedInspection.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Engine/AsyncEngine.h>
#include <Oxygen/ImGui/ImGuiModule.h>
#include <Oxygen/Input/Action.h>
#include <Oxygen/Input/ActionTriggers.h>
#include <Oxygen/Input/InputActionMapping.h>
#include <Oxygen/Input/InputMappingContext.h>
#include <Oxygen/Platform/Input.h>
#include <Oxygen/Renderer/Internal/SkyAtmosphereLutManager.h>
#include <Oxygen/Renderer/Renderer.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Camera/Perspective.h>
#include <Oxygen/Scene/Environment/Fog.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/Environment/SkyAtmosphere.h>
#include <Oxygen/Scene/Environment/SkyLight.h>
#include <Oxygen/Scene/Environment/SkySphere.h>
#include <Oxygen/Scene/Environment/Sun.h>
#include <Oxygen/Scene/Environment/VolumetricClouds.h>
#include <Oxygen/Scene/Light/DirectionalLight.h>
#include <Oxygen/Scene/Light/PointLight.h>
#include <Oxygen/Scene/Light/SpotLight.h>
#include <Oxygen/Scene/Types/Flags.h>

using oxygen::scene::SceneNodeFlags;

namespace {

auto MakeLookRotationFromPosition(const glm::vec3& position,
  const glm::vec3& target, const glm::vec3& up_direction = { 0.0F, 0.0F, 1.0F })
  -> glm::quat
{
  const auto forward_raw = target - position;
  const float forward_len2 = glm::dot(forward_raw, forward_raw);
  if (forward_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto forward = glm::normalize(forward_raw);
  // Avoid singularities when forward is colinear with up.
  glm::vec3 up_dir = up_direction;
  const float dot_abs = std::abs(glm::dot(forward, glm::normalize(up_dir)));
  if (dot_abs > 0.999F) {
    // Pick an alternate up that is guaranteed to be non-colinear.
    up_dir = (std::abs(forward.z) > 0.9F) ? glm::vec3(0.0F, 1.0F, 0.0F)
                                          : glm::vec3(0.0F, 0.0F, 1.0F);
  }

  const auto right_raw = glm::cross(forward, up_dir);
  const float right_len2 = glm::dot(right_raw, right_raw);
  if (right_len2 <= 1e-8F) {
    return glm::quat(1.0F, 0.0F, 0.0F, 0.0F);
  }

  const auto right = right_raw / std::sqrt(right_len2);
  const auto up = glm::cross(right, forward);

  glm::mat4 look_matrix(1.0F);
  // NOLINTBEGIN(*-pro-bounds-avoid-unchecked-container-access)
  look_matrix[0] = glm::vec4(right, 0.0F);
  look_matrix[1] = glm::vec4(up, 0.0F);
  look_matrix[2] = glm::vec4(-forward, 0.0F);
  // NOLINTEND(*-pro-bounds-avoid-unchecked-container-access)

  return glm::quat_cast(look_matrix);
}

auto FindRenderSceneContentRoot() -> std::filesystem::path
{
  auto dir = std::filesystem::current_path();
  for (int i = 0; i < 6; ++i) {
    const auto direct_fbx = dir / "fbx";
    if (std::filesystem::exists(direct_fbx)
      && std::filesystem::is_directory(direct_fbx)) {
      return dir;
    }

    const auto nested_root = dir / "Examples" / "RenderScene";
    const auto nested_fbx = nested_root / "fbx";
    if (std::filesystem::exists(nested_fbx)
      && std::filesystem::is_directory(nested_fbx)) {
      return nested_root;
    }

    if (!dir.has_parent_path()) {
      break;
    }
    const auto parent = dir.parent_path();
    if (parent == dir) {
      break;
    }
    dir = parent;
  }

  return std::filesystem::current_path();
}

} // namespace

namespace oxygen::examples::render_scene {

auto MainModule::UpdateActiveCameraInputContext() -> void
{
  if (!app_.input_system) {
    return;
  }

  if (camera_mode_ == CameraMode::kOrbit) {
    if (orbit_controls_ctx_) {
      app_.input_system->ActivateMappingContext(orbit_controls_ctx_);
    }
    if (fly_controls_ctx_) {
      app_.input_system->DeactivateMappingContext(fly_controls_ctx_);
    }
  } else {
    if (orbit_controls_ctx_) {
      app_.input_system->DeactivateMappingContext(orbit_controls_ctx_);
    }
    if (fly_controls_ctx_) {
      app_.input_system->ActivateMappingContext(fly_controls_ctx_);
    }
  }
}

class SceneLoader : public std::enable_shared_from_this<SceneLoader> {
public:
  SceneLoader(
    oxygen::content::AssetLoader& loader, const int width, const int height)
    : loader_(loader)
    , width_(width)
    , height_(height)
  {
  }

  ~SceneLoader() { LOG_F(INFO, "SceneLoader: Destroying loader."); }

  void Start(const data::AssetKey& key)
  {
    LOG_F(INFO, "SceneLoader: Starting load for scene key: {}",
      oxygen::data::to_string(key));

    swap_.scene_key = key;
    // Start loading the scene asset
    loader_.StartLoadAsset<data::SceneAsset>(key,
      [weak_self = weak_from_this()](std::shared_ptr<data::SceneAsset> asset) {
        if (auto self = weak_self.lock()) {
          self->OnSceneLoaded(std::move(asset));
        }
      });
  }

  [[nodiscard]] auto IsReady() const -> bool { return ready_ && !consumed_; }
  [[nodiscard]] auto IsFailed() const -> bool { return failed_; }
  [[nodiscard]] auto IsConsumed() const -> bool { return consumed_; }

  auto GetResult() -> MainModule::PendingSceneSwap { return std::move(swap_); }

  void MarkConsumed()
  {
    consumed_ = true;
    linger_frames_ = 2;
  }

  auto Tick() -> bool
  {
    if (consumed_) {
      if (linger_frames_ > 0) {
        linger_frames_--;
        return false;
      }
      return true;
    }
    return false;
  }

private:
  void OnSceneLoaded(std::shared_ptr<data::SceneAsset> asset)
  {
    try {
      if (!asset) {
        LOG_F(ERROR, "SceneLoader: Failed to load scene asset");
        failed_ = true;
        return;
      }

      LOG_F(INFO, "SceneLoader: Scene asset loaded. Instantiating nodes...");

      swap_.scene = std::make_shared<scene::Scene>("RenderScene");

      // Check for mutually exclusive sky systems
      const auto sky_atmo_record = asset->TryGetSkyAtmosphereEnvironment();
      const auto sky_sphere_record = asset->TryGetSkySphereEnvironment();

      const bool sky_atmo_enabled
        = sky_atmo_record && sky_atmo_record->enabled != 0U;
      const bool sky_sphere_enabled
        = sky_sphere_record && sky_sphere_record->enabled != 0U;

      if (sky_atmo_enabled && sky_sphere_enabled) {
        LOG_F(WARNING,
          "SceneLoader: Both SkyAtmosphere and SkySphere are enabled in the "
          "scene. They are mutually exclusive; SkyAtmosphere will be used.");
      }

      auto environment = std::make_unique<scene::SceneEnvironment>();

      auto& sun = environment->AddSystem<scene::environment::Sun>();
      sun.SetEnabled(true);
      sun.SetSunSource(scene::environment::SunSource::kFromScene);
      if (sky_atmo_record) {
        sun.SetDiskAngularRadiusRadians(
          sky_atmo_record->sun_disk_angular_radius_radians);
      }

      // SkyAtmosphere takes priority over SkySphere
      if (sky_atmo_enabled) {
        auto& atmo
          = environment->AddSystem<scene::environment::SkyAtmosphere>();
        atmo.SetPlanetRadiusMeters(sky_atmo_record->planet_radius_m);
        atmo.SetAtmosphereHeightMeters(sky_atmo_record->atmosphere_height_m);
        atmo.SetGroundAlbedoRgb(
          oxygen::Vec3 { sky_atmo_record->ground_albedo_rgb[0],
            sky_atmo_record->ground_albedo_rgb[1],
            sky_atmo_record->ground_albedo_rgb[2] });
        atmo.SetRayleighScatteringRgb(
          oxygen::Vec3 { sky_atmo_record->rayleigh_scattering_rgb[0],
            sky_atmo_record->rayleigh_scattering_rgb[1],
            sky_atmo_record->rayleigh_scattering_rgb[2] });
        atmo.SetRayleighScaleHeightMeters(
          sky_atmo_record->rayleigh_scale_height_m);
        atmo.SetMieScatteringRgb(
          oxygen::Vec3 { sky_atmo_record->mie_scattering_rgb[0],
            sky_atmo_record->mie_scattering_rgb[1],
            sky_atmo_record->mie_scattering_rgb[2] });
        atmo.SetMieScaleHeightMeters(sky_atmo_record->mie_scale_height_m);
        atmo.SetMieAnisotropy(sky_atmo_record->mie_g);
        atmo.SetAbsorptionRgb(oxygen::Vec3 { sky_atmo_record->absorption_rgb[0],
          sky_atmo_record->absorption_rgb[1],
          sky_atmo_record->absorption_rgb[2] });
        atmo.SetAbsorptionScaleHeightMeters(
          sky_atmo_record->absorption_scale_height_m);
        atmo.SetMultiScatteringFactor(sky_atmo_record->multi_scattering_factor);
        atmo.SetSunDiskEnabled(sky_atmo_record->sun_disk_enabled != 0U);
        atmo.SetSunDiskAngularRadiusRadians(
          sky_atmo_record->sun_disk_angular_radius_radians);
        atmo.SetAerialPerspectiveDistanceScale(
          sky_atmo_record->aerial_perspective_distance_scale);
        LOG_F(INFO, "SceneLoader: Applied SkyAtmosphere environment");
      } else if (sky_sphere_enabled) {
        auto& sky_sphere
          = environment->AddSystem<scene::environment::SkySphere>();

        if (sky_sphere_record->source
          == static_cast<std::uint32_t>(
            scene::environment::SkySphereSource::kSolidColor)) {
          sky_sphere.SetSource(
            scene::environment::SkySphereSource::kSolidColor);
        } else {
          LOG_F(WARNING,
            "SceneLoader: SkySphere cubemap source requested, but "
            "scene-authored "
            "cubemap AssetKey resolution is not implemented in this example. "
            "Keeping solid color; use the Environment panel Skybox Loader to "
            "bind a cubemap at runtime.");
          sky_sphere.SetSource(
            scene::environment::SkySphereSource::kSolidColor);
        }

        sky_sphere.SetSolidColorRgb(
          oxygen::Vec3 { sky_sphere_record->solid_color_rgb[0],
            sky_sphere_record->solid_color_rgb[1],
            sky_sphere_record->solid_color_rgb[2] });
        sky_sphere.SetIntensity(sky_sphere_record->intensity);
        sky_sphere.SetRotationRadians(sky_sphere_record->rotation_radians);
        sky_sphere.SetTintRgb(oxygen::Vec3 { sky_sphere_record->tint_rgb[0],
          sky_sphere_record->tint_rgb[1], sky_sphere_record->tint_rgb[2] });
        LOG_F(INFO,
          "SceneLoader: Applied SkySphere environment (solid color source)");
      }

      // Load Fog environment
      if (const auto fog_record = asset->TryGetFogEnvironment();
        fog_record && fog_record->enabled != 0U) {
        auto& fog = environment->AddSystem<scene::environment::Fog>();
        fog.SetModel(
          static_cast<scene::environment::FogModel>(fog_record->model));
        fog.SetDensity(fog_record->density);
        fog.SetHeightFalloff(fog_record->height_falloff);
        fog.SetHeightOffsetMeters(fog_record->height_offset_m);
        fog.SetStartDistanceMeters(fog_record->start_distance_m);
        fog.SetMaxOpacity(fog_record->max_opacity);
        fog.SetAlbedoRgb(oxygen::Vec3 { fog_record->albedo_rgb[0],
          fog_record->albedo_rgb[1], fog_record->albedo_rgb[2] });
        fog.SetAnisotropy(fog_record->anisotropy_g);
        fog.SetScatteringIntensity(fog_record->scattering_intensity);
        LOG_F(INFO, "SceneLoader: Applied Fog environment");
      }

      // Load SkyLight environment
      if (const auto sky_light_record = asset->TryGetSkyLightEnvironment();
        sky_light_record && sky_light_record->enabled != 0U) {
        auto& sky_light
          = environment->AddSystem<scene::environment::SkyLight>();
        sky_light.SetSource(static_cast<scene::environment::SkyLightSource>(
          sky_light_record->source));
        if (sky_light.GetSource()
          == scene::environment::SkyLightSource::kSpecifiedCubemap) {
          LOG_F(INFO,
            "SceneLoader: SkyLight specifies a cubemap AssetKey, but this "
            "example "
            "does not yet resolve it to a ResourceKey. Use the Environment "
            "panel "
            "Skybox Loader to bind a cubemap at runtime.");
        }
        sky_light.SetIntensity(sky_light_record->intensity);
        sky_light.SetTintRgb(oxygen::Vec3 { sky_light_record->tint_rgb[0],
          sky_light_record->tint_rgb[1], sky_light_record->tint_rgb[2] });
        sky_light.SetDiffuseIntensity(sky_light_record->diffuse_intensity);
        sky_light.SetSpecularIntensity(sky_light_record->specular_intensity);
        LOG_F(INFO, "SceneLoader: Applied SkyLight environment");
      }

      // Load VolumetricClouds environment
      if (const auto clouds_record = asset->TryGetVolumetricCloudsEnvironment();
        clouds_record && clouds_record->enabled != 0U) {
        auto& clouds
          = environment->AddSystem<scene::environment::VolumetricClouds>();
        clouds.SetBaseAltitudeMeters(clouds_record->base_altitude_m);
        clouds.SetLayerThicknessMeters(clouds_record->layer_thickness_m);
        clouds.SetCoverage(clouds_record->coverage);
        clouds.SetDensity(clouds_record->density);
        clouds.SetAlbedoRgb(oxygen::Vec3 { clouds_record->albedo_rgb[0],
          clouds_record->albedo_rgb[1], clouds_record->albedo_rgb[2] });
        clouds.SetExtinctionScale(clouds_record->extinction_scale);
        clouds.SetPhaseAnisotropy(clouds_record->phase_g);
        clouds.SetWindDirectionWs(oxygen::Vec3 { clouds_record->wind_dir_ws[0],
          clouds_record->wind_dir_ws[1], clouds_record->wind_dir_ws[2] });
        clouds.SetWindSpeedMps(clouds_record->wind_speed_mps);
        clouds.SetShadowStrength(clouds_record->shadow_strength);
        LOG_F(INFO, "SceneLoader: Applied VolumetricClouds environment");
      }

      // Load PostProcessVolume environment
      if (const auto pp_record = asset->TryGetPostProcessVolumeEnvironment();
        pp_record && pp_record->enabled != 0U) {
        auto& pp
          = environment->AddSystem<scene::environment::PostProcessVolume>();
        pp.SetToneMapper(
          static_cast<scene::environment::ToneMapper>(pp_record->tone_mapper));
        pp.SetExposureMode(static_cast<scene::environment::ExposureMode>(
          pp_record->exposure_mode));
        pp.SetExposureCompensationEv(pp_record->exposure_compensation_ev);
        pp.SetAutoExposureRangeEv(
          pp_record->auto_exposure_min_ev, pp_record->auto_exposure_max_ev);
        pp.SetAutoExposureAdaptationSpeeds(pp_record->auto_exposure_speed_up,
          pp_record->auto_exposure_speed_down);
        pp.SetBloomIntensity(pp_record->bloom_intensity);
        pp.SetBloomThreshold(pp_record->bloom_threshold);
        pp.SetSaturation(pp_record->saturation);
        pp.SetContrast(pp_record->contrast);
        pp.SetVignetteIntensity(pp_record->vignette_intensity);
        LOG_F(INFO, "SceneLoader: Applied PostProcessVolume environment");
      }

      swap_.scene->SetEnvironment(std::move(environment));

      // Instantiate nodes (synchronous part)
      using oxygen::data::pak::DirectionalLightRecord;
      using oxygen::data::pak::NodeRecord;
      using oxygen::data::pak::OrthographicCameraRecord;
      using oxygen::data::pak::PerspectiveCameraRecord;
      using oxygen::data::pak::PointLightRecord;
      using oxygen::data::pak::RenderableRecord;
      using oxygen::data::pak::SpotLightRecord;

      const auto nodes = asset->GetNodes();
      runtime_nodes_.reserve(nodes.size());

      LOG_F(INFO,
        "SceneLoader: Scene summary: nodes={} renderables={} "
        "perspective_cameras={} orthographic_cameras={} "
        "directional_lights={} point_lights={} spot_lights={}",
        nodes.size(), asset->GetComponents<RenderableRecord>().size(),
        asset->GetComponents<PerspectiveCameraRecord>().size(),
        asset->GetComponents<OrthographicCameraRecord>().size(),
        asset->GetComponents<DirectionalLightRecord>().size(),
        asset->GetComponents<PointLightRecord>().size(),
        asset->GetComponents<SpotLightRecord>().size());

      for (size_t i = 0; i < nodes.size(); ++i) {
        const NodeRecord& node = nodes[i];

        std::string_view name_view = asset->GetNodeName(node);
        std::string name;
        if (name_view.empty()) {
          name = "Node" + std::to_string(i);
        } else {
          name.assign(name_view.begin(), name_view.end());
        }

        auto n = swap_.scene->CreateNode(name);
        auto tf = n.GetTransform();
        tf.SetLocalPosition(glm::vec3(
          node.translation[0], node.translation[1], node.translation[2]));
        tf.SetLocalRotation(glm::quat(node.rotation[3], node.rotation[0],
          node.rotation[1], node.rotation[2]));
        tf.SetLocalScale(
          glm::vec3(node.scale[0], node.scale[1], node.scale[2]));

        runtime_nodes_.push_back(std::move(n));
      }

      // Apply hierarchy using parent indices.
      for (size_t i = 0; i < nodes.size(); ++i) {
        const auto parent_index = static_cast<size_t>(nodes[i].parent_index);
        if (parent_index == i) {
          continue;
        }
        if (parent_index >= runtime_nodes_.size()) {
          LOG_F(
            WARNING, "Invalid parent_index {} for node {}", parent_index, i);
          continue;
        }

        const bool ok = swap_.scene->ReparentNode(runtime_nodes_[i],
          runtime_nodes_[parent_index], /*preserve_world_transform=*/false);
        if (!ok) {
          LOG_F(
            WARNING, "Failed to reparent node {} under {}", i, parent_index);
        }
      }

      // Identify renderables and assign geometries (synchronous)
      const auto renderables = asset->GetComponents<RenderableRecord>();
      int valid_renderables = 0;
      for (const RenderableRecord& r : renderables) {
        if (r.visible == 0) {
          continue;
        }
        const auto node_index = static_cast<size_t>(r.node_index);
        if (node_index >= runtime_nodes_.size()) {
          continue;
        }

        // AssetLoader guarantees dependencies are loaded (or placeholders are
        // ready). We retrieve the asset directly to support placeholders and
        // avoid redundant async waits.
        auto geo = loader_.GetGeometryAsset(r.geometry_key);
        if (geo) {
          runtime_nodes_[node_index].GetRenderable().SetGeometry(
            std::move(geo));
          valid_renderables++;
        } else {
          LOG_F(WARNING, "SceneLoader: Missing geometry dependency for node {}",
            node_index);
        }
      }

      if (valid_renderables > 0) {
        LOG_F(INFO, "SceneLoader: Assigned {} geometries from cache.",
          valid_renderables);
      }

      // Instantiate light components (synchronous).
      const auto ApplyCommonLight =
        [](scene::CommonLightProperties& dst,
          const oxygen::data::pak::LightCommonRecord& src) {
          dst.affects_world = (src.affects_world != 0U);
          dst.color_rgb
            = { src.color_rgb[0], src.color_rgb[1], src.color_rgb[2] };
          dst.intensity = src.intensity;
          dst.mobility = static_cast<scene::LightMobility>(src.mobility);
          dst.casts_shadows = (src.casts_shadows != 0U);
          dst.shadow.bias = src.shadow.bias;
          dst.shadow.normal_bias = src.shadow.normal_bias;
          dst.shadow.contact_shadows = (src.shadow.contact_shadows != 0U);
          dst.shadow.resolution_hint = static_cast<scene::ShadowResolutionHint>(
            src.shadow.resolution_hint);
          dst.exposure_compensation_ev = src.exposure_compensation_ev;
        };

      int attached_directional = 0;
      for (const DirectionalLightRecord& rec :
        asset->GetComponents<DirectionalLightRecord>()) {
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index >= runtime_nodes_.size()) {
          continue;
        }

        auto light = std::make_unique<scene::DirectionalLight>();
        ApplyCommonLight(light->Common(), rec.common);
        light->SetAngularSizeRadians(rec.angular_size_radians);
        light->SetEnvironmentContribution(rec.environment_contribution != 0U);
        light->SetIsSunLight(rec.is_sun_light != 0U);

        auto& csm = light->CascadedShadows();
        csm.cascade_count = std::clamp<std::uint32_t>(
          rec.cascade_count, 1U, scene::kMaxShadowCascades);
        for (std::uint32_t i = 0U; i < scene::kMaxShadowCascades; ++i) {
          // NOLINTNEXTLINE(*-pro-bounds-constant-array-index)
          csm.cascade_distances[i] = rec.cascade_distances[i];
        }
        csm.distribution_exponent = rec.distribution_exponent;

        const bool attached
          = runtime_nodes_[node_index].ReplaceLight(std::move(light));
        if (attached) {
          attached_directional++;
        } else {
          LOG_F(WARNING,
            "SceneLoader: Failed to attach DirectionalLight to node_index={}",
            node_index);
        }
      }

      // Ensure a sunlight exists even when the scene asset provides no valid
      // directional light component. Avoid LookAt() here because world
      // transforms are not guaranteed to be available during the
      // load/instantiation phase.
      if (attached_directional == 0) {
        auto sun_node = swap_.scene->CreateNode("Sun");
        auto sun_tf = sun_node.GetTransform();
        sun_tf.SetLocalPosition(glm::vec3(0.0F, 0.0F, 0.0F));

        // Set a natural sun direction (angled, not straight down).
        // Convention: engine forward is -Y and Z-up.
        // We compute a rotation that maps local Forward (-Y) to the desired
        // world-space ray direction (from light toward the scene).
        const glm::vec3 from_dir(0.0F, -1.0F, 0.0F);
        const glm::vec3 to_dir = glm::normalize(glm::vec3(-1.0F, -0.6F, -1.4F));

        const float cos_theta
          = std::clamp(glm::dot(from_dir, to_dir), -1.0F, 1.0F);
        glm::quat sun_rot(1.0F, 0.0F, 0.0F, 0.0F);
        if (cos_theta < 0.9999F) {
          if (cos_theta > -0.9999F) {
            const glm::vec3 axis = glm::normalize(glm::cross(from_dir, to_dir));
            const float angle = std::acos(cos_theta);
            sun_rot = glm::angleAxis(angle, axis);
          } else {
            // Opposite vectors: pick a stable orthogonal axis.
            const glm::vec3 axis = glm::vec3(0.0F, 0.0F, 1.0F);
            sun_rot = glm::angleAxis(glm::pi<float>(), axis);
          }
        }

        sun_tf.SetLocalRotation(sun_rot);

        auto sun_light = std::make_unique<scene::DirectionalLight>();
        sun_light->SetIsSunLight(true);
        sun_light->SetEnvironmentContribution(true);
        sun_light->Common().affects_world = true;
        sun_light->Common().color_rgb = { 1.0F, 0.98F, 0.92F };
        sun_light->Common().intensity = 2.0F;
        sun_light->Common().mobility = scene::LightMobility::kRealtime;
        sun_light->Common().casts_shadows = true;
        sun_light->SetAngularSizeRadians(0.01F);
        sun_light->SetEnvironmentContribution(true);

        const bool attached = sun_node.ReplaceLight(std::move(sun_light));
        if (!attached) {
          LOG_F(WARNING, "SceneLoader: Failed to attach fallback Sun light");
        } else {
          attached_directional++;
        }
      }

      int attached_point = 0;
      for (const PointLightRecord& rec :
        asset->GetComponents<PointLightRecord>()) {
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index >= runtime_nodes_.size()) {
          continue;
        }

        auto light = std::make_unique<scene::PointLight>();
        ApplyCommonLight(light->Common(), rec.common);
        light->SetRange(std::abs(rec.range));
        light->SetAttenuationModel(
          static_cast<scene::AttenuationModel>(rec.attenuation_model));
        light->SetDecayExponent(rec.decay_exponent);
        light->SetSourceRadius(std::abs(rec.source_radius));

        const bool attached
          = runtime_nodes_[node_index].ReplaceLight(std::move(light));
        if (attached) {
          attached_point++;
        } else {
          LOG_F(WARNING,
            "SceneLoader: Failed to attach PointLight to node_index={}",
            node_index);
        }
      }

      int attached_spot = 0;
      for (const SpotLightRecord& rec :
        asset->GetComponents<SpotLightRecord>()) {
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index >= runtime_nodes_.size()) {
          continue;
        }

        auto light = std::make_unique<scene::SpotLight>();
        ApplyCommonLight(light->Common(), rec.common);
        light->SetRange(std::abs(rec.range));
        light->SetAttenuationModel(
          static_cast<scene::AttenuationModel>(rec.attenuation_model));
        light->SetDecayExponent(rec.decay_exponent);
        light->SetConeAnglesRadians(
          rec.inner_cone_angle_radians, rec.outer_cone_angle_radians);
        light->SetSourceRadius(std::abs(rec.source_radius));

        const bool attached
          = runtime_nodes_[node_index].ReplaceLight(std::move(light));
        if (attached) {
          attached_spot++;
        } else {
          LOG_F(WARNING,
            "SceneLoader: Failed to attach SpotLight to node_index={}",
            node_index);
        }
      }

      if (attached_directional + attached_point + attached_spot > 0) {
        LOG_F(INFO,
          "SceneLoader: Attached lights: directional={} point={} spot={} "
          "(total={})",
          attached_directional, attached_point, attached_spot,
          attached_directional + attached_point + attached_spot);
      }

      // Pick or create an active camera.
      const auto perspective_cams
        = asset->GetComponents<PerspectiveCameraRecord>();
      if (!perspective_cams.empty()) {
        LOG_F(INFO, "SceneLoader: Found {} perspective camera(s)",
          perspective_cams.size());
        const auto& rec = perspective_cams.front();
        const auto node_index = static_cast<size_t>(rec.node_index);
        if (node_index < runtime_nodes_.size()) {
          swap_.active_camera = runtime_nodes_[node_index];
          LOG_F(INFO,
            "SceneLoader: Using perspective camera node_index={} name='{}'",
            rec.node_index, swap_.active_camera.GetName().c_str());
          if (!swap_.active_camera.HasCamera()) {
            auto cam = std::make_unique<scene::PerspectiveCamera>();
            const bool attached
              = swap_.active_camera.AttachCamera(std::move(cam));
            CHECK_F(attached,
              "Failed to attach PerspectiveCamera to scene camera node");
          }
          if (auto cam_ref
            = swap_.active_camera.GetCameraAs<scene::PerspectiveCamera>();
            cam_ref) {
            auto& cam = cam_ref->get();
            float near_plane = std::abs(rec.near_plane);
            float far_plane = std::abs(rec.far_plane);
            if (far_plane < near_plane) {
              std::swap(far_plane, near_plane);
            }
            cam.SetFieldOfView(rec.fov_y);

            cam.SetNearPlane(near_plane);
            cam.SetFarPlane(far_plane);

            const float fov_y_deg
              = rec.fov_y * (180.0F / std::numbers::pi_v<float>);
            LOG_F(INFO,
              "SceneLoader: Applied perspective camera params fov_y_deg={} "
              "near={} far={} aspect_hint={}",
              fov_y_deg, near_plane, far_plane, rec.aspect_ratio);

            auto tf = swap_.active_camera.GetTransform();
            glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
            glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };
            if (auto lp = tf.GetLocalPosition()) {
              cam_pos = *lp;
            }
            if (auto lr = tf.GetLocalRotation()) {
              cam_rot = *lr;
            }
            const glm::vec3 forward = cam_rot * glm::vec3(0.0F, 0.0F, -1.0F);
            const glm::vec3 up = cam_rot * glm::vec3(0.0F, 1.0F, 0.0F);
            LOG_F(INFO,
              "SceneLoader: Camera local pose pos=({:.3f}, {:.3f}, {:.3f}) "
              "forward=({:.3f}, {:.3f}, {:.3f}) up=({:.3f}, {:.3f}, {:.3f})",
              cam_pos.x, cam_pos.y, cam_pos.z, forward.x, forward.y, forward.z,
              up.x, up.y, up.z);
          }
        }
      }

      // If no perspective, try ortho
      if (!swap_.active_camera.IsAlive()) {
        const auto ortho_cams
          = asset->GetComponents<OrthographicCameraRecord>();
        if (!ortho_cams.empty()) {
          LOG_F(INFO, "SceneLoader: Found {} orthographic camera(s)",
            ortho_cams.size());
          const auto& rec = ortho_cams.front();
          const auto node_index = static_cast<size_t>(rec.node_index);
          if (node_index < runtime_nodes_.size()) {
            swap_.active_camera = runtime_nodes_[node_index];
            LOG_F(INFO,
              "SceneLoader: Using orthographic camera node_index={} name='{}'",
              rec.node_index, swap_.active_camera.GetName().c_str());
            if (!swap_.active_camera.HasCamera()) {
              auto cam = std::make_unique<scene::OrthographicCamera>();
              const bool attached
                = swap_.active_camera.AttachCamera(std::move(cam));
              CHECK_F(attached,
                "Failed to attach OrthographicCamera to scene camera node");
            }
            if (auto cam_ref
              = swap_.active_camera.GetCameraAs<scene::OrthographicCamera>();
              cam_ref) {
              float near_plane = std::abs(rec.near_plane);
              float far_plane = std::abs(rec.far_plane);
              if (far_plane < near_plane) {
                std::swap(far_plane, near_plane);
              }
              cam_ref->get().SetExtents(rec.left, rec.right, rec.bottom,
                rec.top, near_plane, far_plane);
              LOG_F(INFO,
                "SceneLoader: Applied orthographic camera extents l={} r={} "
                "b={} "
                "t={} near={} far={}",
                rec.left, rec.right, rec.bottom, rec.top, near_plane,
                far_plane);
            }
          }
        }
      }

      // Finalize setup
      const float aspect = height_ > 0
        ? (static_cast<float>(width_) / static_cast<float>(height_))
        : 1.0F;

      const ViewPort viewport { .top_left_x = 0.0F,
        .top_left_y = 0.0F,
        .width = static_cast<float>(width_),
        .height = static_cast<float>(height_),
        .min_depth = 0.0F,
        .max_depth = 1.0F };

      // Ensure we have a camera if none was found in the scene
      if (!swap_.active_camera.IsAlive()) {
        swap_.active_camera = swap_.scene->CreateNode("MainCamera");
        // Stable, elevated pose: look at origin with Z-up.
        const glm::vec3 cam_pos(10.0F, 10.0F, 10.0F);
        const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
        auto tf = swap_.active_camera.GetTransform();
        tf.SetLocalPosition(cam_pos);
        tf.SetLocalRotation(MakeLookRotationFromPosition(cam_pos, cam_target));
        LOG_F(INFO,
          "SceneLoader: No camera in scene; created fallback camera '{}'",
          swap_.active_camera.GetName().c_str());
      }

      if (!swap_.active_camera.HasCamera()) {
        auto camera = std::make_unique<scene::PerspectiveCamera>();
        swap_.active_camera.AttachCamera(std::move(camera));
      }

      // Apply viewport to the active camera
      if (auto cam_ref
        = swap_.active_camera.GetCameraAs<scene::PerspectiveCamera>();
        cam_ref) {
        auto& cam = cam_ref->get();
        cam.SetAspectRatio(aspect);
        cam.SetViewport(viewport);
      } else if (auto ortho_ref
        = swap_.active_camera.GetCameraAs<scene::OrthographicCamera>();
        ortho_ref) {
        ortho_ref->get().SetViewport(viewport);
      }

      // Dump the runtime scene hierarchy (once per load).
      LOG_F(INFO, "SceneLoader: Runtime scene hierarchy:");
      std::unordered_set<scene::NodeHandle> visited_nodes;
      visited_nodes.reserve(runtime_nodes_.size());
      const auto PrintNodeLine = [](scene::SceneNode& node, const int depth) {
        const std::string indent(static_cast<size_t>(depth * 2), ' ');
        const bool has_renderable = node.GetRenderable().HasGeometry();
        const bool has_camera = node.HasCamera();
        const bool has_light = node.HasLight();
        LOG_F(INFO, "{}- {}{}{}{}", indent, node.GetName().c_str(),
          has_renderable ? " [R]" : "", has_camera ? " [C]" : "",
          has_light ? " [L]" : "");
      };

      const auto PrintSubtree = [&](const auto& self, scene::SceneNode node,
                                  const int depth) -> void {
        if (!node.IsAlive()) {
          return;
        }

        visited_nodes.insert(node.GetHandle());
        PrintNodeLine(node, depth);

        auto child = node.GetFirstChild();
        while (child) {
          self(self, *child, depth + 1);
          child = child->GetNextSibling();
        }
      };

      for (auto& root : swap_.scene->GetRootNodes()) {
        PrintSubtree(PrintSubtree, root, 0);
      }

      if (visited_nodes.size() != runtime_nodes_.size()) {
        LOG_F(WARNING,
          "SceneLoader: Hierarchy traversal visited {} of {} nodes.",
          visited_nodes.size(), runtime_nodes_.size());
        for (auto& node : runtime_nodes_) {
          if (!node.IsAlive() || visited_nodes.contains(node.GetHandle())) {
            continue;
          }

          const bool has_renderable = node.GetRenderable().HasGeometry();
          const bool has_camera = node.HasCamera();
          const bool has_light = node.HasLight();
          LOG_F(WARNING, "SceneLoader: Unvisited node: {}{}{}{}",
            node.GetName().c_str(), has_renderable ? " [R]" : "",
            has_camera ? " [C]" : "", has_light ? " [L]" : "");
        }
      } else {
        LOG_F(INFO, "SceneLoader: Hierarchy traversal covered all {} nodes.",
          runtime_nodes_.size());
      }

      ready_ = true;
      LOG_F(INFO,
        "SceneLoader: Scene loading and instantiation complete. Ready for "
        "swap.");
    } catch (const std::exception& ex) {
      LOG_F(
        ERROR, "SceneLoader: Exception while building scene: {}", ex.what());
      swap_ = {};
      runtime_nodes_.clear();
      ready_ = false;
      failed_ = true;
    } catch (...) {
      LOG_F(ERROR, "SceneLoader: Unknown exception while building scene");
      swap_ = {};
      runtime_nodes_.clear();
      ready_ = false;
      failed_ = true;
    }
  }

  oxygen::content::AssetLoader& loader_;
  int width_;
  int height_;
  MainModule::PendingSceneSwap swap_;
  std::vector<scene::SceneNode> runtime_nodes_;
  bool ready_ { false };
  bool failed_ { false };
  bool consumed_ { false };
  int linger_frames_ { 0 };
};

MainModule::MainModule(const oxygen::examples::common::AsyncEngineApp& app)
  : Base(app)
{
  content_root_ = FindRenderSceneContentRoot();
}

MainModule::~MainModule() = default;

auto MainModule::BuildDefaultWindowProperties() const
  -> platform::window::Properties
{
  platform::window::Properties p("Oxygen Example");
  p.extent = { .width = 2560U, .height = 1400 };
  p.flags = { .hidden = false,
    .always_on_top = false,
    .full_screen = app_.fullscreen,
    .maximized = false,
    .minimized = false,
    .resizable = true,
    .borderless = false };
  return p;
}

auto MainModule::OnAttached(
  oxygen::observer_ptr<oxygen::AsyncEngine> engine) noexcept -> bool
{
  if (!engine) {
    return false;
  }

  if (!Base::OnAttached(engine)) {
    return false;
  }

  LOG_F(WARNING, "RenderScene: OnAttached; input_system={} engine={}",
    static_cast<const void*>(app_.input_system.get()),
    static_cast<const void*>(engine.get()));

  if (!InitInputBindings()) {
    LOG_F(WARNING, "RenderScene: InitInputBindings failed");
    return false;
  }

  // Ensure the correct mapping context is active for the initial mode.
  UpdateActiveCameraInputContext();

  content_root_ = FindRenderSceneContentRoot();

  // Initialize UI panels
  InitializeUIPanels();

  LOG_F(WARNING, "RenderScene: InitInputBindings ok");
  return true;
}

void MainModule::OnShutdown() noexcept
{
  content_loader_panel_.GetImportPanel().CancelImport();
  ReleaseCurrentSceneAsset("module shutdown");
  ClearSceneRuntime("module shutdown");
  Base::OnShutdown();
}

auto MainModule::OnFrameStart(oxygen::engine::FrameContext& context) -> void
{
  Base::OnFrameStart(context);
}

auto MainModule::OnExampleFrameStart(engine::FrameContext& context) -> void
{
  if (scene_loader_) {
    if (scene_loader_->IsReady()) {
      auto loader = scene_loader_;
      auto swap = scene_loader_->GetResult();
      LOG_F(WARNING, "RenderScene: Applying staged scene swap (scene_key={})",
        oxygen::data::to_string(swap.scene_key));
      ReleaseCurrentSceneAsset("scene swap");
      ClearSceneRuntime("scene swap");

      scene_ = std::move(swap.scene);
      active_camera_ = std::move(swap.active_camera);
      current_scene_key_ = swap.scene_key;
      if (active_camera_.IsAlive()) {
        // Store initial camera pose for reset functionality
        auto tf = active_camera_.GetTransform();
        if (auto pos = tf.GetLocalPosition()) {
          initial_camera_position_ = *pos;
        }
        if (auto rot = tf.GetLocalRotation()) {
          initial_camera_rotation_ = *rot;
        }

        orbit_controller_ = std::make_unique<OrbitCameraController>();
        orbit_controller_->SyncFromTransform(active_camera_);
        fly_controller_ = std::make_unique<FlyCameraController>();
        fly_controller_->SetLookSensitivity(0.0015f);
        fly_controller_->SyncFromTransform(active_camera_);
        UpdateCameraControlPanelConfig();
      }
      registered_view_camera_ = scene::NodeHandle();
      scene_loader_ = std::move(loader);
      if (scene_loader_) {
        scene_loader_->MarkConsumed();
      }
    } else if (scene_loader_->IsFailed()) {
      LOG_F(ERROR, "RenderScene: Scene loading failed");
      scene_loader_.reset();
    } else if (scene_loader_->IsConsumed()) {
      if (scene_loader_->Tick()) {
        scene_loader_.reset();
      }
    }
  }

  if (!scene_) {
    scene_ = std::make_shared<scene::Scene>("RenderScene");
  }

  // Keep the skybox helper bound to the current scene.
  if (skybox_manager_scene_ != scene_.get()) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      skybox_manager_ = std::make_unique<SkyboxManager>(
        observer_ptr { asset_loader.get() }, scene_);
      skybox_manager_scene_ = scene_.get();
    } else {
      skybox_manager_.reset();
      skybox_manager_scene_ = nullptr;
    }
  }
  context.SetScene(observer_ptr { scene_.get() });
}

auto MainModule::OnSceneMutation(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  DCHECK_NOTNULL_F(scene_);

  UpdateFrameContext(context, [this, &context](int w, int h) {
    last_viewport_w_ = w;
    last_viewport_h_ = h;
    EnsureActiveCameraViewport(w, h);

    if (pending_sync_active_camera_ && active_camera_.IsAlive()) {
      if (camera_mode_ == CameraMode::kOrbit && orbit_controller_) {
        orbit_controller_->SyncFromTransform(active_camera_);
      } else if (camera_mode_ == CameraMode::kFly && fly_controller_) {
        fly_controller_->SyncFromTransform(active_camera_);
      }

      pending_sync_active_camera_ = false;
    }

    // Process deferred camera reset
    if (pending_reset_camera_ && active_camera_.IsAlive()) {
      auto transform = active_camera_.GetTransform();
      transform.SetLocalPosition(initial_camera_position_);
      transform.SetLocalRotation(initial_camera_rotation_);

      if (camera_mode_ == CameraMode::kOrbit && orbit_controller_) {
        orbit_controller_->SyncFromTransform(active_camera_);
      } else if (camera_mode_ == CameraMode::kFly && fly_controller_) {
        fly_controller_->SyncFromTransform(active_camera_);
      }

      pending_reset_camera_ = false;
      LOG_F(INFO, "Camera reset to initial pose");
    }
  });
  if (!app_window_->GetWindow()) {
    co_return;
  }

  // Panel updates happen here before scene loading
  UpdateUIPanels();

  // Handle skybox load requests from the environment debug panel.
  if (scene_ && skybox_manager_) {
    if (auto req = environment_debug_panel_.TakeSkyboxLoadRequest()) {
      auto result
        = co_await skybox_manager_->LoadSkyboxAsync(req->path, req->options);

      environment_debug_panel_.SetSkyboxLoadStatus(
        result.status_message, result.face_size, result.resource_key);

      if (result.success) {
        skybox_manager_->ApplyToScene(
          environment_debug_panel_.GetSkyLightParams());
        environment_debug_panel_.RequestResync();
      }
    }
  }

  if (pending_load_scene_) {
    pending_load_scene_ = false;

    if (pending_scene_key_) {
      ReleaseCurrentSceneAsset("scene load request");
      ClearSceneRuntime("scene load request");
      auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
      if (asset_loader) {
        scene_loader_ = std::make_shared<SceneLoader>(
          *asset_loader, last_viewport_w_, last_viewport_h_);
        scene_loader_->Start(*pending_scene_key_);
        LOG_F(WARNING, "RenderScene: Started async scene load (scene_key={})",
          oxygen::data::to_string(*pending_scene_key_));
      } else {
        LOG_F(ERROR, "AssetLoader unavailable");
      }
    }
  }

  co_return;
}

auto MainModule::ReleaseCurrentSceneAsset(const char* reason) -> void
{
  if (!current_scene_key_.has_value()) {
    return;
  }

  auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
  if (!asset_loader) {
    last_released_scene_key_ = current_scene_key_;
    current_scene_key_.reset();
    return;
  }

  LOG_F(INFO, "RenderScene: Releasing scene asset (reason={} key={})", reason,
    oxygen::data::to_string(*current_scene_key_));
  last_released_scene_key_ = current_scene_key_;
  (void)asset_loader->ReleaseAsset(*current_scene_key_);
  current_scene_key_.reset();
}

auto MainModule::ClearSceneRuntime(const char* reason) -> void
{
  UnregisterViewForRendering(reason);
  scene_.reset();
  scene_loader_.reset();
  active_camera_ = {};
  registered_view_camera_ = scene::NodeHandle();
  orbit_controller_.reset();
  fly_controller_.reset();
  skybox_manager_.reset();
  skybox_manager_scene_ = nullptr;
  pending_sync_active_camera_ = false;
  pending_reset_camera_ = false;
  UpdateCameraControlPanelConfig();
}

auto MainModule::OnGameplay(engine::FrameContext& context) -> co::Co<>
{
  if (!logged_gameplay_tick_) {
    logged_gameplay_tick_ = true;
    LOG_F(WARNING, "RenderScene: OnGameplay is running");
  }

  // Input edges are finalized during kInput earlier in the frame (mirrors the
  // InputSystem example). Apply camera controls here so WASD/Shift/Space and
  // mouse deltas are visible in the same frame.
  ApplyOrbitAndZoom(context.GetGameDeltaTime());

  co_return;
}

auto MainModule::InitInputBindings() noexcept -> bool
{
  using oxygen::input::Action;
  using oxygen::input::ActionTriggerChain;
  using oxygen::input::ActionTriggerDown;
  using oxygen::input::ActionTriggerPulse;
  using oxygen::input::ActionTriggerTap;
  using oxygen::input::ActionValueType;
  using oxygen::input::InputActionMapping;
  using oxygen::input::InputMappingContext;
  using oxygen::platform::InputSlots;

  if (!app_.input_system) {
    LOG_F(WARNING, "RenderScene: InputSystem not available; no input bindings");
    return false;
  }

  LOG_F(WARNING, "RenderScene: Creating camera input actions");

  zoom_in_action_ = std::make_shared<Action>("zoom in", ActionValueType::kBool);
  zoom_out_action_
    = std::make_shared<Action>("zoom out", ActionValueType::kBool);
  rmb_action_ = std::make_shared<Action>("rmb", ActionValueType::kBool);
  orbit_action_
    = std::make_shared<Action>("camera orbit", ActionValueType::kAxis2D);
  move_fwd_action_
    = std::make_shared<Action>("move fwd", ActionValueType::kBool);
  move_bwd_action_
    = std::make_shared<Action>("move bwd", ActionValueType::kBool);
  move_left_action_
    = std::make_shared<Action>("move left", ActionValueType::kBool);
  move_right_action_
    = std::make_shared<Action>("move right", ActionValueType::kBool);
  move_up_action_ = std::make_shared<Action>("move up", ActionValueType::kBool);
  move_down_action_
    = std::make_shared<Action>("move down", ActionValueType::kBool);
  fly_plane_lock_action_
    = std::make_shared<Action>("fly plane lock", ActionValueType::kBool);
  fly_boost_action_
    = std::make_shared<Action>("fly boost", ActionValueType::kBool);

  app_.input_system->AddAction(zoom_in_action_);
  app_.input_system->AddAction(zoom_out_action_);
  app_.input_system->AddAction(rmb_action_);
  app_.input_system->AddAction(orbit_action_);
  app_.input_system->AddAction(move_fwd_action_);
  app_.input_system->AddAction(move_bwd_action_);
  app_.input_system->AddAction(move_left_action_);
  app_.input_system->AddAction(move_right_action_);
  app_.input_system->AddAction(move_up_action_);
  app_.input_system->AddAction(move_down_action_);
  app_.input_system->AddAction(fly_plane_lock_action_);
  app_.input_system->AddAction(fly_boost_action_);

  LOG_F(
    WARNING, "RenderScene: Added actions (zoom_in/zoom_out/rmb/orbit/move)");

  // Orbit-only mapping context: wheel zoom + orbit/look (MouseXY gated by RMB)
  orbit_controls_ctx_ = std::make_shared<InputMappingContext>("camera orbit");
  {
    // Zoom in: Mouse wheel up
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_in_action_, InputSlots::MouseWheelUp);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // Zoom out: Mouse wheel down
    {
      const auto trigger = std::make_shared<ActionTriggerTap>();
      trigger->SetTapTimeThreshold(0.25F);
      trigger->MakeExplicit();
      const auto mapping = std::make_shared<InputActionMapping>(
        zoom_out_action_, InputSlots::MouseWheelDown);
      mapping->AddTrigger(trigger);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // RMB helper mapping
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      orbit_controls_ctx_->AddMapping(mapping);
    }

    // Orbit mapping: MouseXY with an implicit chain requiring RMB.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      orbit_controls_ctx_->AddMapping(mapping);
    }
  }

  // Fly-only mapping context: keyboard movement + mouse-look (MouseXY gated by
  // RMB). We keep the same actions, but isolate the mappings.
  fly_controls_ctx_ = std::make_shared<InputMappingContext>("camera fly");
  {
    // RMB helper mapping (shared action)
    {
      const auto trig_down = std::make_shared<ActionTriggerDown>();
      trig_down->MakeExplicit();
      trig_down->SetActuationThreshold(0.1F);
      const auto mapping = std::make_shared<InputActionMapping>(
        rmb_action_, InputSlots::RightMouseButton);
      mapping->AddTrigger(trig_down);
      fly_controls_ctx_->AddMapping(mapping);
    }

    // Mouse look mapping: MouseXY with RMB prerequisite.
    {
      const auto trig_move = std::make_shared<ActionTriggerDown>();
      trig_move->MakeExplicit();
      trig_move->SetActuationThreshold(0.0F);

      const auto rmb_chain = std::make_shared<ActionTriggerChain>();
      rmb_chain->SetLinkedAction(rmb_action_);
      rmb_chain->MakeImplicit();
      rmb_chain->RequirePrerequisiteHeld(true);

      const auto mapping = std::make_shared<InputActionMapping>(
        orbit_action_, InputSlots::MouseXY);
      mapping->AddTrigger(trig_move);
      mapping->AddTrigger(rmb_chain);
      fly_controls_ctx_->AddMapping(mapping);
    }

    auto add_bool_mapping = [&](const std::shared_ptr<Action>& action,
                              const auto& slot) {
      const auto mapping = std::make_shared<InputActionMapping>(action, slot);
      const auto trigger = std::make_shared<ActionTriggerPulse>();
      trigger->MakeExplicit();
      trigger->SetActuationThreshold(0.1F);
      mapping->AddTrigger(trigger);
      fly_controls_ctx_->AddMapping(mapping);
    };

    add_bool_mapping(move_fwd_action_, InputSlots::W);
    add_bool_mapping(move_bwd_action_, InputSlots::S);
    add_bool_mapping(move_left_action_, InputSlots::A);
    add_bool_mapping(move_right_action_, InputSlots::D);
    add_bool_mapping(move_up_action_, InputSlots::E);
    add_bool_mapping(move_down_action_, InputSlots::Q);
    add_bool_mapping(fly_plane_lock_action_, InputSlots::Space);
    add_bool_mapping(fly_boost_action_, InputSlots::LeftShift);
  }

  // Register both contexts; only one will be active at a time.
  app_.input_system->AddMappingContext(orbit_controls_ctx_, 10);
  app_.input_system->AddMappingContext(fly_controls_ctx_, 10);
  UpdateActiveCameraInputContext();

  LOG_F(WARNING,
    "RenderScene: Registered camera input contexts (orbit+fly) priority={} ",
    10);

  return true;
}

auto MainModule::ApplyOrbitAndZoom(time::CanonicalDuration delta_time) -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  if (camera_mode_ == CameraMode::kOrbit) {
    if (!orbit_controller_) {
      return;
    }

    // Zoom via mouse wheel actions
    if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(1.0f);
    }
    if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
      orbit_controller_->AddZoomInput(-1.0f);
    }

    // Orbit via MouseXY deltas for this frame
    if (orbit_action_
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      glm::vec2 orbit_delta(0.0f);
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        orbit_delta.x += v.x;
        orbit_delta.y += v.y;
      }

      if (std::abs(orbit_delta.x) > 0.0f || std::abs(orbit_delta.y) > 0.0f) {
        orbit_controller_->AddOrbitInput(orbit_delta);
      }
    }

    orbit_controller_->Update(active_camera_, delta_time);
  } else if (camera_mode_ == CameraMode::kFly) {
    if (!fly_controller_) {
      return;
    }

    if (fly_boost_action_) {
      fly_controller_->SetBoostActive(fly_boost_action_->IsOngoing());
    }
    if (fly_plane_lock_action_) {
      fly_controller_->SetPlaneLockActive(fly_plane_lock_action_->IsOngoing());
    }

    // Zoom via mouse wheel actions (adjust speed)
    if (zoom_in_action_ && zoom_in_action_->WasTriggeredThisFrame()) {
      const float speed = fly_controller_->GetMoveSpeed();
      fly_controller_->SetMoveSpeed(std::min(speed * 1.2f, 1000.0f));
    }
    if (zoom_out_action_ && zoom_out_action_->WasTriggeredThisFrame()) {
      const float speed = fly_controller_->GetMoveSpeed();
      fly_controller_->SetMoveSpeed(std::max(speed / 1.2f, 0.1f));
    }

    // Look via MouseXY deltas
    if (orbit_action_
      && orbit_action_->GetValueType()
        == oxygen::input::ActionValueType::kAxis2D) {
      glm::vec2 look_delta(0.0f);
      for (const auto& tr : orbit_action_->GetFrameTransitions()) {
        const auto& v = tr.value_at_transition.GetAs<oxygen::Axis2D>();
        look_delta.x += v.x;
        look_delta.y += v.y;
      }

      if (std::abs(look_delta.x) > 0.0f || std::abs(look_delta.y) > 0.0f) {
        fly_controller_->AddRotationInput(look_delta);
      }
    }

    // Move via WASD/QE
    glm::vec3 move_input(0.0f);
    if (move_fwd_action_ && move_fwd_action_->IsOngoing()) {
      move_input.z += 1.0f;
    }
    if (move_bwd_action_ && move_bwd_action_->IsOngoing()) {
      move_input.z -= 1.0f;
    }
    if (move_left_action_ && move_left_action_->IsOngoing()) {
      move_input.x -= 1.0f;
    }
    if (move_right_action_ && move_right_action_->IsOngoing()) {
      move_input.x += 1.0f;
    }
    if (move_up_action_ && move_up_action_->IsOngoing()) {
      move_input.y += 1.0f;
    }
    if (move_down_action_ && move_down_action_->IsOngoing()) {
      move_input.y -= 1.0f;
    }

    if (glm::length(move_input) > 0.0f) {
      fly_controller_->AddMovementInput(move_input);
    }

    fly_controller_->Update(active_camera_, delta_time);
  }
}

auto MainModule::EnsureViewCameraRegistered() -> void
{
  if (!active_camera_.IsAlive()) {
    return;
  }

  const auto camera_handle = active_camera_.GetHandle();
  if (registered_view_camera_ != camera_handle) {
    registered_view_camera_ = camera_handle;
    UnregisterViewForRendering("camera changed");
    LOG_F(WARNING, "RenderScene: Active camera changed; re-registering view");
  }

  RegisterViewForRendering(active_camera_);
}

auto MainModule::OnGuiUpdate(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);
  if (!app_window_->GetWindow()) {
    co_return;
  }
  auto imgui_module_ref
    = app_.engine ? app_.engine->GetModule<imgui::ImGuiModule>() : std::nullopt;

  if (!imgui_module_ref) {
    co_return;
  }
  auto& imgui_module = imgui_module_ref->get();
  if (!imgui_module.IsWitinFrameScope()) {
    co_return;
  }
  auto* imgui_context = imgui_module.GetImGuiContext();
  if (imgui_context == nullptr) {
    co_return;
  }
  ImGui::SetCurrentContext(imgui_context);

  DrawUI();
  co_return;
}

auto MainModule::OnPreRender(engine::FrameContext& context) -> co::Co<>
{
  DCHECK_NOTNULL_F(app_window_);

  if (auto imgui_module_ref = app_.engine->GetModule<imgui::ImGuiModule>()) {
    auto& imgui_module = imgui_module_ref->get();
    if (auto* imgui_context = imgui_module.GetImGuiContext()) {
      ImGui::SetCurrentContext(imgui_context);
    }
  }

  if (auto rg = GetRenderGraph(); rg) {
    rg->SetupRenderPasses();
  }

  EnsureViewCameraRegistered();
  co_return;
}

auto MainModule::OnCompositing(engine::FrameContext& context) -> co::Co<>
{
  MarkSurfacePresentable(context);
  co_return;
}

auto MainModule::OnFrameEnd(engine::FrameContext& context) -> void
{
  Base::OnFrameEnd(context);
}

auto MainModule::EnsureFallbackCamera(const int width, const int height) -> void
{
  using scene::PerspectiveCamera;

  if (!scene_) {
    return;
  }

  if (!active_camera_.IsAlive()) {
    active_camera_ = scene_->CreateNode("MainCamera");

    // Camera at -Y axis looking at origin with Z-up.
    // User is at (0, -15, 0) watching the scene at origin.
    const glm::vec3 cam_pos(0.0F, -15.0F, 0.0F);
    const glm::vec3 cam_target(0.0F, 0.0F, 0.0F);
    const glm::quat cam_rot = MakeLookRotationFromPosition(cam_pos, cam_target);

    auto tf = active_camera_.GetTransform();
    tf.SetLocalPosition(cam_pos);
    tf.SetLocalRotation(cam_rot);

    initial_camera_position_ = cam_pos;
    initial_camera_target_ = cam_target;
    initial_camera_rotation_ = cam_rot;

    orbit_controller_ = std::make_unique<OrbitCameraController>();
    orbit_controller_->SyncFromTransform(active_camera_);
    fly_controller_ = std::make_unique<FlyCameraController>();
    fly_controller_->SyncFromTransform(active_camera_);
    UpdateCameraControlPanelConfig();
  }

  if (!active_camera_.HasCamera()) {
    auto camera = std::make_unique<PerspectiveCamera>();
    const bool attached = active_camera_.AttachCamera(std::move(camera));
    CHECK_F(attached, "Failed to attach PerspectiveCamera to MainCamera");
  }

  EnsureActiveCameraViewport(width, height);
}

auto MainModule::EnsureActiveCameraViewport(const int width, const int height)
  -> void
{
  if (!active_camera_.IsAlive()) {
    EnsureFallbackCamera(width, height);
    return;
  }

  const float aspect = height > 0
    ? (static_cast<float>(width) / static_cast<float>(height))
    : 1.0F;

  const ViewPort viewport { .top_left_x = 0.0F,
    .top_left_y = 0.0F,
    .width = static_cast<float>(width),
    .height = static_cast<float>(height),
    .min_depth = 0.0F,
    .max_depth = 1.0F };

  if (auto cam_ref = active_camera_.GetCameraAs<scene::PerspectiveCamera>();
    cam_ref) {
    auto& cam = cam_ref->get();
    cam.SetAspectRatio(aspect);
    cam.SetViewport(viewport);
    return;
  }

  if (auto ortho_ref = active_camera_.GetCameraAs<scene::OrthographicCamera>();
    ortho_ref) {
    ortho_ref->get().SetViewport(viewport);
    return;
  }

  EnsureFallbackCamera(width, height);
}

auto MainModule::InitializeUIPanels() -> void
{
  // Configure content loader panel
  ui::ContentLoaderPanel::Config loader_config;
  loader_config.content_root = content_root_;
  loader_config.on_scene_load_requested = [this](const data::AssetKey& key) {
    pending_scene_key_ = key;
    pending_load_scene_ = true;
  };
  loader_config.on_dump_texture_memory = [this](const std::size_t top_n) {
    if (auto* renderer = ResolveRenderer()) {
      renderer->DumpEstimatedTextureMemory(top_n);
    }
  };
  loader_config.get_last_released_scene_key
    = [this]() { return last_released_scene_key_; };
  loader_config.on_force_trim = [this]() {
    ReleaseCurrentSceneAsset("force trim");
    ClearSceneRuntime("force trim");
  };
  loader_config.on_pak_mounted = [this](const std::filesystem::path& path) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      ReleaseCurrentSceneAsset("pak mounted");
      ClearSceneRuntime("pak mounted");
      asset_loader->ClearMounts();
      asset_loader->AddPakFile(path);
    }
  };
  loader_config.on_loose_index_loaded = [this](
                                          const std::filesystem::path& path) {
    auto asset_loader = app_.engine ? app_.engine->GetAssetLoader() : nullptr;
    if (asset_loader) {
      ReleaseCurrentSceneAsset("loose cooked root");
      ClearSceneRuntime("loose cooked root");
      asset_loader->ClearMounts();
      asset_loader->AddLooseCookedRoot(path.parent_path());
    }
  };
  content_loader_panel_.Initialize(loader_config);

  // Configure camera control panel
  UpdateCameraControlPanelConfig();

  // Configure light culling debug panel
  if (auto render_graph = GetRenderGraph()) {
    ui::LightCullingDebugConfig debug_config;
    debug_config.shader_pass_config = render_graph->GetShaderPassConfig().get();
    debug_config.light_culling_pass_config
      = render_graph->GetLightCullingPassConfig().get();
    debug_config.initial_mode = ui::ShaderDebugMode::kDisabled;

    // Callback to invalidate PSO when cluster mode changes
    debug_config.on_cluster_mode_changed = [this]() {
      // The LightCullingPass will detect the config change via
      // NeedRebuildPipelineState() and rebuild on next frame
      LOG_F(INFO, "Light culling mode changed, PSO will rebuild next frame");
    };

    light_culling_debug_panel_.Initialize(debug_config);
  }

  // Configure environment debug panel
  ui::EnvironmentDebugConfig env_config;
  env_config.scene = scene_;
  auto* renderer = ResolveRenderer();
  if (renderer) {
    env_config.renderer = renderer;
  }
  env_config.on_atmosphere_params_changed = [renderer]() {
    LOG_F(INFO, "Atmosphere parameters changed, LUTs will regenerate");
    if (renderer) {
      if (auto lut_mgr = renderer->GetSkyAtmosphereLutManager()) {
        lut_mgr->MarkDirty();
      }
    }
  };
  env_config.on_exposure_changed
    = []() { LOG_F(INFO, "Exposure settings changed"); };
  environment_debug_panel_.Initialize(env_config);
}

auto MainModule::UpdateCameraControlPanelConfig() -> void
{
  ui::CameraControlConfig camera_config;
  camera_config.active_camera = &active_camera_;
  camera_config.orbit_controller = orbit_controller_.get();
  camera_config.fly_controller = fly_controller_.get();
  camera_config.move_fwd_action = move_fwd_action_.get();
  camera_config.move_bwd_action = move_bwd_action_.get();
  camera_config.move_left_action = move_left_action_.get();
  camera_config.move_right_action = move_right_action_.get();
  camera_config.fly_boost_action = fly_boost_action_.get();
  camera_config.fly_plane_lock_action = fly_plane_lock_action_.get();
  camera_config.rmb_action = rmb_action_.get();
  camera_config.orbit_action = orbit_action_.get();

  camera_config.on_mode_changed = [this](ui::CameraControlMode mode) {
    camera_mode_ = (mode == ui::CameraControlMode::kOrbit) ? CameraMode::kOrbit
                                                           : CameraMode::kFly;
    UpdateActiveCameraInputContext();
    pending_sync_active_camera_ = true;
  };

  camera_config.on_reset_requested = [this]() { ResetCameraToInitialPose(); };

  camera_control_panel_.UpdateConfig(camera_config);

  // Sync mode
  const auto ui_mode = (camera_mode_ == CameraMode::kOrbit)
    ? ui::CameraControlMode::kOrbit
    : ui::CameraControlMode::kFly;
  camera_control_panel_.SetMode(ui_mode);
}

auto MainModule::UpdateUIPanels() -> void
{
  content_loader_panel_.Update();

  // Update light culling debug panel config if render graph exists
  if (auto render_graph = GetRenderGraph()) {
    ui::LightCullingDebugConfig debug_config;
    debug_config.shader_pass_config = render_graph->GetShaderPassConfig().get();
    debug_config.light_culling_pass_config
      = render_graph->GetLightCullingPassConfig().get();
    debug_config.initial_mode = light_culling_debug_panel_.GetDebugMode();

    // Callback to invalidate PSO when cluster mode changes
    debug_config.on_cluster_mode_changed = [this]() {
      LOG_F(INFO, "Light culling mode changed, PSO will rebuild next frame");
    };

    light_culling_debug_panel_.UpdateConfig(debug_config);
  }

  // Update environment debug panel when scene is available
  if (scene_) {
    ui::EnvironmentDebugConfig env_config;
    env_config.scene = scene_;
    auto* renderer = ResolveRenderer();
    env_config.renderer = renderer;
    // IMPORTANT: Re-set the callbacks (they get cleared if not set)
    env_config.on_atmosphere_params_changed = [renderer]() {
      LOG_F(INFO, "Atmosphere parameters changed, LUTs will regenerate");
      if (renderer) {
        if (auto lut_mgr = renderer->GetSkyAtmosphereLutManager()) {
          lut_mgr->MarkDirty();
        }
      }
    };
    env_config.on_exposure_changed
      = []() { LOG_F(INFO, "Exposure settings changed"); };
    environment_debug_panel_.UpdateConfig(env_config);

    // Apply any pending UI changes to the scene during mutation phase
    if (environment_debug_panel_.HasPendingChanges()) {
      environment_debug_panel_.ApplyPendingChanges();
    }
  }
}

auto MainModule::DrawUI() -> void
{
  content_loader_panel_.Draw();
  camera_control_panel_.Draw();
  light_culling_debug_panel_.Draw();
  environment_debug_panel_.Draw();

  if (auto render_graph = GetRenderGraph()) {
    auto shader_pass_config = render_graph->GetShaderPassConfig();
    auto transparent_pass_config = render_graph->GetTransparentPassConfig();
    if (shader_pass_config && transparent_pass_config) {
      using graphics::FillMode;
      const bool is_wireframe
        = shader_pass_config->fill_mode == FillMode::kWireFrame;
      bool use_wireframe = is_wireframe;

      if (ImGui::Begin("Render Mode")) {
        ImGui::TextUnformatted("Rasterization");
        if (ImGui::RadioButton("Solid", !use_wireframe)) {
          use_wireframe = false;
        }
        if (ImGui::RadioButton("Wireframe", use_wireframe)) {
          use_wireframe = true;
        }
      }
      ImGui::End();

      if (use_wireframe != is_wireframe) {
        const auto mode
          = use_wireframe ? FillMode::kWireFrame : FillMode::kSolid;
        shader_pass_config->fill_mode = mode;
        transparent_pass_config->fill_mode = mode;
      }
    }
  }

  // Draw axes widget with current camera view matrix
  if (active_camera_.IsAlive()) {
    // Compute view matrix from camera transform
    glm::vec3 cam_pos { 0.0F, 0.0F, 0.0F };
    glm::quat cam_rot { 1.0F, 0.0F, 0.0F, 0.0F };

    const auto& tf = active_camera_.GetTransform();
    if (auto wp = tf.GetWorldPosition()) {
      cam_pos = *wp;
    } else if (auto lp = tf.GetLocalPosition()) {
      cam_pos = *lp;
    }
    if (auto wr = tf.GetWorldRotation()) {
      cam_rot = *wr;
    } else if (auto lr = tf.GetLocalRotation()) {
      cam_rot = *lr;
    }

    // Engine view-space conventions: Forward = -Z, Up = +Y
    // (from space::look in Constants.h)
    constexpr glm::vec3 kViewForward { 0.0F, 0.0F, -1.0F };
    constexpr glm::vec3 kViewUp { 0.0F, 1.0F, 0.0F };
    const glm::vec3 forward = cam_rot * kViewForward;
    const glm::vec3 up = cam_rot * kViewUp;
    const glm::mat4 view_matrix = glm::lookAt(cam_pos, cam_pos + forward, up);

    axes_widget_.Draw(view_matrix);
  }
}

auto MainModule::ResetCameraToInitialPose() -> void
{
  // Defer the actual reset to OnSceneMutation when transforms are valid
  pending_reset_camera_ = true;
}

} // namespace oxygen::examples::render_scene
