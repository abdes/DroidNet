//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Schemas/oxygen.scene-descriptor.schema.json

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Cooker/Test/Support/SceneDescriptorTestData.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using nlohmann::json;
using oxygen::content::import::test::MakeCurrentSceneDescriptor;
using oxygen::cooker::test::LoadSchema;
using oxygen::cooker::test::SchemaCase;
using oxygen::cooker::test::ValidateJson;
using ::testing::Contains;
using ::testing::HasSubstr;
using ::testing::IsEmpty;

auto Schema() -> const json&
{
  static const auto schema
    = LoadSchema("Import/Schemas/oxygen.scene-descriptor.schema.json");
  return schema;
}

// The documents in the table below spell out `"version": 10`; this test fails
// loudly when the schema and the scene asset version drift apart.
NOLINT_TEST(SceneDescriptorSchemaTest, SchemaVersionMatchesSceneAssetVersion)
{
  EXPECT_EQ(Schema().at("properties").at("version").at("const"),
    MakeCurrentSceneDescriptor("{}").at("version"));
  EXPECT_EQ(Schema().at("properties").at("version").at("const"), 10);
}

class SceneDescriptorSchemaCaseTest
  : public ::testing::TestWithParam<SchemaCase> { };

NOLINT_TEST_P(SceneDescriptorSchemaCaseTest, ValidatesDocument)
{
  oxygen::cooker::test::ExpectSchemaCase(Schema(), GetParam());
}

INSTANTIATE_TEST_SUITE_P(Cases, SceneDescriptorSchemaCaseTest,
  ::testing::Values(
    SchemaCase {
      "AcceptsCanonicalDocument",
      R"({ "version": 10,
    "$schema": "./src/Oxygen/Cooker/Import/Schemas/oxygen.scene-descriptor.schema.json",
    "name": "DemoScene",
    "nodes": [
      { "name": "Root", "transform": { "translation": [0, 0, 0] } },
      { "name": "MeshNode", "parent": 0, "transform": { "translation": [1, 0, 0] } }
    ],
    "renderables": [
      { "node": 1, "geometry_ref": "/.cooked/Geometry/cube.ogeo",
        "material_overrides": [{
          "slot_id": "00000000-0000-0000-0000-000000000001",
          "material_ref": "/.cooked/Materials/cube.omat",
          "layout_revision": "0000000000000000000000000000000000000000000000000000000000000001"
        }]
      }
    ],
    "cameras": {
      "perspective": [
        { "node": 0, "aspect_mode": "auto", "fov_y": 1.2, "near_plane": 0.1, "far_plane": 500.0 }
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
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsUnknownNestedFields",
      R"({ "version": 10,
    "name": "BadScene",
    "nodes": [ { "name": "Root", "unknown_field": true } ]
  })",
      false,
      "'unknown_field'",
    },
    SchemaCase {
      "AcceptsDirectionalShadowTuningFields",
      R"({ "version": 10,
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
  })",
      true,
      "",
    },
    SchemaCase {
      "AcceptsCurrentEnvironmentAndLocalFogShape",
      R"({ "version": 10,
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
        "source_cubemap_angle_radians": 0.25,
        "lower_hemisphere_color": [0.1, 0.1, 0.2],
        "lower_hemisphere_is_solid_color": true,
        "lower_hemisphere_blend_alpha": 1.0,
        "volumetric_scattering_intensity": 0.5,
        "affect_reflections": true
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
  })",
      true,
      "",
    },
    SchemaCase {
      "RejectsMissingVersion",
      R"({
    "name": "LegacyScene",
    "nodes": [ { "name": "Root" } ]
  })",
      false,
      "'version'",
    }),
  oxygen::cooker::test::SchemaCaseName);

NOLINT_TEST(SceneDescriptorSchemaTest, NodeFlagSourceModesAreCanonical)
{
  auto document
    = MakeCurrentSceneDescriptor(R"({"name":"Flags","nodes":[{"flags":{}}]})");
  for (const auto& visibility : { "inherit", "shown", "hidden" }) {
    for (const auto& shadow : { "inherit", "on", "off" }) {
      document.at("nodes").at(0).update({ { "flags",
        {
          { "visible", visibility },
          { "casts_shadows", shadow },
          { "receives_shadows", shadow },
          { "static", false },
        } } });
      EXPECT_THAT(ValidateJson(Schema(), document), IsEmpty());
    }
  }
  struct Invalid {
    json flags;
    std::string error_substr;
  };
  for (const auto& invalid :
    std::vector<Invalid> {
      { { { "visible", true } }, "/nodes/0/flags/visible: " },
      { { { "casts_shadows", false } }, "/nodes/0/flags/casts_shadows: " },
      { { { "receives_shadows", true } }, "/nodes/0/flags/receives_shadows: " },
      { { { "visible", "on" } }, "/nodes/0/flags/visible: " },
      { { { "casts_shadows", "shown" } }, "/nodes/0/flags/casts_shadows: " },
      { { { "visible", "local" } }, "/nodes/0/flags/visible: " },
    }) {
    document.at("nodes").at(0).update({ { "flags", invalid.flags } });
    EXPECT_THAT(ValidateJson(Schema(), document),
      Contains(HasSubstr(invalid.error_substr)))
      << invalid.flags.dump();
  }
  document.at("nodes").at(0).update({ { "flags", json::object() } });
  document.update({ { "version", 4 } });
  EXPECT_THAT(
    ValidateJson(Schema(), document), Contains(HasSubstr("/version: ")));
}

NOLINT_TEST(SceneDescriptorSchemaTest, RequiresExplicitCameraAspectPolicy)
{
  auto doc = MakeCurrentSceneDescriptor(R"({
    "name": "CameraPolicy", "nodes": [{}],
    "cameras": { "perspective": [{ "node": 0, "aspect_mode": "auto" }] }
  })");
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  auto& camera = doc.at("cameras").at("perspective").at(0);
  camera.update({ { "aspect_mode", "fixed" }, { "aspect_ratio", 1.5 } });
  EXPECT_THAT(ValidateJson(Schema(), doc), IsEmpty());
  camera.update({ { "aspect_mode", "stretch" } });
  EXPECT_THAT(ValidateJson(Schema(), doc),
    Contains(HasSubstr("/cameras/perspective/0/aspect_mode: ")));
  camera.erase("aspect_mode");
  EXPECT_THAT(
    ValidateJson(Schema(), doc), Contains(HasSubstr("'aspect_mode'")));
}

NOLINT_TEST(
  SceneDescriptorSchemaTest, PostProcessAndBackgroundRejectInvalidEncodings)
{
  const auto base = MakeCurrentSceneDescriptor(R"JSON(
{
  "name": "Environment",
  "nodes": [
    {
      "name": "Root"
    }
  ],
  "environment": {
    "post_process_volume": {
      "tone_mapper": 1,
      "exposure_mode": 2,
      "exposure_enabled": false,
      "exposure_compensation_ev": 1.25,
      "exposure_key": 8.75,
      "manual_exposure_ev": 11.5,
      "auto_exposure_min_ev": -3.25,
      "auto_exposure_max_ev": 12.75,
      "auto_exposure_speed_up": 5.5,
      "auto_exposure_speed_down": 1.75,
      "auto_exposure_metering_mode": 0,
      "auto_exposure_low_percentile": 0.2,
      "auto_exposure_high_percentile": 0.85,
      "auto_exposure_min_log_luminance": -10.5,
      "auto_exposure_log_luminance_range": 21.25,
      "auto_exposure_target_luminance": 0.27,
      "auto_exposure_spot_meter_radius": 0.35,
      "bloom_intensity": 0.6,
      "bloom_threshold": 2.25,
      "saturation": 0.8,
      "contrast": 1.4,
      "vignette_intensity": 0.3,
      "display_gamma": 2.4,
      "enabled": true
    },
    "background": {
      "enabled": true,
      "color_rgb": [
        0.05,
        0.25,
        0.75
      ]
    }
  }
}
)JSON");
  EXPECT_THAT(ValidateJson(Schema(), base), IsEmpty());
  for (const auto& pointer : {
         "/environment/post_process_volume/tone_mapper",
         "/environment/post_process_volume/exposure_mode",
         "/environment/post_process_volume/auto_exposure_metering_mode",
       }) {
    auto invalid = base;
    invalid.at(json::json_pointer(pointer)) = 99;
    EXPECT_THAT(ValidateJson(Schema(), invalid),
      Contains(HasSubstr(std::string(pointer) + ": ")))
      << pointer;
  }
  for (const float channel : { -0.1F, 1.1F }) {
    auto invalid = base;
    invalid.at("environment").at("background").at("color_rgb").at(0) = channel;
    EXPECT_THAT(ValidateJson(Schema(), invalid),
      Contains(HasSubstr("/environment/background/color_rgb/0: ")));
  }
  auto overflow = base;
  overflow.at("environment")
    .at("post_process_volume")
    .update({ { "manual_exposure_ev", 1e100 } });
  EXPECT_THAT(ValidateJson(Schema(), overflow),
    Contains(
      HasSubstr("/environment/post_process_volume/manual_exposure_ev: ")));
  auto old = base;
  old.update({ { "version", 3 } });
  EXPECT_THAT(ValidateJson(Schema(), old), Contains(HasSubstr("/version: ")));
}

NOLINT_TEST(SceneDescriptorSchemaTest, RejectsRetiredCaptureModeAndOldVersion)
{
  const auto canonical = MakeCurrentSceneDescriptor(R"({
    "name": "AutomaticSky", "nodes": [{}],
    "environment": { "sky_light": {
      "enabled": true, "source": 0, "intensity": 2.5,
      "tint_rgb": [0.2, 0.4, 0.8], "diffuse_intensity": 0.5,
      "specular_intensity": 1.5, "lower_hemisphere_color": [0.1, 0.2, 0.3],
      "volumetric_scattering_intensity": 0.25, "affect_reflections": true
    } }
  })");
  EXPECT_THAT(ValidateJson(Schema(), canonical), IsEmpty());
  for (const bool value : { false, true }) {
    auto retired = canonical;
    retired.at("environment")
      .at("sky_light")
      .update({ { "real_time_capture_enabled", value } });
    EXPECT_THAT(ValidateJson(Schema(), retired),
      Contains(HasSubstr("'real_time_capture_enabled'")));
  }
  auto previous = canonical;
  previous.update({ { "version", 7 } });
  EXPECT_THAT(
    ValidateJson(Schema(), previous), Contains(HasSubstr("/version: ")));
}

} // namespace
