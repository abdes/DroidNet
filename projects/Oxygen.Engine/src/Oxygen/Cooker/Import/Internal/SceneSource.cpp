//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/JsonSchemaValidation.h>
#include <Oxygen/Core/Types/CameraAspectMode.h>
#include <Oxygen/Core/Types/PostProcess.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/MaterialSlotId.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Scene/ExposureSettings.h>

namespace oxygen::content::import::internal {
namespace {
  using nlohmann::json;
  using nlohmann::json_schema::json_validator;

  struct SourceContext final {
    const std::filesystem::path& source_path;
    std::vector<ImportDiagnostic>& diagnostics;
  };

  auto AddDiagnostic(SourceContext& context, const ImportSeverity severity,
    std::string code, std::string message, std::string object_path = {}) -> void
  {
    context.diagnostics.push_back({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = context.source_path.string(),
      .object_path = std::move(object_path),
    });
  }

  class StringTableBuilder final {
  public:
    StringTableBuilder()
      : bytes_ { std::byte { 0 } }
    {
      offsets_.insert_or_assign(
        std::string {}, data::pak::core::StringTableOffsetT { 0 });
    }

    [[nodiscard]] auto Add(std::string_view value)
      -> data::pak::core::StringTableOffsetT
    {
      if (value.empty()) {
        return data::pak::core::StringTableOffsetT { 0 };
      }

      if (const auto it = offsets_.find(std::string(value));
        it != offsets_.end()) {
        return it->second;
      }

      const auto offset
        = static_cast<data::pak::core::StringTableOffsetT>(bytes_.size());
      bytes_.reserve(bytes_.size() + value.size() + 1U);
      for (const auto ch : value) {
        bytes_.push_back(static_cast<std::byte>(ch));
      }
      bytes_.push_back(std::byte { 0 });
      offsets_.insert_or_assign(std::string(value), offset);
      return offset;
    }

    [[nodiscard]] auto Build() && -> std::vector<std::byte>
    {
      return std::move(bytes_);
    }

  private:
    std::vector<std::byte> bytes_;
    std::unordered_map<std::string, data::pak::core::StringTableOffsetT>
      offsets_;
  };

  auto GetSceneDescriptorValidator() -> json_validator&
  {
    static auto validator = [] -> json_validator {
      auto out = json_validator {};
      out.set_root_schema(nlohmann::json::parse(kSceneDescriptorSchema));
      return out;
    }();
    return validator;
  }

  auto ValidateDescriptorSchema(
    SourceContext& context, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto config = internal::JsonSchemaValidationDiagnosticConfig {
      .validation_failed_code = "scene.descriptor.schema_validation_failed",
      .validation_failed_prefix = "Scene descriptor validation failed: ",
      .validation_overflow_prefix = "Scene descriptor validation emitted ",
      .validator_failure_code = "scene.descriptor.schema_validator_failure",
      .validator_failure_prefix = "Scene descriptor schema validator failed: ",
      .max_issues = 12,
    };

    return internal::ValidateJsonSchemaWithDiagnostics(
      GetSceneDescriptorValidator(), descriptor_doc, config,
      [&](const std::string_view code, const std::string& message,
        const std::string& object_path) -> void {
        AddDiagnostic(context, ImportSeverity::kError, std::string(code),
          message, object_path);
      });
  }

  auto ValidateDescriptorVersion(
    SourceContext& context, const nlohmann::json& descriptor_doc) -> bool
  {
    const auto version_it = descriptor_doc.find("version");
    const auto invalid_version = version_it == descriptor_doc.end()
      || !version_it->is_number_unsigned()
      || version_it->get<uint64_t>() != data::pak::world::kSceneAssetVersion;
    if (!invalid_version) {
      return true;
    }

    AddDiagnostic(context, ImportSeverity::kError,
      "scene.descriptor.recook_required",
      "Scene descriptor version "
        + std::to_string(
          static_cast<uint32_t>(data::pak::world::kSceneAssetVersion))
        + " is required; update the authored descriptor and re-cook the scene "
          "content.");
    return false;
  }

  template <size_t Count>
  auto CopyFloatArray(const json& source, float (&dest)[Count]) -> void
  {
    for (size_t i = 0; i < Count; ++i) {
      dest[i] = source.at(i).get<float>();
    }
  }

  auto BuildSkyAtmosphereSystemRecord(const json& source)
    -> data::pak::world::SkyAtmosphereEnvironmentRecord
  {
    auto record = data::pak::world::SkyAtmosphereEnvironmentRecord {};
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.planet_radius_m
      = source.value("planet_radius_m", record.planet_radius_m);
    record.atmosphere_height_m
      = source.value("atmosphere_height_m", record.atmosphere_height_m);
    CopyFloatArray(source.at("ground_albedo_rgb"), record.ground_albedo_rgb);
    CopyFloatArray(
      source.at("rayleigh_scattering_rgb"), record.rayleigh_scattering_rgb);
    record.rayleigh_scale_height_m
      = source.value("rayleigh_scale_height_m", record.rayleigh_scale_height_m);
    CopyFloatArray(source.at("mie_scattering_rgb"), record.mie_scattering_rgb);
    CopyFloatArray(source.at("mie_absorption_rgb"), record.mie_absorption_rgb);
    record.mie_scale_height_m
      = source.value("mie_scale_height_m", record.mie_scale_height_m);
    record.mie_g = source.value("mie_anisotropy", record.mie_g);
    CopyFloatArray(source.at("ozone_absorption_rgb"), record.absorption_rgb);
    CopyFloatArray(
      source.at("ozone_density_profile"), record.ozone_density_profile);
    record.multi_scattering_factor
      = source.value("multi_scattering_factor", record.multi_scattering_factor);
    CopyFloatArray(
      source.at("sky_luminance_factor_rgb"), record.sky_luminance_factor_rgb);
    CopyFloatArray(source.at("sky_and_aerial_perspective_luminance_factor_rgb"),
      record.sky_and_aerial_perspective_luminance_factor_rgb);
    record.aerial_perspective_distance_scale
      = source.value("aerial_perspective_distance_scale",
        record.aerial_perspective_distance_scale);
    record.aerial_scattering_strength = source.value(
      "aerial_scattering_strength", record.aerial_scattering_strength);
    record.aerial_perspective_start_depth_m
      = source.value("aerial_perspective_start_depth_m",
        record.aerial_perspective_start_depth_m);
    record.height_fog_contribution
      = source.value("height_fog_contribution", record.height_fog_contribution);
    record.trace_sample_count_scale = source.value(
      "trace_sample_count_scale", record.trace_sample_count_scale);
    record.transmittance_min_light_elevation_deg
      = source.value("transmittance_min_light_elevation_deg",
        record.transmittance_min_light_elevation_deg);
    record.sun_disk_enabled = source.value("sun_disk_enabled", true) ? 1U : 0U;
    record.holdout = source.value("holdout", false) ? 1U : 0U;
    record.render_in_main_pass
      = source.value("render_in_main_pass", true) ? 1U : 0U;

    return record;
  }

  auto BuildFogSystemRecord(const json& source) -> SceneSource::Fog
  {
    auto prepared = SceneSource::Fog {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.model = source.value("model", record.model);
    record.extinction_sigma_t_per_m = source.value(
      "extinction_sigma_t_per_m", record.extinction_sigma_t_per_m);
    record.height_falloff_per_m
      = source.value("height_falloff_per_m", record.height_falloff_per_m);
    record.height_offset_m
      = source.value("height_offset_m", record.height_offset_m);
    record.start_distance_m
      = source.value("start_distance_m", record.start_distance_m);
    record.max_opacity = source.value("max_opacity", record.max_opacity);
    CopyFloatArray(source.at("single_scattering_albedo_rgb"),
      record.single_scattering_albedo_rgb);
    record.anisotropy_g = source.value("anisotropy_g", record.anisotropy_g);
    record.enable_height_fog
      = source.value("enable_height_fog", true) ? 1U : 0U;
    record.enable_volumetric_fog
      = source.value("enable_volumetric_fog", false) ? 1U : 0U;
    record.second_fog_density
      = source.value("second_fog_density", record.second_fog_density);
    record.second_fog_height_falloff = source.value(
      "second_fog_height_falloff", record.second_fog_height_falloff);
    record.second_fog_height_offset = source.value(
      "second_fog_height_offset", record.second_fog_height_offset);
    CopyFloatArray(source.at("fog_inscattering_luminance"),
      record.fog_inscattering_luminance);
    CopyFloatArray(source.at("sky_atmosphere_ambient_contribution_color_scale"),
      record.sky_atmosphere_ambient_contribution_color_scale);
    if (source.contains("inscattering_color_cubemap_ref")) {
      prepared.cubemap
        = source.at("inscattering_color_cubemap_ref").get<std::string>();
    }
    record.inscattering_color_cubemap_angle
      = source.value("inscattering_color_cubemap_angle",
        record.inscattering_color_cubemap_angle);
    CopyFloatArray(
      source.at("inscattering_texture_tint"), record.inscattering_texture_tint);
    record.fully_directional_inscattering_color_distance
      = source.value("fully_directional_inscattering_color_distance",
        record.fully_directional_inscattering_color_distance);
    record.non_directional_inscattering_color_distance
      = source.value("non_directional_inscattering_color_distance",
        record.non_directional_inscattering_color_distance);
    CopyFloatArray(source.at("directional_inscattering_luminance"),
      record.directional_inscattering_luminance);
    record.directional_inscattering_exponent
      = source.value("directional_inscattering_exponent",
        record.directional_inscattering_exponent);
    record.directional_inscattering_start_distance
      = source.value("directional_inscattering_start_distance",
        record.directional_inscattering_start_distance);
    record.end_distance_m
      = source.value("end_distance_m", record.end_distance_m);
    record.fog_cutoff_distance_m
      = source.value("fog_cutoff_distance_m", record.fog_cutoff_distance_m);
    record.volumetric_fog_scattering_distribution
      = source.value("volumetric_fog_scattering_distribution",
        record.volumetric_fog_scattering_distribution);
    CopyFloatArray(
      source.at("volumetric_fog_albedo"), record.volumetric_fog_albedo);
    CopyFloatArray(
      source.at("volumetric_fog_emissive"), record.volumetric_fog_emissive);
    record.volumetric_fog_extinction_scale
      = source.value("volumetric_fog_extinction_scale",
        record.volumetric_fog_extinction_scale);
    record.volumetric_fog_distance
      = source.value("volumetric_fog_distance", record.volumetric_fog_distance);
    record.volumetric_fog_start_distance = source.value(
      "volumetric_fog_start_distance", record.volumetric_fog_start_distance);
    record.volumetric_fog_near_fade_in_distance
      = source.value("volumetric_fog_near_fade_in_distance",
        record.volumetric_fog_near_fade_in_distance);
    record.volumetric_fog_static_lighting_scattering_intensity
      = source.value("volumetric_fog_static_lighting_scattering_intensity",
        record.volumetric_fog_static_lighting_scattering_intensity);
    record.override_light_colors_with_fog_inscattering_colors
      = source.value(
          "override_light_colors_with_fog_inscattering_colors", false)
      ? 1U
      : 0U;
    record.holdout = source.value("holdout", false) ? 1U : 0U;
    record.render_in_main_pass
      = source.value("render_in_main_pass", true) ? 1U : 0U;
    record.visible_in_reflection_captures
      = source.value("visible_in_reflection_captures", true) ? 1U : 0U;
    record.visible_in_real_time_sky_captures
      = source.value("visible_in_real_time_sky_captures", true) ? 1U : 0U;

    return prepared;
  }

  auto BuildSkyLightSystemRecord(const json& source) -> SceneSource::SkyLight
  {
    auto prepared = SceneSource::SkyLight {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.source = source.value("source", record.source);
    if (source.contains("cubemap_ref")) {
      prepared.cubemap = source.at("cubemap_ref").get<std::string>();
    }
    record.intensity = source.value("intensity", record.intensity);
    CopyFloatArray(source.at("tint_rgb"), record.tint_rgb);
    record.diffuse_intensity
      = source.value("diffuse_intensity", record.diffuse_intensity);
    record.specular_intensity
      = source.value("specular_intensity", record.specular_intensity);

    record.source_cubemap_angle_radians = source.value(
      "source_cubemap_angle_radians", record.source_cubemap_angle_radians);
    CopyFloatArray(
      source.at("lower_hemisphere_color"), record.lower_hemisphere_color);
    record.lower_hemisphere_is_solid_color
      = source.value("lower_hemisphere_is_solid_color", true) ? 1U : 0U;
    record.lower_hemisphere_blend_alpha = source.value(
      "lower_hemisphere_blend_alpha", record.lower_hemisphere_blend_alpha);
    record.volumetric_scattering_intensity
      = source.value("volumetric_scattering_intensity",
        record.volumetric_scattering_intensity);
    record.affect_reflections
      = source.value("affect_reflections", true) ? 1U : 0U;

    return prepared;
  }

  auto BuildPostProcessSystemRecord(SourceContext& context, const json& source)
    -> std::optional<SceneSource::PostProcess>
  {
    auto prepared = SceneSource::PostProcess {};
    auto& record = prepared.record;
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    record.tone_mapper = static_cast<engine::ToneMapper>(
      source.value("tone_mapper", static_cast<uint32_t>(record.tone_mapper)));
    record.exposure_mode = static_cast<engine::ExposureMode>(source.value(
      "exposure_mode", static_cast<uint32_t>(record.exposure_mode)));
    record.exposure_enabled
      = source.value("exposure_enabled", record.exposure_enabled != 0U) ? 1U
                                                                        : 0U;
    record.exposure_compensation_ev = source.value(
      "exposure_compensation_ev", record.exposure_compensation_ev);
    record.exposure_key = source.value("exposure_key", record.exposure_key);
    record.manual_exposure_ev
      = source.value("manual_exposure_ev", record.manual_exposure_ev);
    record.auto_exposure_min_ev
      = source.value("auto_exposure_min_ev", record.auto_exposure_min_ev);
    record.auto_exposure_max_ev
      = source.value("auto_exposure_max_ev", record.auto_exposure_max_ev);
    record.auto_exposure_speed_up
      = source.value("auto_exposure_speed_up", record.auto_exposure_speed_up);
    record.auto_exposure_speed_down = source.value(
      "auto_exposure_speed_down", record.auto_exposure_speed_down);
    record.auto_exposure_metering_mode = static_cast<engine::MeteringMode>(
      source.value("auto_exposure_metering_mode",
        static_cast<uint32_t>(record.auto_exposure_metering_mode)));
    record.auto_exposure_low_percentile = source.value(
      "auto_exposure_low_percentile", record.auto_exposure_low_percentile);
    record.auto_exposure_high_percentile = source.value(
      "auto_exposure_high_percentile", record.auto_exposure_high_percentile);
    record.auto_exposure_min_log_luminance
      = source.value("auto_exposure_min_log_luminance",
        record.auto_exposure_min_log_luminance);
    record.auto_exposure_log_luminance_range
      = source.value("auto_exposure_log_luminance_range",
        record.auto_exposure_log_luminance_range);
    record.auto_exposure_target_luminance = source.value(
      "auto_exposure_target_luminance", record.auto_exposure_target_luminance);
    record.auto_exposure_spot_meter_radius
      = source.value("auto_exposure_spot_meter_radius",
        record.auto_exposure_spot_meter_radius);
    record.bloom_intensity
      = source.value("bloom_intensity", record.bloom_intensity);
    record.bloom_threshold
      = source.value("bloom_threshold", record.bloom_threshold);
    record.saturation = source.value("saturation", record.saturation);
    record.contrast = source.value("contrast", record.contrast);
    record.vignette_intensity
      = source.value("vignette_intensity", record.vignette_intensity);
    record.display_gamma = source.value("display_gamma", record.display_gamma);
    record.auto_exposure_black_influence = source.value(
      "auto_exposure_black_influence", record.auto_exposure_black_influence);
    record.auto_exposure_transition_distance_ev
      = source.value("auto_exposure_transition_distance_ev",
        record.auto_exposure_transition_distance_ev);
    if (source.contains("auto_exposure_metering_mask")) {
      prepared.metering_mask
        = source.at("auto_exposure_metering_mask").get<std::string>();
    }
    auto settings = scene::ExposureSettings {
      .enabled = record.exposure_enabled != 0U,
      .mode = record.exposure_mode,
      .manual_ev = record.manual_exposure_ev,
      .compensation_ev = record.exposure_compensation_ev,
      .key = record.exposure_key,
      .min_ev = record.auto_exposure_min_ev,
      .max_ev = record.auto_exposure_max_ev,
      .speed_up = record.auto_exposure_speed_up,
      .speed_down = record.auto_exposure_speed_down,
      .metering_mode = record.auto_exposure_metering_mode,
      .low_percentile = record.auto_exposure_low_percentile,
      .high_percentile = record.auto_exposure_high_percentile,
      .min_log_luminance = record.auto_exposure_min_log_luminance,
      .log_luminance_range = record.auto_exposure_log_luminance_range,
      .target_luminance = record.auto_exposure_target_luminance,
      .spot_meter_radius = record.auto_exposure_spot_meter_radius,
      .black_influence = record.auto_exposure_black_influence,
      .transition_distance = record.auto_exposure_transition_distance_ev,
    };
    if (const auto curve = source.find("auto_exposure_compensation_curve");
      curve != source.end()) {
      for (const auto& key : *curve) {
        settings.compensation_curve.push_back({
          .metered_ev = key.at("metered_ev").get<float>(),
          .compensation_ev = key.at("compensation_ev").get<float>(),
        });
      }
    }
    const auto resolved = scene::ResolveExposureSettings(settings);
    // The authored record has no selected view/camera. Validate gain again
    // with the actual camera EV when the runtime activates ManualCamera.
    if (!resolved
      && resolved.error() != scene::ExposureSettingsError::kMissingCameraEv) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.exposure_invalid",
        "Exposure settings rejected: "
          + std::string(scene::to_string(resolved.error())),
        "environment.post_process_volume");
      return std::nullopt;
    }
    record.curve_key_count
      = static_cast<uint32_t>(settings.compensation_curve.size());
    record.header.record_size = static_cast<uint32_t>(sizeof(record)
      + (settings.compensation_curve.size()
        * sizeof(data::pak::world::ExposureCompensationKeyRecord)));
    prepared.curve.reserve(settings.compensation_curve.size());
    for (const auto& key : settings.compensation_curve) {
      prepared.curve.push_back({
        .metered_ev = key.metered_ev,
        .compensation_ev = key.compensation_ev,
      });
    }
    return prepared;
  }

  auto BuildBackgroundSystemRecord(const json& source)
    -> data::pak::world::BackgroundEnvironmentRecord
  {
    auto record = data::pak::world::BackgroundEnvironmentRecord {};
    record.enabled = source.value("enabled", true) ? 1U : 0U;
    if (source.contains("color_rgb")) {
      CopyFloatArray(source.at("color_rgb"), record.color_rgb);
    }
    return record;
  }

  auto ValidateNodeIndex(SourceContext& context, const uint32_t node_index,
    const uint32_t node_count, std::string code, std::string message,
    std::string object_path) -> bool
  {
    if (node_index < node_count) {
      return true;
    }
    AddDiagnostic(context, ImportSeverity::kError, std::move(code),
      std::move(message), std::move(object_path));
    return false;
  }

  auto ApplyLightCommon(
    const json& common_doc, data::pak::world::LightCommonRecord& common) -> void
  {
    if (common_doc.contains("affects_world")) {
      common.affects_world
        = common_doc.at("affects_world").get<bool>() ? 1U : 0U;
    }
    if (common_doc.contains("color_rgb")) {
      const auto& color = common_doc.at("color_rgb");
      for (size_t i = 0; i < 3U; ++i) {
        common.color_rgb[i] = color.at(i).get<float>();
      }
    }
    if (common_doc.contains("casts_shadows")) {
      common.casts_shadows
        = common_doc.at("casts_shadows").get<bool>() ? 1U : 0U;
    }
    if (common_doc.contains("exposure_compensation_ev")) {
      common.exposure_compensation_ev
        = common_doc.at("exposure_compensation_ev").get<float>();
    }
    if (common_doc.contains("shadow")) {
      const auto& shadow = common_doc.at("shadow");
      if (shadow.contains("bias")) {
        common.shadow.bias = shadow.at("bias").get<float>();
      }
      if (shadow.contains("normal_bias")) {
        common.shadow.normal_bias = shadow.at("normal_bias").get<float>();
      }
      if (shadow.contains("contact_shadows")) {
        common.shadow.contact_shadows
          = shadow.at("contact_shadows").get<bool>() ? 1U : 0U;
      }
      if (shadow.contains("resolution_hint")) {
        common.shadow.resolution_hint
          = static_cast<uint8_t>(shadow.at("resolution_hint").get<uint32_t>());
      }
    }
  }

  auto BuildNodeFlags(const json& node_doc, const bool is_root,
    data::pak::world::NodeRecord& node) -> void
  {
    node.node_flags
      = is_root ? data::pak::world::kSceneNodeFlags_Inheritable : 0U;
    node.inherited_flags
      = is_root ? 0U : data::pak::world::kSceneNodeFlags_Inheritable;
    if (!node_doc.contains("flags")) {
      return;
    }

    const auto& flags_doc = node_doc.at("flags");
    const auto apply_flag = [&](const char* key, const uint32_t mask) -> void {
      if (!flags_doc.contains(key)) {
        return;
      }
      const auto enabled = flags_doc.at(key).get<bool>();
      if (enabled) {
        node.node_flags |= mask;
      } else {
        node.node_flags &= ~mask;
      }
    };

    const auto apply_source
      = [&](const char* key, const uint32_t mask) -> void {
      if (!flags_doc.contains(key)) {
        return;
      }
      const auto& source = flags_doc.at(key).get_ref<const std::string&>();
      node.node_flags &= ~mask;
      node.inherited_flags &= ~mask;
      if (source == "inherit") {
        node.inherited_flags |= mask;
      } else if (source == "shown" || source == "on") {
        node.node_flags |= mask;
      }
    };

    apply_source("visible", data::pak::world::kSceneNodeFlag_Visible);
    apply_flag("static", data::pak::world::kSceneNodeFlag_Static);
    apply_source(
      "casts_shadows", data::pak::world::kSceneNodeFlag_CastsShadows);
    apply_source(
      "receives_shadows", data::pak::world::kSceneNodeFlag_ReceivesShadows);
    apply_flag("ray_cast_selectable",
      data::pak::world::kSceneNodeFlag_RayCastingSelectable);
    apply_flag("ignore_parent_transform",
      data::pak::world::kSceneNodeFlag_IgnoreParentTransform);
  }

  auto PrepareSceneSource(SourceContext& context, const json& descriptor_doc)
    -> std::optional<SceneSource>
  {
    auto prepared = SceneSource {};
    prepared.name = descriptor_doc.at("name").get<std::string>();
    if (descriptor_doc.contains("content_hashing")) {
      prepared.content_hashing
        = descriptor_doc.at("content_hashing").get<bool>();
    }
    const auto& nodes_doc = descriptor_doc.at("nodes");
    const auto node_count = static_cast<uint32_t>(nodes_doc.size());

    auto string_table = StringTableBuilder {};
    prepared.build.nodes.reserve(nodes_doc.size());

    for (size_t node_i = 0; node_i < nodes_doc.size(); ++node_i) {
      const auto& node_doc = nodes_doc.at(node_i);
      const auto node_path
        = std::string { "nodes[" } + std::to_string(node_i) + "]";

      auto node_record = data::pak::world::NodeRecord {};
      if (node_doc.contains("name")) {
        node_record.scene_name_offset
          = string_table.Add(node_doc.at("name").get<std::string>());
      }
      node_record.parent_index = static_cast<uint32_t>(node_i);
      if (node_doc.contains("parent")) {
        const auto parent_index = node_doc.at("parent").get<uint32_t>();
        if (parent_index >= node_count) {
          AddDiagnostic(context, ImportSeverity::kError,
            "scene.descriptor.node_parent_out_of_range",
            "Node parent index is out of range", node_path + ".parent");
          return std::nullopt;
        }
        node_record.parent_index = parent_index;
      }

      BuildNodeFlags(node_doc,
        node_record.parent_index == static_cast<uint32_t>(node_i), node_record);

      if (node_doc.contains("transform")) {
        const auto& transform = node_doc.at("transform");
        if (transform.contains("translation")) {
          const auto& translation = transform.at("translation");
          for (size_t i = 0; i < 3U; ++i) {
            node_record.translation[i] = translation.at(i).get<float>();
          }
        }
        if (transform.contains("rotation")) {
          const auto& rotation = transform.at("rotation");
          for (size_t i = 0; i < 4U; ++i) {
            node_record.rotation[i] = rotation.at(i).get<float>();
          }
        }
        if (transform.contains("scale")) {
          const auto& scale = transform.at("scale");
          for (size_t i = 0; i < 3U; ++i) {
            node_record.scale[i] = scale.at(i).get<float>();
          }
        }
      }

      prepared.build.nodes.push_back(node_record);
    }

    prepared.build.strings = std::move(string_table).Build();

    if (descriptor_doc.contains("renderables")) {
      const auto& values = descriptor_doc.at("renderables");
      prepared.renderables.reserve(values.size());
      for (size_t i = 0; i < values.size(); ++i) {
        const auto& document = values.at(i);
        const auto object_path = "renderables[" + std::to_string(i) + "]";
        auto renderable = SceneSource::Renderable {
          .node_index = document.at("node").get<uint32_t>(),
          .visible = document.value("visible", true),
          .geometry = document.at("geometry_ref").get<std::string>(),
          .materials = {},
        };
        if (!ValidateNodeIndex(context, renderable.node_index, node_count,
              "scene.descriptor.renderable_node_index_out_of_range",
              "Renderable node index is out of range", object_path + ".node")) {
          return std::nullopt;
        }
        if (document.contains("material_overrides")) {
          auto assigned = std::unordered_set<data::MaterialSlotId> {};
          for (const auto& assignment : document.at("material_overrides")) {
            const auto slot = data::MaterialSlotId::FromString(
              assignment.at("slot_id").get<std::string>());
            if (!slot || slot.value().IsNil()
              || !assigned.insert(slot.value()).second) {
              AddDiagnostic(context, ImportSeverity::kError,
                "scene.descriptor.material_slot_invalid",
                "Material assignment requires a non-nil, unique slot identity",
                object_path + ".material_overrides");
              return std::nullopt;
            }
            renderable.materials.push_back({
              .slot_id = slot.value(),
              .material = assignment.at("material_ref").get<std::string>(),
              .layout_revision
              = assignment.at("layout_revision").get<std::string>(),
            });
          }
        }
        prepared.renderables.push_back(std::move(renderable));
      }
    }

    if (descriptor_doc.contains("cameras")) {
      const auto& cameras_doc = descriptor_doc.at("cameras");
      if (cameras_doc.contains("perspective")) {
        const auto& perspective_doc = cameras_doc.at("perspective");
        prepared.build.perspective_cameras.reserve(perspective_doc.size());
        for (size_t i = 0; i < perspective_doc.size(); ++i) {
          const auto& camera_doc = perspective_doc.at(i);
          const auto object_path
            = std::string { "cameras.perspective[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Perspective camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto camera = data::pak::world::PerspectiveCameraRecord {};
          camera.node_index = node_index;
          camera.aspect_mode = camera_doc.at("aspect_mode") == "fixed"
            ? CameraAspectMode::kFixed
            : CameraAspectMode::kAuto;
          if (camera_doc.contains("fov_y")) {
            camera.fov_y = camera_doc.at("fov_y").get<float>();
          }
          if (camera_doc.contains("aspect_ratio")) {
            camera.aspect_ratio = camera_doc.at("aspect_ratio").get<float>();
          }
          if (camera_doc.contains("near_plane")) {
            camera.near_plane = camera_doc.at("near_plane").get<float>();
          }
          if (camera_doc.contains("far_plane")) {
            camera.far_plane = camera_doc.at("far_plane").get<float>();
          }
          camera.aperture_f = camera_doc.value("aperture_f", camera.aperture_f);
          camera.shutter_rate
            = camera_doc.value("shutter_rate", camera.shutter_rate);
          camera.iso = camera_doc.value("iso", camera.iso);
          prepared.build.perspective_cameras.push_back(camera);
        }
      }

      if (cameras_doc.contains("orthographic")) {
        const auto& orthographic_doc = cameras_doc.at("orthographic");
        prepared.build.orthographic_cameras.reserve(orthographic_doc.size());
        for (size_t i = 0; i < orthographic_doc.size(); ++i) {
          const auto& camera_doc = orthographic_doc.at(i);
          const auto object_path
            = std::string { "cameras.orthographic[" } + std::to_string(i) + "]";
          const auto node_index = camera_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.camera_node_index_out_of_range",
                "Orthographic camera node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto camera = data::pak::world::OrthographicCameraRecord {};
          camera.node_index = node_index;
          if (camera_doc.contains("left")) {
            camera.left = camera_doc.at("left").get<float>();
          }
          if (camera_doc.contains("right")) {
            camera.right = camera_doc.at("right").get<float>();
          }
          if (camera_doc.contains("bottom")) {
            camera.bottom = camera_doc.at("bottom").get<float>();
          }
          if (camera_doc.contains("top")) {
            camera.top = camera_doc.at("top").get<float>();
          }
          if (camera_doc.contains("near_plane")) {
            camera.near_plane = camera_doc.at("near_plane").get<float>();
          }
          if (camera_doc.contains("far_plane")) {
            camera.far_plane = camera_doc.at("far_plane").get<float>();
          }
          camera.aperture_f = camera_doc.value("aperture_f", camera.aperture_f);
          camera.shutter_rate
            = camera_doc.value("shutter_rate", camera.shutter_rate);
          camera.iso = camera_doc.value("iso", camera.iso);
          prepared.build.orthographic_cameras.push_back(camera);
        }
      }
    }

    if (descriptor_doc.contains("lights")) {
      const auto& lights_doc = descriptor_doc.at("lights");
      if (lights_doc.contains("directional")) {
        const auto& directional_doc = lights_doc.at("directional");
        prepared.build.directional_lights.reserve(directional_doc.size());
        for (size_t i = 0; i < directional_doc.size(); ++i) {
          const auto& light_doc = directional_doc.at(i);
          const auto object_path
            = std::string { "lights.directional[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Directional light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::DirectionalLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("angular_size_radians")) {
            light.angular_size_radians
              = light_doc.at("angular_size_radians").get<float>();
          }
          light.atmosphere_light_slot
            = light_doc.value("atmosphere_light_slot", uint8_t { 0U });
          light.use_per_pixel_atmosphere_transmittance
            = light_doc.value("use_per_pixel_atmosphere_transmittance", false)
            ? 1U
            : 0U;
          if (light_doc.contains("atmosphere_disk_luminance_scale_rgb")) {
            for (std::size_t channel = 0U; channel < 3U; ++channel) {
              light.atmosphere_disk_luminance_scale_rgb[channel]
                = light_doc.at("atmosphere_disk_luminance_scale_rgb")
                    .at(channel)
                    .get<float>();
            }
          }
          if (light_doc.contains("cascade_count")) {
            light.cascade_count = light_doc.at("cascade_count").get<uint32_t>();
          }
          if (light_doc.contains("split_mode")) {
            light.split_mode = static_cast<uint8_t>(
              light_doc.at("split_mode").get<uint32_t>());
          }
          if (light_doc.contains("max_shadow_distance")) {
            light.max_shadow_distance
              = light_doc.at("max_shadow_distance").get<float>();
          }
          if (light_doc.contains("cascade_distances")) {
            const auto& distances = light_doc.at("cascade_distances");
            for (size_t d = 0; d < 4U; ++d) {
              light.cascade_distances[d] = distances.at(d).get<float>();
            }
            if (!light_doc.contains("split_mode")) {
              light.split_mode = 1U;
            }
            if (!light_doc.contains("max_shadow_distance")) {
              light.max_shadow_distance = light.cascade_distances[3];
            }
          }
          if (light_doc.contains("distribution_exponent")) {
            light.distribution_exponent
              = light_doc.at("distribution_exponent").get<float>();
          }
          if (light_doc.contains("transition_fraction")) {
            light.transition_fraction
              = light_doc.at("transition_fraction").get<float>();
          }
          if (light_doc.contains("distance_fadeout_fraction")) {
            light.distance_fadeout_fraction
              = light_doc.at("distance_fadeout_fraction").get<float>();
          }
          if (light_doc.contains("intensity_lux")) {
            light.intensity_lux = light_doc.at("intensity_lux").get<float>();
          }
          prepared.build.directional_lights.push_back(light);
        }
      }

      if (lights_doc.contains("point")) {
        const auto& point_doc = lights_doc.at("point");
        prepared.build.point_lights.reserve(point_doc.size());
        for (size_t i = 0; i < point_doc.size(); ++i) {
          const auto& light_doc = point_doc.at(i);
          const auto object_path
            = std::string { "lights.point[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Point light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::PointLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("range")) {
            light.range = light_doc.at("range").get<float>();
          }
          if (light_doc.contains("source_radius")) {
            light.source_radius = light_doc.at("source_radius").get<float>();
          }
          if (light_doc.contains("luminous_flux_lm")) {
            light.luminous_flux_lm
              = light_doc.at("luminous_flux_lm").get<float>();
          }
          prepared.build.point_lights.push_back(light);
        }
      }

      if (lights_doc.contains("spot")) {
        const auto& spot_doc = lights_doc.at("spot");
        prepared.build.spot_lights.reserve(spot_doc.size());
        for (size_t i = 0; i < spot_doc.size(); ++i) {
          const auto& light_doc = spot_doc.at(i);
          const auto object_path
            = std::string { "lights.spot[" } + std::to_string(i) + "]";
          const auto node_index = light_doc.at("node").get<uint32_t>();
          if (!ValidateNodeIndex(context, node_index, node_count,
                "scene.descriptor.light_node_index_out_of_range",
                "Spot light node index is out of range",
                object_path + ".node")) {
            return std::nullopt;
          }

          auto light = data::pak::world::SpotLightRecord {};
          light.node_index = node_index;
          if (light_doc.contains("common")) {
            ApplyLightCommon(light_doc.at("common"), light.common);
          }
          if (light_doc.contains("range")) {
            light.range = light_doc.at("range").get<float>();
          }
          if (light_doc.contains("inner_cone_angle_radians")) {
            light.inner_cone_angle_radians
              = light_doc.at("inner_cone_angle_radians").get<float>();
          }
          if (light_doc.contains("outer_cone_angle_radians")) {
            light.outer_cone_angle_radians
              = light_doc.at("outer_cone_angle_radians").get<float>();
          }
          if (light_doc.contains("source_radius")) {
            light.source_radius = light_doc.at("source_radius").get<float>();
          }
          if (light_doc.contains("luminous_flux_lm")) {
            light.luminous_flux_lm
              = light_doc.at("luminous_flux_lm").get<float>();
          }
          prepared.build.spot_lights.push_back(light);
        }
      }
    }

    if (descriptor_doc.contains("environment")) {
      const auto& environment = descriptor_doc.at("environment");
      if (environment.contains("post_process_volume")) {
        prepared.post_process = BuildPostProcessSystemRecord(
          context, environment.at("post_process_volume"));
        if (!prepared.post_process.has_value()) {
          return std::nullopt;
        }
      }
      if (environment.contains("background")) {
        prepared.background
          = BuildBackgroundSystemRecord(environment.at("background"));
      }
      if (environment.contains("sky_atmosphere")) {
        prepared.atmosphere
          = BuildSkyAtmosphereSystemRecord(environment.at("sky_atmosphere"));
      }
      if (environment.contains("fog")) {
        prepared.fog = BuildFogSystemRecord(environment.at("fog"));
      }
      if (environment.contains("sky_light")) {
        prepared.sky_light
          = BuildSkyLightSystemRecord(environment.at("sky_light"));
      }
    }

    if (descriptor_doc.contains("local_fog_volumes")) {
      const auto& local_fog_doc = descriptor_doc.at("local_fog_volumes");
      prepared.build.local_fog_volumes.reserve(local_fog_doc.size());
      for (size_t i = 0; i < local_fog_doc.size(); ++i) {
        const auto& volume_doc = local_fog_doc.at(i);
        const auto object_path
          = std::string { "local_fog_volumes[" } + std::to_string(i) + "]";
        const auto node_index = volume_doc.at("node").get<uint32_t>();
        if (!ValidateNodeIndex(context, node_index, node_count,
              "scene.descriptor.local_fog_node_index_out_of_range",
              "Local fog volume node index is out of range",
              object_path + ".node")) {
          return std::nullopt;
        }

        auto record = data::pak::world::LocalFogVolumeRecord {};
        record.node_index = node_index;
        record.enabled = volume_doc.value("enabled", true) ? 1U : 0U;
        record.radial_fog_extinction = volume_doc.value(
          "radial_fog_extinction", record.radial_fog_extinction);
        record.height_fog_extinction = volume_doc.value(
          "height_fog_extinction", record.height_fog_extinction);
        record.height_fog_falloff
          = volume_doc.value("height_fog_falloff", record.height_fog_falloff);
        record.height_fog_offset
          = volume_doc.value("height_fog_offset", record.height_fog_offset);
        record.fog_phase_g
          = volume_doc.value("fog_phase_g", record.fog_phase_g);
        CopyFloatArray(volume_doc.at("fog_albedo"), record.fog_albedo);
        CopyFloatArray(volume_doc.at("fog_emissive"), record.fog_emissive);
        record.sort_priority
          = volume_doc.value("sort_priority", record.sort_priority);
        prepared.build.local_fog_volumes.push_back(record);
      }
    }

    if (descriptor_doc.contains("references")) {
      const auto& refs_doc = descriptor_doc.at("references");

      const auto add_references = [&](const std::string_view key,
                                    const std::optional<data::AssetType> type) {
        if (!refs_doc.contains(key)) {
          return;
        }
        const auto& values = refs_doc.at(key);
        for (size_t i = 0; i < values.size(); ++i) {
          prepared.references.push_back({
            .virtual_path = values.at(i).get<std::string>(),
            .type = type,
            .object_path
            = "references." + std::string(key) + "[" + std::to_string(i) + "]",
          });
        }
      };
      add_references("materials", data::AssetType::kMaterial);
      add_references("scripts", data::AssetType::kScript);
      add_references("input_actions", data::AssetType::kInputAction);
      add_references(
        "input_mapping_contexts", data::AssetType::kInputMappingContext);
      add_references("physics_sidecars", data::AssetType::kPhysicsScene);
      add_references("extra_assets", std::nullopt);
    }

    return prepared;
  }

} // namespace

auto SceneSource::FromDescriptor(const std::string_view bytes,
  const std::filesystem::path& source_path,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<SceneSource>
{
  auto context
    = SourceContext { .source_path = source_path, .diagnostics = diagnostics };
  try {
    const auto document = json::parse(bytes);
    if (!document.is_object()) {
      AddDiagnostic(context, ImportSeverity::kError,
        "scene.descriptor.request_invalid_json",
        "Scene descriptor must be a JSON object");
      return std::nullopt;
    }
    if (!ValidateDescriptorVersion(context, document)
      || !ValidateDescriptorSchema(context, document)) {
      return std::nullopt;
    }
    return PrepareSceneSource(context, document);
  } catch (const json::exception& error) {
    AddDiagnostic(context, ImportSeverity::kError,
      "scene.descriptor.request_invalid_json", error.what());
    return std::nullopt;
  }
}

} // namespace oxygen::content::import::internal
