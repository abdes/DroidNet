//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>

#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using nlohmann::json_schema::error_handler;
using nlohmann::json_schema::json_validator;

class CollectingErrorHandler final : public error_handler {
public:
  void error(const json::json_pointer& ptr, const json& instance,
    const std::string& message) override
  {
    auto out = std::ostringstream {};
    const auto path = ptr.to_string();
    out << (path.empty() ? "<root>" : path) << ": " << message;
    if (!instance.is_discarded()) {
      out << " (value=" << instance.dump() << ")";
    }
    errors_.push_back(out.str());
  }

  [[nodiscard]] auto HasErrors() const noexcept -> bool
  {
    return !errors_.empty();
  }

  [[nodiscard]] auto ToString() const -> std::string
  {
    auto out = std::ostringstream {};
    for (const auto& error : errors_) {
      out << "- " << error << "\n";
    }
    return out.str();
  }

private:
  std::vector<std::string> errors_;
};

auto FindRepoRoot() -> std::filesystem::path
{
  auto path = std::filesystem::path(__FILE__).parent_path();
  while (!path.empty()) {
    if (std::filesystem::exists(path / "src" / "Oxygen" / "Cooker" / "Import"
          / "Schemas" / "oxygen.scene-descriptor.schema.json")) {
      return path;
    }
    path = path.parent_path();
  }
  return {};
}

auto SchemaFile(const std::filesystem::path& repo_root) -> std::filesystem::path
{
  return repo_root / "src" / "Oxygen" / "Cooker" / "Import" / "Schemas"
    / "oxygen.scene-descriptor.schema.json";
}

auto LoadJsonFile(const std::filesystem::path& path) -> std::optional<json>
{
  auto in = std::ifstream(path);
  if (!in) {
    return std::nullopt;
  }
  try {
    auto parsed = json {};
    in >> parsed;
    return parsed;
  } catch (...) {
    return std::nullopt;
  }
}

auto ValidateSchema(
  const json& schema, const json& instance, std::string& errors) -> bool
{
  try {
    auto validator = json_validator {};
    validator.set_root_schema(schema);
    auto handler = CollectingErrorHandler {};
    [[maybe_unused]] auto _ = validator.validate(instance, handler);
    if (handler.HasErrors()) {
      errors = handler.ToString();
      return false;
    }
    return true;
  } catch (const std::exception& ex) {
    errors = ex.what();
    return false;
  }
}

NOLINT_TEST(SceneDescriptorJsonSchemaTest, AcceptsCanonicalDocument)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json",
    "version": 3,
    "name": "DemoScene",
    "nodes": [
      { "name": "Root", "transform": { "translation": [0, 0, 0] } },
      { "name": "MeshNode", "parent": 0, "transform": { "translation": [1, 0, 0] } }
    ],
    "renderables": [
      { "node": 1, "geometry_ref": "/.cooked/Geometry/cube.ogeo", "material_ref": "/.cooked/Materials/cube.omat" }
    ],
    "cameras": {
      "perspective": [
        { "node": 0, "fov_y": 1.2, "near_plane": 0.1, "far_plane": 500.0 }
      ]
    },
    "lights": {
      "directional": [
        { "node": 0, "common": { "casts_shadows": true }, "intensity_lux": 10000.0 }
      ]
    },
    "references": {
      "scripts": ["/.cooked/Scripts/spin.oscript"],
      "input_actions": ["/.cooked/Input/jump.oiact"],
      "input_mapping_contexts": ["/.cooked/Input/gameplay.oimap"],
      "physics_sidecars": ["/.cooked/Scenes/DemoScene.opscene"]
    }
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(SceneDescriptorJsonSchemaTest, RejectsUnknownNestedFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "version": 3,
    "name": "BadScene",
    "nodes": [ { "name": "Root", "unknown_field": true } ]
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

NOLINT_TEST(SceneDescriptorJsonSchemaTest, AcceptsDirectionalShadowTuningFields)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "version": 3,
    "name": "TunedScene",
    "nodes": [
      { "name": "Root" }
    ],
    "lights": {
      "directional": [
        {
          "node": 0,
          "common": { "casts_shadows": true },
          "cascade_count": 4,
          "split_mode": 0,
          "max_shadow_distance": 160.0,
          "distribution_exponent": 3.0,
          "transition_fraction": 0.1,
          "distance_fadeout_fraction": 0.1,
          "intensity_lux": 10000.0
        }
      ]
    }
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(SceneDescriptorJsonSchemaTest, AcceptsV3EnvironmentAndLocalFogShape)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "version": 3,
    "name": "FogScene",
    "nodes": [
      { "name": "Root" },
      { "name": "FogVolumeNode", "parent": 0 }
    ],
    "environment": {
      "sky_atmosphere": {
        "enabled": true,
        "planet_radius_m": 6360000.0,
        "atmosphere_height_m": 80000.0,
        "ground_albedo_rgb": [0.1, 0.1, 0.1],
        "rayleigh_scattering_rgb": [0.0000058, 0.0000135, 0.0000331],
        "rayleigh_scale_height_m": 8000.0,
        "mie_scattering_rgb": [0.000021, 0.000021, 0.000021],
        "mie_absorption_rgb": [0.000004, 0.000004, 0.000004],
        "mie_scale_height_m": 1200.0,
        "mie_anisotropy": 0.8,
        "ozone_absorption_rgb": [0.00065, 0.001881, 0.000085],
        "ozone_density_profile": [25000.0, 0.0, 0.0],
        "multi_scattering_factor": 1.0,
        "sky_luminance_factor_rgb": [1.0, 1.0, 1.0],
        "sky_and_aerial_perspective_luminance_factor_rgb": [1.0, 1.0, 1.0],
        "aerial_perspective_distance_scale": 1.0,
        "aerial_scattering_strength": 1.0,
        "aerial_perspective_start_depth_m": 100.0,
        "height_fog_contribution": 1.0,
        "trace_sample_count_scale": 1.0,
        "transmittance_min_light_elevation_deg": -90.0,
        "sun_disk_enabled": true,
        "holdout": false,
        "render_in_main_pass": true
      },
      "fog": {
        "enabled": true,
        "model": 1,
        "extinction_sigma_t_per_m": 0.01,
        "height_falloff_per_m": 0.2,
        "height_offset_m": 0.0,
        "start_distance_m": 0.0,
        "max_opacity": 1.0,
        "single_scattering_albedo_rgb": [1.0, 1.0, 1.0],
        "anisotropy_g": 0.2,
        "enable_height_fog": true,
        "enable_volumetric_fog": true,
        "second_fog_density": 0.02,
        "second_fog_height_falloff": 0.1,
        "second_fog_height_offset": 10.0,
        "fog_inscattering_luminance": [1.0, 1.0, 1.0],
        "sky_atmosphere_ambient_contribution_color_scale": [1.0, 1.0, 1.0],
        "inscattering_color_cubemap_ref": "/.cooked/Textures/fog_probe.otex",
        "inscattering_color_cubemap_angle": 0.0,
        "inscattering_texture_tint": [1.0, 1.0, 1.0],
        "fully_directional_inscattering_color_distance": 1000.0,
        "non_directional_inscattering_color_distance": 500.0,
        "directional_inscattering_luminance": [1.0, 1.0, 1.0],
        "directional_inscattering_exponent": 4.0,
        "directional_inscattering_start_distance": 50.0,
        "end_distance_m": 5000.0,
        "fog_cutoff_distance_m": 6000.0,
        "volumetric_fog_scattering_distribution": 0.5,
        "volumetric_fog_albedo": [0.8, 0.8, 0.8],
        "volumetric_fog_emissive": [0.0, 0.0, 0.0],
        "volumetric_fog_extinction_scale": 1.0,
        "volumetric_fog_distance": 1000.0,
        "volumetric_fog_start_distance": 0.0,
        "volumetric_fog_near_fade_in_distance": 10.0,
        "volumetric_fog_static_lighting_scattering_intensity": 1.0,
        "override_light_colors_with_fog_inscattering_colors": false,
        "holdout": false,
        "render_in_main_pass": true,
        "visible_in_reflection_captures": true,
        "visible_in_real_time_sky_captures": true
      },
      "sky_light": {
        "enabled": true,
        "source": 1,
        "cubemap_ref": "/.cooked/Textures/sky_probe.otex",
        "intensity": 1.0,
        "tint_rgb": [1.0, 1.0, 1.0],
        "diffuse_intensity": 1.0,
        "specular_intensity": 1.0,
        "real_time_capture_enabled": true,
        "lower_hemisphere_color": [0.1, 0.1, 0.2],
        "volumetric_scattering_intensity": 0.5,
        "affect_reflections": true,
        "affect_global_illumination": true
      }
    },
    "local_fog_volumes": [
      {
        "node": 1,
        "enabled": true,
        "radial_fog_extinction": 0.3,
        "height_fog_extinction": 0.2,
        "height_fog_falloff": 0.15,
        "height_fog_offset": 1.25,
        "fog_phase_g": 0.4,
        "fog_albedo": [0.7, 0.8, 0.9],
        "fog_emissive": [0.1, 0.2, 0.3],
        "sort_priority": 2
      }
    ]
  })");

  auto errors = std::string {};
  EXPECT_TRUE(ValidateSchema(*schema, doc, errors)) << errors;
}

NOLINT_TEST(SceneDescriptorJsonSchemaTest, RejectsMissingV3Version)
{
  const auto repo_root = FindRepoRoot();
  ASSERT_FALSE(repo_root.empty());
  const auto schema = LoadJsonFile(SchemaFile(repo_root));
  ASSERT_TRUE(schema.has_value());

  const auto doc = json::parse(R"({
    "name": "LegacyScene",
    "nodes": [ { "name": "Root" } ]
  })");

  auto errors = std::string {};
  EXPECT_FALSE(ValidateSchema(*schema, doc, errors));
}

} // namespace
