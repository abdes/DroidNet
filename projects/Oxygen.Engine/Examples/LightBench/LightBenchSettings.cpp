//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cmath>
#include <cstddef>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "LightBench/LightBenchSettings.h"
#include "LightBench/LightScene.h"
#include "LightBench/ReferenceScene.h"
#include "LightBenchSettings_schema.h"
#include <glm/ext/quaternion_geometric.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Core/Constants.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Scene/Camera/Orthographic.h>
#include <Oxygen/Scene/Environment/PostProcessVolume.h>
#include <Oxygen/Scene/Environment/SceneEnvironment.h>
#include <Oxygen/Scene/ExposureSettings.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::examples::light_bench {
namespace {
  using Json = nlohmann::json;
  auto Objects(LightScene& scene)
    -> std::array<LightScene::SceneObjectState*, 6>
  {
    return {
      &scene.GetGrayCardState(),
      &scene.GetWhiteCardState(),
      &scene.GetBlackCardState(),
      &scene.GetMatteSphereState(),
      &scene.GetGlossySphereState(),
      &scene.GetGroundPlaneState(),
    };
  }
  auto Vector(const Vec3& v) -> Json { return Json::array({ v.x, v.y, v.z }); }
  auto Vector(const Json& v) -> Vec3
  {
    return { v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>() };
  }
  constexpr std::array kExposureFields {
    std::pair { "manual_ev", &scene::ExposureSettings::manual_ev },
    std::pair { "compensation_ev", &scene::ExposureSettings::compensation_ev },
    std::pair { "key", &scene::ExposureSettings::key },
    std::pair { "min_ev", &scene::ExposureSettings::min_ev },
    std::pair { "max_ev", &scene::ExposureSettings::max_ev },
    std::pair { "speed_up", &scene::ExposureSettings::speed_up },
    std::pair { "speed_down", &scene::ExposureSettings::speed_down },
    std::pair { "low_percentile", &scene::ExposureSettings::low_percentile },
    std::pair { "high_percentile", &scene::ExposureSettings::high_percentile },
    std::pair {
      "min_log_luminance", &scene::ExposureSettings::min_log_luminance },
    std::pair {
      "log_luminance_range", &scene::ExposureSettings::log_luminance_range },
    std::pair {
      "target_luminance", &scene::ExposureSettings::target_luminance },
    std::pair {
      "spot_meter_radius", &scene::ExposureSettings::spot_meter_radius },
    std::pair { "black_influence", &scene::ExposureSettings::black_influence },
    std::pair {
      "transition_distance", &scene::ExposureSettings::transition_distance },
  };
  auto ToJson(const LightBenchSettings& s) -> Json
  {
    auto objects = Json::array();
    for (const auto& o : s.objects) {
      objects.push_back({
        { "enabled", o.enabled },
        { "position", Vector(o.position) },
        { "rotation_deg", Vector(o.rotation_deg) },
        { "scale", Vector(o.scale) },
      });
    }
    Json exposure {
      { "enabled", s.exposure.enabled },
      { "mode", s.exposure.mode },
      { "metering_mode", s.exposure.metering_mode },
      { "curve", Json::array() },
    };
    for (const auto& [name, member] : kExposureFields) {
      exposure[name] = s.exposure.*member;
    }
    for (const auto& key : s.exposure.compensation_curve) {
      exposure["curve"].push_back({ key.metered_ev, key.compensation_ev });
    }
    return {
      { "version", 1 },
      { "preset", GetPresetInfo(s.preset).id },
      { "initial_auto_ev", s.initial_auto_ev },
      { "objects", objects },
      {
        "directional",
        {
          { "enabled", s.directional.enabled },
          { "color", Vector(s.directional.color_rgb) },
          { "lux", s.directional.illuminance_lux },
          { "direction", Vector(s.directional.direction_ws) },
          { "casts_shadows", s.directional.casts_shadows },
        },
      },
      {
        "point",
        {
          { "enabled", s.point.enabled },
          { "position", Vector(s.point.position) },
          { "color", Vector(s.point.color_rgb) },
          { "flux_lm", s.point.intensity },
          { "range_m", s.point.range },
          { "radius_m", s.point.source_radius },
          { "casts_shadows", s.point.casts_shadows },
        },
      },
      {
        "spot",
        {
          { "enabled", s.spot.enabled },
          { "position", Vector(s.spot.position) },
          { "direction", Vector(s.spot.direction_ws) },
          { "color", Vector(s.spot.color_rgb) },
          { "flux_lm", s.spot.intensity },
          { "range_m", s.spot.range },
          { "radius_m", s.spot.source_radius },
          { "casts_shadows", s.spot.casts_shadows },
          { "inner_degrees", s.spot.inner_angle_deg },
          { "outer_degrees", s.spot.outer_angle_deg },
        },
      },
      {
        "camera",
        {
          { "position", Vector(s.camera_position) },
          {
            "rotation",
            {
              s.camera_rotation.w,
              s.camera_rotation.x,
              s.camera_rotation.y,
              s.camera_rotation.z,
            },
          },
          { "extents", s.camera_extents },
          { "fit_to_view", s.camera_fit_to_view },
          { "aperture_f", s.camera_exposure.aperture_f },
          { "shutter_rate", s.camera_exposure.shutter_rate },
          { "iso", s.camera_exposure.iso },
        },
      },
      { "exposure", exposure },
      { "tone_mapper", s.tone_mapper },
      { "gamma", s.gamma },
    };
  }
  auto Validate(const Json& document) -> void
  {
    static const auto validator
      = [] -> std::unique_ptr<nlohmann::json_schema::json_validator> {
      auto result = std::make_unique<nlohmann::json_schema::json_validator>();
      result->set_root_schema(Json::parse(kLightBenchSettingsSchema));
      return result;
    }();
    validator->validate(document);
  }
} // namespace

auto ReferenceSettings() -> LightBenchSettings
{
  auto result = LightBenchSettings {};
  auto scene = LightScene {};
  const auto objects = Objects(scene);
  for (std::size_t i = 0; i < objects.size(); ++i) {
    result.objects.at(i) = *objects.at(i);
  }
  result.camera_rotation
    = glm::quatLookAtRH(space::move::Forward, space::move::Up);
  result.exposure.mode = engine::ExposureMode::kManual;
  result.exposure.manual_ev = reference::kExposureEv;
  result.exposure.key = reference::kExposureKey;
  return result;
}

auto GetPresets() -> std::span<const PresetInfo>
{
  static constexpr std::array presets {
    PresetInfo { LightBenchPreset::kNeutralReference, "neutral-reference",
      "Neutral Reference",
      "Three calibrated cards under 1000 lux. Fixed exposure, no tone curve; "
      "use this to check physical lighting and EV changes." },
    PresetInfo { LightBenchPreset::kPointFalloff, "point-falloff",
      "Point Falloff",
      "One gray receiver, 1000 lm point light, 2 m distance. Exposure stays "
      "fixed: moving to 1 m gives about 4x the illumination; 4 m gives about "
      "1/4." },
    PresetInfo { LightBenchPreset::kSpotCone, "spot-cone", "Spot Cone",
      "A white 1000 lm spot aimed at a broad gray receiver, 3 m away. "
      "Inner/outer half angles are 15/30 degrees. Change the cone to inspect "
      "its footprint and edge falloff." },
    PresetInfo { LightBenchPreset::kMaterialLighting, "material-lighting",
      "Material Lighting",
      "Matte and glossy spheres on a lit ground. Angled directional "
      "illumination, shadows, fixed EV8 and ACES make surface response and "
      "highlights easy to compare." },
    PresetInfo { LightBenchPreset::kAutoAdaptation, "auto-adaptation",
      "Auto Adaptation",
      "Cards, spheres and ground receive balanced directional illumination. "
      "Auto + Average metering + ACES. Use the light steps to see adaptation "
      "without resetting its history." },
    PresetInfo { LightBenchPreset::kIndoor, "indoor", "Indoor",
      "Warm 1600 lm point fill and 2500 lm spot lighting. Auto + Center "
      "Weighted metering + ACES; a practical indoor direct-lighting setup with "
      "the same reference objects." },
    PresetInfo { LightBenchPreset::kOutdoorDaylight, "outdoor-daylight",
      "Outdoor Daylight",
      "100,000 lux white directional daylight illuminates both the objects and "
      "ground. Auto + Center Weighted metering + ACES maintains a useful "
      "exposure at outdoor light levels." },
  };
  return presets;
}

auto GetPresetInfo(LightBenchPreset preset) -> const PresetInfo&
{
  for (const auto& info : GetPresets()) {
    if (info.preset == preset) {
      return info;
    }
  }
  throw std::invalid_argument("Unknown LightBench preset");
}

auto PresetSettings(LightBenchPreset preset) -> LightBenchSettings
{
  auto s = ReferenceSettings();
  s.preset = preset;
  if (preset == LightBenchPreset::kNeutralReference) {
    return s;
  }
  if (preset == LightBenchPreset::kPointFalloff
    || preset == LightBenchPreset::kSpotCone) {
    for (auto& object : s.objects) {
      object.enabled = false;
    }
    s.objects.at(0).enabled = true;
    s.objects.at(0).position = { 0.0F, 0.0F, 1.0F };
    s.directional.enabled = false;
    if (preset == LightBenchPreset::kPointFalloff) {
      s.objects.at(0).scale = { 2.6F, 2.6F, 2.6F };
      s.point.enabled = true;
      s.point.position = { 0.0F, 2.0F, 1.0F };
      s.point.intensity = 1000.0F;
      s.point.range = 20.0F;
      // Independent gray response at 2 m including the authored range fade.
      s.exposure.manual_ev = 2.7289812066561563F;
    } else {
      s.objects.at(0).scale = { 5.0F, 5.0F, 5.0F };
      s.spot.enabled = true;
      s.spot.position = { 0.0F, 3.0F, 1.0F };
      s.spot.direction_ws = { 0.0F, -1.0F, 0.0F };
      s.spot.intensity = 1000.0F;
      s.spot.range = 20.0F;
      s.spot.inner_angle_deg = 15.0F;
      s.spot.outer_angle_deg = 30.0F;
      // Fixed center response 0.36 before gamma, including flux normalization.
      s.exposure.manual_ev = 5.449541159604736F;
    }
    return s;
  }
  for (auto& object : s.objects) {
    object.enabled = true;
  }
  // Unit cards and spheres have half-height/radius 0.5 m. Rest them on z=0.
  for (std::size_t i = 0; i < 5; ++i) {
    s.objects.at(i).position.z = .5F;
  }
  // Unequal vertical/horizontal incidence keeps the gray card distinct from
  // the equally gray floor while illuminating both surfaces.
  s.directional.direction_ws = { -.3F, -1.0F, -.5F };
  s.directional.casts_shadows = true;
  s.camera_position = { 4.0F, 7.0F, 4.5F };
  s.camera_rotation = glm::quatLookAtRH(
    glm::normalize(Vec3 { 0.0F, 1.0F, .5F } - s.camera_position),
    space::move::Up);
  s.exposure.manual_ev = 8.0F;
  s.tone_mapper = engine::ToneMapper::kAcesFitted;
  if (preset == LightBenchPreset::kMaterialLighting) {
    for (std::size_t i = 0; i < 3; ++i) {
      s.objects.at(i).enabled = false;
    }
    return s;
  }
  s.exposure.mode = engine::ExposureMode::kAuto;
  s.initial_auto_ev = 8.0F;
  if (preset == LightBenchPreset::kAutoAdaptation) {
    return s;
  }
  s.exposure.metering_mode = engine::MeteringMode::kCenterWeighted;
  if (preset == LightBenchPreset::kOutdoorDaylight) {
    s.directional.illuminance_lux = 100000.0F;
    s.initial_auto_ev = 14.6F;
    return s;
  }
  if (preset == LightBenchPreset::kIndoor) {
    s.directional.enabled = false;
    s.point.enabled = true;
    s.point.casts_shadows = true;
    s.point.position = { -2.0F, 2.0F, 3.0F };
    s.point.intensity = 1600.0F;
    s.point.range = 12.0F;
    s.point.source_radius = .15F;
    s.point.color_rgb = { 1.0F, .82F, .64F };
    s.spot.enabled = true;
    s.spot.casts_shadows = true;
    s.spot.position = { 2.8F, 3.2F, 4.2F };
    s.spot.direction_ws = { -2.8F, -3.2F, -3.2F };
    s.spot.intensity = 2500.0F;
    s.spot.range = 15.0F;
    s.spot.inner_angle_deg = 25.0F;
    s.spot.outer_angle_deg = 50.0F;
    s.spot.source_radius = .12F;
    s.spot.color_rgb = { 1.0F, .9F, .8F };
    s.initial_auto_ev = 5.0F;
    return s;
  }
  throw std::invalid_argument("Unknown LightBench preset");
}

auto CaptureSettings(LightScene& scene, scene::SceneNode& camera)
  -> LightBenchSettings
{
  auto result = ReferenceSettings();
  const auto objects = Objects(scene);
  for (std::size_t i = 0; i < objects.size(); ++i) {
    result.objects.at(i) = *objects.at(i);
  }
  result.directional = scene.GetDirectionalLightState();
  result.point = scene.GetPointLightState();
  result.spot = scene.GetSpotLightState();
  const auto transform = camera.GetTransform();
  const auto position = transform.GetLocalPosition();
  const auto rotation = transform.GetLocalRotation();
  CHECK_F(position.has_value() && rotation.has_value(),
    "LightBench camera transform is unavailable");
  result.camera_position = *position;
  result.camera_rotation = *rotation;
  const auto lens = camera.GetCameraAs<scene::OrthographicCamera>();
  CHECK_F(lens.has_value(), "LightBench requires its orthographic camera");
  result.camera_exposure = lens->get().Exposure();
  result.camera_extents = lens->get().GetExtents();
  const auto post = scene.GetScene()
                      ->GetEnvironment()
                      ->TryGetSystem<scene::environment::PostProcessVolume>();
  result.exposure = post->GetExposureSettings();
  result.tone_mapper = post->GetToneMapper();
  result.gamma = post->GetDisplayGamma();
  return result;
}

auto ApplySettings(const LightBenchSettings& s, LightScene& scene,
  scene::SceneNode& camera) -> void
{
  const auto objects = Objects(scene);
  for (std::size_t i = 0; i < objects.size(); ++i) {
    *objects.at(i) = s.objects.at(i);
  }
  scene.GetDirectionalLightState() = s.directional;
  scene.GetPointLightState() = s.point;
  scene.GetSpotLightState() = s.spot;
  auto transform = camera.GetTransform();
  CHECK_F(transform.SetLocalPosition(s.camera_position),
    "Could not apply camera position");
  CHECK_F(transform.SetLocalRotation(s.camera_rotation),
    "Could not apply camera orientation");
  const auto lens = camera.GetCameraAs<scene::OrthographicCamera>();
  CHECK_F(lens.has_value(), "LightBench requires its orthographic camera");
  lens->get().SetExposure(s.camera_exposure);
  lens->get().SetExtents(s.camera_extents.at(0), s.camera_extents.at(1),
    s.camera_extents.at(2), s.camera_extents.at(3), s.camera_extents.at(4),
    s.camera_extents.at(5));
  const auto post = scene.GetScene()
                      ->GetEnvironment()
                      ->TryGetSystem<scene::environment::PostProcessVolume>();
  post->SetExposureSettings(s.exposure);
  post->SetToneMapper(s.tone_mapper);
  post->SetDisplayGamma(s.gamma);
  scene.Update();
}

auto IsPresetSettings(const LightBenchSettings& s) -> bool
{
  auto candidate = s;
  const auto reference = PresetSettings(s.preset);
  // Camera rig synchronization may introduce insignificant quaternion rounding.
  if (glm::length(candidate.camera_position - reference.camera_position)
    < 1e-5F) {
    candidate.camera_position = reference.camera_position;
  }
  if (std::abs(glm::dot(candidate.camera_rotation, reference.camera_rotation))
    > 1.0F - 1e-6F) {
    candidate.camera_rotation = reference.camera_rotation;
  }
  return candidate.initial_auto_ev == reference.initial_auto_ev
    && candidate.objects == reference.objects
    && candidate.directional == reference.directional
    && candidate.point == reference.point && candidate.spot == reference.spot
    && candidate.camera_position == reference.camera_position
    && candidate.camera_rotation == reference.camera_rotation
    && candidate.camera_exposure.aperture_f
    == reference.camera_exposure.aperture_f
    && candidate.camera_exposure.shutter_rate
    == reference.camera_exposure.shutter_rate
    && candidate.camera_exposure.iso == reference.camera_exposure.iso
    && candidate.camera_fit_to_view == reference.camera_fit_to_view
    && candidate.camera_extents == reference.camera_extents
    && candidate.exposure == reference.exposure
    && candidate.tone_mapper == reference.tone_mapper
    && candidate.gamma == reference.gamma;
}

auto IsReferenceSettings(const LightBenchSettings& s) -> bool
{
  return s.preset == LightBenchPreset::kNeutralReference && IsPresetSettings(s);
}

auto EncodeSettings(const LightBenchSettings& settings)
  -> Result<std::string, SettingsError>
{
  if (settings.exposure.metering_mask != content::ResourceKey {}) {
    return Err(SettingsError {
      std::string("LightBench has no serializable scene mask asset"),
    });
  }
  try {
    auto document = ToJson(settings);
    Validate(document);
    const auto text = document.dump(2) + '\n';
    const auto checked = DecodeSettings(text);
    if (!checked) {
      return Err(checked.error());
    }
    return Ok(text);
  } catch (const std::exception& error) {
    return Err(SettingsError { std::string(error.what()) });
  }
}

auto DecodeSettings(std::string_view encoded)
  -> Result<LightBenchSettings, SettingsError>
{
  try {
    const auto j = Json::parse(encoded);
    Validate(j);
    auto s = ReferenceSettings();
    if (j.contains("preset")) {
      for (const auto& info : GetPresets()) {
        if (info.id == j.at("preset").get<std::string>()) {
          s.preset = info.preset;
        }
      }
    }
    s.initial_auto_ev = j.value("initial_auto_ev", 8.0F);
    for (std::size_t i = 0; i < s.objects.size(); ++i) {
      const auto& o = j.at("objects").at(i);
      s.objects.at(i) = {
        .enabled = o.at("enabled").get<bool>(),
        .position = Vector(o.at("position")),
        .rotation_deg = Vector(o.at("rotation_deg")),
        .scale = Vector(o.at("scale")),
      };
    }
    const auto& d = j.at("directional");
    s.directional = {
      .enabled = d.at("enabled").get<bool>(),
      .color_rgb = Vector(d.at("color")),
      .illuminance_lux = d.at("lux").get<float>(),
    };
    if (d.contains("direction")) {
      s.directional.direction_ws = Vector(d.at("direction"));
    }
    s.directional.casts_shadows = d.value("casts_shadows", false);
    const auto& p = j.at("point");
    s.point = {
      .enabled = p.at("enabled").get<bool>(),
      .position = Vector(p.at("position")),
      .color_rgb = Vector(p.at("color")),
      .intensity = p.at("flux_lm").get<float>(),
      .range = p.at("range_m").get<float>(),
      .source_radius = p.at("radius_m").get<float>(),
    };
    const auto& t = j.at("spot");
    s.spot = {
      .enabled = t.at("enabled").get<bool>(),
      .position = Vector(t.at("position")),
      .direction_ws = Vector(t.at("direction")),
      .color_rgb = Vector(t.at("color")),
      .intensity = t.at("flux_lm").get<float>(),
      .range = t.at("range_m").get<float>(),
      .inner_angle_deg = t.at("inner_degrees").get<float>(),
      .outer_angle_deg = t.at("outer_degrees").get<float>(),
      .source_radius = t.at("radius_m").get<float>(),
    };
    s.point.casts_shadows = p.value("casts_shadows", false);
    s.spot.casts_shadows = t.value("casts_shadows", false);
    const auto& c = j.at("camera");
    s.camera_position = Vector(c.at("position"));
    const auto& q = c.at("rotation");
    s.camera_rotation = { q.at(0).get<float>(), q.at(1).get<float>(),
      q.at(2).get<float>(), q.at(3).get<float>() };
    s.camera_extents = c.at("extents").get<std::array<float, 6>>();
    s.camera_fit_to_view = c.at("fit_to_view").get<bool>();
    s.camera_exposure = {
      .aperture_f = c.at("aperture_f").get<float>(),
      .shutter_rate = c.at("shutter_rate").get<float>(),
      .iso = c.at("iso").get<float>(),
    };
    const auto& e = j.at("exposure");
    s.exposure.enabled = e.at("enabled").get<bool>();
    s.exposure.mode = e.at("mode").get<engine::ExposureMode>();
    s.exposure.metering_mode
      = e.at("metering_mode").get<engine::MeteringMode>();
    for (const auto& [name, member] : kExposureFields) {
      s.exposure.*member = e.at(name).get<float>();
    }
    for (const auto& key : e.at("curve")) {
      s.exposure.compensation_curve.push_back({
        .metered_ev = key.at(0).get<float>(),
        .compensation_ev = key.at(1).get<float>(),
      });
    }
    s.tone_mapper = j.at("tone_mapper").get<engine::ToneMapper>();
    s.gamma = j.at("gamma").get<float>();
    if (s.camera_extents.at(0) >= s.camera_extents.at(1)
      || s.camera_extents.at(2) >= s.camera_extents.at(3)
      || s.camera_extents.at(4) <= 0
      || s.camera_extents.at(4) >= s.camera_extents.at(5)) {
      return Err(
        SettingsError { "Invalid orthographic extents or clipping planes" });
    }
    if (std::abs(glm::length(s.camera_rotation) - 1.0F) > 1e-4F
      || glm::length(s.spot.direction_ws) < 1e-6F
      || glm::length(s.directional.direction_ws) < 1e-6F
      || s.spot.inner_angle_deg > s.spot.outer_angle_deg
      || !std::isfinite(s.camera_exposure.GetEv())) {
      return Err(SettingsError {
        std::string(
          "Invalid camera orientation/exposure or spot direction/cones"),
      });
    }
    const auto exposure
      = scene::ResolveExposureSettings(s.exposure, s.camera_exposure.GetEv());
    if (!exposure) {
      return Err(SettingsError { std::string(to_string(exposure.error())) });
    }
    return Ok(std::move(s));
  } catch (const std::exception& error) {
    return Err(SettingsError { std::string(error.what()) });
  }
}

} // namespace oxygen::examples::light_bench
