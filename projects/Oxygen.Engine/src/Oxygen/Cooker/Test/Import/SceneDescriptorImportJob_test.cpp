//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>

namespace oxygen::content::import::test {

namespace {

  auto MakeTempCookedRoot(const std::string_view suffix)
    -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path()
      / "oxygen_scene_descriptor_import_job";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root);
    return root;
  }

  auto WriteTextFile(
    const std::filesystem::path& path, const std::string_view text) -> void
  {
    std::filesystem::create_directories(path.parent_path());
    auto out = std::ofstream(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.is_open());
    out << text;
  }

  auto ReadBinaryFile(const std::filesystem::path& path)
    -> std::vector<std::byte>
  {
    auto in = std::ifstream(path, std::ios::binary);
    EXPECT_TRUE(in.is_open());
    if (!in.is_open()) {
      return {};
    }

    in.seekg(0, std::ios::end);
    const auto size = static_cast<size_t>(in.tellg());
    in.seekg(0, std::ios::beg);

    auto bytes = std::vector<std::byte>(size);
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    EXPECT_TRUE(in.good() || in.eof());
    return bytes;
  }

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    const std::string_view code) -> bool
  {
    return std::ranges::any_of(diagnostics,
      [code](const ImportDiagnostic& d) { return d.code == code; });
  }

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    auto report = ImportReport {};
    std::latch done(1);
    const auto submitted = service.SubmitImport(
      std::move(request),
      [&report, &done](
        const ImportJobId /*job_id*/, const ImportReport& completed) {
        report = completed;
        done.count_down();
      },
      nullptr);
    if (!submitted.has_value()) {
      report.success = false;
      report.diagnostics.push_back({
        .severity = ImportSeverity::kError,
        .code = "test.submit_failed",
        .message = "Failed to submit scene descriptor import job",
        .source_path = {},
        .object_path = {},
      });
      return report;
    }
    done.wait();
    return report;
  }

  class SceneDescriptorImportJobTest : public testing::Test {
  protected:
    auto MakeRequest(const std::filesystem::path& cooked_root,
      std::string descriptor_json, std::string_view job_name = "DemoScene")
      -> ImportRequest
    {
      auto request = ImportRequest {};
      request.source_path = "inline://scene-descriptor";
      request.job_name = std::string(job_name);
      request.cooked_root = cooked_root;
      request.loose_cooked_layout.virtual_mount_root = "/.cooked";
      request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
        .normalized_descriptor_json = std::move(descriptor_json),
      };
      return request;
    }
  };

  auto WriteIndexedReference(const std::filesystem::path& root,
    const data::AssetKey& key, const data::AssetType type) -> void
  {
    auto writer = LooseCookedWriter(root);
    const auto bytes = std::array { std::byte { 1 } };
    writer.WriteAssetDescriptor(key, type, "/Art/Geometry/Mesh.ogeo",
      type == data::AssetType::kGeometry ? "Geometry/Mesh.ogeo"
                                         : "Materials/Wrong.omat",
      bytes);
    static_cast<void>(writer.Finish());
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, OrderedRootsResolveLastMatchingAsset)
  {
    const auto output = MakeTempCookedRoot("ordered_output");
    const auto library = MakeTempCookedRoot("ordered_library");
    const auto own_key = data::AssetKey::FromVirtualPath("/Own/Mesh.ogeo");
    const auto library_key
      = data::AssetKey::FromVirtualPath("/Library/Mesh.ogeo");
    WriteIndexedReference(output, own_key, data::AssetType::kGeometry);
    WriteIndexedReference(library, library_key, data::AssetType::kGeometry);
    auto service = AsyncImportService {};
    const auto descriptor
      = R"({"version":4,"name":"Scene","nodes":[{"name":"Mesh"}],
      "renderables":[{"node":0,"geometry_ref":"/Art/Geometry/Mesh.ogeo"}]})";
    for (const auto own_wins : { false, true }) {
      auto request = MakeRequest(output, descriptor);
      request.cooked_context_roots = own_wins
        ? std::vector<std::filesystem::path> { library, output }
        : std::vector<std::filesystem::path> { output, library };
      const auto report = SubmitAndWait(service, std::move(request));
      ASSERT_TRUE(report.success);
      const auto bytes = ReadBinaryFile(
        output / LooseCookedLayout {}.SceneDescriptorRelPath("Scene"));
      const auto scene = data::SceneAsset(
        data::AssetKey {}, std::span<const std::byte>(bytes));
      const auto renderables
        = scene.GetComponents<data::pak::world::RenderableRecord>();
      ASSERT_EQ(renderables.size(), 1U);
      EXPECT_EQ(renderables[0].geometry_key, own_wins ? own_key : library_key);
    }
    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, WinningRootTypeMismatchDoesNotFallBack)
  {
    const auto output = MakeTempCookedRoot("wrong_winner_output");
    const auto library = MakeTempCookedRoot("wrong_winner_library");
    WriteIndexedReference(output,
      data::AssetKey::FromVirtualPath("/Own/Mesh.ogeo"),
      data::AssetType::kGeometry);
    WriteIndexedReference(library,
      data::AssetKey::FromVirtualPath("/Library/Wrong.omat"),
      data::AssetType::kMaterial);
    auto service = AsyncImportService {};
    auto request = MakeRequest(
      output, R"({"version":4,"name":"Scene","nodes":[{"name":"Mesh"}],
      "renderables":[{"node":0,"geometry_ref":"/Art/Geometry/Mesh.ogeo"}]})");
    request.cooked_context_roots = { library };
    const auto report = SubmitAndWait(service, std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.reference_type_mismatch"));
    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, ResolvesReferencesAndEmitsSceneDescriptor)
  {
    const auto cooked_root = MakeTempCookedRoot("resolves_and_emits");
    WriteTextFile(cooked_root / "Geometry" / "cube.ogeo", "ogeo");
    WriteTextFile(cooked_root / "Materials" / "cube.omat", "omat");
    WriteTextFile(cooked_root / "Scripts" / "spin.oscript", "oscript");
    WriteTextFile(cooked_root / "Input" / "jump.oiact", "oiact");
    WriteTextFile(cooked_root / "Input" / "gameplay.oimap", "oimap");
    WriteTextFile(cooked_root / "Scenes" / "DemoScene.opscene", "physics");

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "version": 4,
      "name": "DemoScene",
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        {
          "node": 1,
          "geometry_ref": "/.cooked/Geometry/cube.ogeo",
          "material_ref": "/.cooked/Materials/cube.omat",
          "visible": true
        }
      ],
      "references": {
        "scripts": ["/.cooked/Scripts/spin.oscript"],
        "input_actions": ["/.cooked/Input/jump.oiact"],
        "input_mapping_contexts": ["/.cooked/Input/gameplay.oimap"],
        "physics_sidecars": ["/.cooked/Scenes/DemoScene.opscene"]
      }
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.scenes_written, 1U);
    EXPECT_FALSE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.reference_missing"));

    auto layout = LooseCookedLayout {};
    const auto expected_relpath = layout.SceneDescriptorRelPath("DemoScene");
    const auto scene_path
      = cooked_root / std::filesystem::path(expected_relpath);
    EXPECT_TRUE(std::filesystem::exists(scene_path));

    const auto scene_bytes = ReadBinaryFile(scene_path);
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));
    const auto renderables
      = scene.GetComponents<oxygen::data::pak::world::RenderableRecord>();
    ASSERT_EQ(renderables.size(), 1U);
    EXPECT_EQ(renderables[0].material_key,
      oxygen::data::AssetKey::FromVirtualPath("/.cooked/Materials/cube.omat"));

    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, MissingGeometryReferenceProducesDiagnostic)
  {
    const auto cooked_root = MakeTempCookedRoot("missing_geometry_reference");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "version": 4,
      "name": "DemoScene",
      "nodes": [
        { "name": "Root" },
        { "name": "MeshNode", "parent": 0 }
      ],
      "renderables": [
        {
          "node": 1,
          "geometry_ref": "/.cooked/Geometry/missing.ogeo"
        }
      ]
    })"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.reference_missing"));

    service.Stop();
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    DirectionalLightManualCascadeDistancesDeriveMaxShadowDistance)
  {
    const auto cooked_root = MakeTempCookedRoot("directional_light_tuning");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "version": 4,
      "name": "DirectionalTuning",
      "nodes": [
        { "name": "Root" },
        { "name": "Sun", "parent": 0 }
      ],
      "lights": {
        "directional": [
          {
            "node": 1,
            "common": { "casts_shadows": true },
            "cascade_count": 4,
            "cascade_distances": [10.0, 30.0, 80.0, 200.0],
            "distribution_exponent": 1.0,
            "intensity_lux": 10000.0
          }
        ]
      }
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.scenes_written, 1U);

    auto layout = LooseCookedLayout {};
    const auto scene_path = cooked_root
      / std::filesystem::path(
        layout.SceneDescriptorRelPath("DirectionalTuning"));
    ASSERT_TRUE(std::filesystem::exists(scene_path));

    const auto scene_bytes = ReadBinaryFile(scene_path);
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));
    const auto directional
      = scene.GetComponents<oxygen::data::pak::world::DirectionalLightRecord>();
    ASSERT_EQ(directional.size(), 1U);
    EXPECT_EQ(directional[0].split_mode, 1U);
    EXPECT_FLOAT_EQ(directional[0].max_shadow_distance, 200.0F);
    EXPECT_FLOAT_EQ(directional[0].cascade_distances[0], 10.0F);
    EXPECT_FLOAT_EQ(directional[0].cascade_distances[1], 30.0F);
    EXPECT_FLOAT_EQ(directional[0].cascade_distances[2], 80.0F);
    EXPECT_FLOAT_EQ(directional[0].cascade_distances[3], 200.0F);
    EXPECT_FLOAT_EQ(directional[0].transition_fraction, 0.1F);
    EXPECT_FLOAT_EQ(directional[0].distance_fadeout_fraction, 0.1F);

    service.Stop();
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    RejectsLegacyDescriptorVersionWithRecookDiagnostic)
  {
    const auto cooked_root = MakeTempCookedRoot("legacy_version");
    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "version": 2,
      "name": "LegacyScene",
      "nodes": [ { "name": "Root" } ]
    })"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.recook_required"));

    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, SerializesV3EnvironmentAndLocalFogRecords)
  {
    const auto cooked_root = MakeTempCookedRoot("environment_and_local_fog");
    WriteTextFile(cooked_root / "Textures" / "sky_probe.otex", "otex");
    WriteTextFile(cooked_root / "Textures" / "fog_probe.otex", "otex");

    auto service = AsyncImportService(AsyncImportService::Config {
      .thread_pool_size = 2U,
    });

    const auto report = SubmitAndWait(service, MakeRequest(cooked_root, R"({
      "version": 4,
      "name": "EnvironmentScene",
      "nodes": [
        { "name": "Root" },
        { "name": "FogVolumeNode", "parent": 0 }
      ],
      "environment": {
        "fog": {
          "enabled": true,
          "model": 1,
          "extinction_sigma_t_per_m": 0.01,
          "height_falloff_per_m": 0.2,
          "height_offset_m": 0.0,
          "start_distance_m": 1.0,
          "max_opacity": 0.8,
          "single_scattering_albedo_rgb": [0.9, 0.8, 0.7],
          "anisotropy_g": 0.25,
          "enable_height_fog": true,
          "enable_volumetric_fog": true,
          "second_fog_density": 0.03,
          "second_fog_height_falloff": 0.15,
          "second_fog_height_offset": 10.0,
          "fog_inscattering_luminance": [1.0, 0.9, 0.8],
          "sky_atmosphere_ambient_contribution_color_scale": [0.2, 0.3, 0.4],
          "inscattering_color_cubemap_ref": "/.cooked/Textures/fog_probe.otex",
          "inscattering_color_cubemap_angle": 0.5,
          "inscattering_texture_tint": [0.7, 0.6, 0.5],
          "fully_directional_inscattering_color_distance": 100.0,
          "non_directional_inscattering_color_distance": 50.0,
          "directional_inscattering_luminance": [0.1, 0.2, 0.3],
          "directional_inscattering_exponent": 4.0,
          "directional_inscattering_start_distance": 15.0,
          "end_distance_m": 2500.0,
          "fog_cutoff_distance_m": 3000.0,
          "volumetric_fog_scattering_distribution": 0.6,
          "volumetric_fog_albedo": [0.5, 0.6, 0.7],
          "volumetric_fog_emissive": [0.0, 0.1, 0.2],
          "volumetric_fog_extinction_scale": 1.5,
          "volumetric_fog_distance": 500.0,
          "volumetric_fog_start_distance": 3.0,
          "volumetric_fog_near_fade_in_distance": 4.0,
          "volumetric_fog_static_lighting_scattering_intensity": 2.0,
          "override_light_colors_with_fog_inscattering_colors": true,
          "holdout": false,
          "render_in_main_pass": true,
          "visible_in_reflection_captures": true,
          "visible_in_real_time_sky_captures": false
        },
        "sky_light": {
          "enabled": true,
          "source": 1,
          "cubemap_ref": "/.cooked/Textures/sky_probe.otex",
          "intensity": 1.5,
          "tint_rgb": [0.8, 0.9, 1.0],
          "diffuse_intensity": 1.1,
          "specular_intensity": 1.2,
          "real_time_capture_enabled": true,
          "source_cubemap_angle_radians": 0.75,
          "lower_hemisphere_color": [0.1, 0.2, 0.3],
          "lower_hemisphere_is_solid_color": false,
          "lower_hemisphere_blend_alpha": 0.35,
          "volumetric_scattering_intensity": 0.4,
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
    })"));

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.scenes_written, 1U);

    auto layout = LooseCookedLayout {};
    const auto scene_path = cooked_root
      / std::filesystem::path(
        layout.SceneDescriptorRelPath("EnvironmentScene"));
    ASSERT_TRUE(std::filesystem::exists(scene_path));

    const auto scene_bytes = ReadBinaryFile(scene_path);
    ASSERT_FALSE(scene_bytes.empty());

    const auto scene = oxygen::data::SceneAsset(
      oxygen::data::AssetKey {}, std::span<const std::byte>(scene_bytes));

    const auto fog = scene.TryGetFogEnvironment();
    ASSERT_TRUE(fog.has_value());
    EXPECT_EQ(fog->enable_height_fog, 1U);
    EXPECT_EQ(fog->enable_volumetric_fog, 1U);
    EXPECT_FLOAT_EQ(fog->second_fog_density, 0.03F);
    EXPECT_FLOAT_EQ(fog->volumetric_fog_extinction_scale, 1.5F);
    EXPECT_EQ(fog->override_light_colors_with_fog_inscattering_colors, 1U);
    EXPECT_EQ(fog->visible_in_real_time_sky_captures, 0U);

    const auto sky_light = scene.TryGetSkyLightEnvironment();
    ASSERT_TRUE(sky_light.has_value());
    EXPECT_EQ(sky_light->real_time_capture_enabled, 1U);
    EXPECT_FLOAT_EQ(sky_light->source_cubemap_angle_radians, 0.75F);
    EXPECT_EQ(sky_light->lower_hemisphere_is_solid_color, 0U);
    EXPECT_FLOAT_EQ(sky_light->lower_hemisphere_blend_alpha, 0.35F);
    EXPECT_FLOAT_EQ(sky_light->volumetric_scattering_intensity, 0.4F);

    const auto local_fog
      = scene.GetComponents<oxygen::data::pak::world::LocalFogVolumeRecord>();
    ASSERT_EQ(local_fog.size(), 1U);
    EXPECT_EQ(local_fog[0].node_index, 1U);
    EXPECT_EQ(local_fog[0].enabled, 1U);
    EXPECT_FLOAT_EQ(local_fog[0].radial_fog_extinction, 0.3F);
    EXPECT_EQ(local_fog[0].sort_priority, 2);

    service.Stop();
  }

  NOLINT_TEST_F(
    SceneDescriptorImportJobTest, CompletePostProcessAndBackgroundRoundTrip)
  {
    auto service = AsyncImportService(
      AsyncImportService::Config { .thread_pool_size = 2U });
    const auto root = MakeTempCookedRoot("complete_environment");
    auto document = nlohmann::json::parse(
      R"JSON(
{
  "version": 4,
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
    for (uint32_t tone = 0; tone != 4; ++tone) {
      for (uint32_t exposure = 0; exposure != 3; ++exposure) {
        for (uint32_t metering = 0; metering != 3; ++metering) {
          auto& input = document["environment"]["post_process_volume"];
          input["tone_mapper"] = tone;
          input["exposure_mode"] = exposure;
          input["auto_exposure_metering_mode"] = metering;
          input["exposure_enabled"] = exposure == 1;
          SCOPED_TRACE(document.dump());
          const auto report = SubmitAndWait(
            service, MakeRequest(root, document.dump(), "Environment"));
          ASSERT_TRUE(report.success);
          EXPECT_EQ(report.scenes_written, 1U);
          const auto path
            = root / LooseCookedLayout {}.SceneDescriptorRelPath("Environment");
          const auto bytes = ReadBinaryFile(path);
          const auto scene = data::SceneAsset(
            data::AssetKey {}, std::span<const std::byte>(bytes));
          const auto post = scene.TryGetPostProcessVolumeEnvironment();
          ASSERT_TRUE(post.has_value());
          EXPECT_EQ(post->header.record_size, 104U);
          EXPECT_EQ(static_cast<uint32_t>(post->tone_mapper), tone);
          EXPECT_EQ(static_cast<uint32_t>(post->exposure_mode), exposure);
          EXPECT_EQ(
            static_cast<uint32_t>(post->auto_exposure_metering_mode), metering);
          EXPECT_EQ(post->exposure_enabled, exposure == 1 ? 1U : 0U);
          EXPECT_FLOAT_EQ(post->exposure_compensation_ev, 1.25F);
          EXPECT_FLOAT_EQ(post->exposure_key, 8.75F);
          EXPECT_FLOAT_EQ(post->manual_exposure_ev, 11.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_min_ev, -3.25F);
          EXPECT_FLOAT_EQ(post->auto_exposure_max_ev, 12.75F);
          EXPECT_FLOAT_EQ(post->auto_exposure_speed_up, 5.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_speed_down, 1.75F);
          EXPECT_FLOAT_EQ(post->auto_exposure_low_percentile, 0.2F);
          EXPECT_FLOAT_EQ(post->auto_exposure_high_percentile, 0.85F);
          EXPECT_FLOAT_EQ(post->auto_exposure_min_log_luminance, -10.5F);
          EXPECT_FLOAT_EQ(post->auto_exposure_log_luminance_range, 21.25F);
          EXPECT_FLOAT_EQ(post->auto_exposure_target_luminance, 0.27F);
          EXPECT_FLOAT_EQ(post->auto_exposure_spot_meter_radius, 0.35F);
          EXPECT_FLOAT_EQ(post->bloom_intensity, 0.6F);
          EXPECT_FLOAT_EQ(post->bloom_threshold, 2.25F);
          EXPECT_FLOAT_EQ(post->saturation, 0.8F);
          EXPECT_FLOAT_EQ(post->contrast, 1.4F);
          EXPECT_FLOAT_EQ(post->vignette_intensity, 0.3F);
          EXPECT_FLOAT_EQ(post->display_gamma, 2.4F);
          const auto background = scene.TryGetBackgroundEnvironment();
          ASSERT_TRUE(background.has_value());
          EXPECT_EQ(background->enabled, 1U);
          EXPECT_EQ(background->header.record_size, 24U);
          EXPECT_FLOAT_EQ(background->color_rgb[0], 0.05F);
          EXPECT_FLOAT_EQ(background->color_rgb[1], 0.25F);
          EXPECT_FLOAT_EQ(background->color_rgb[2], 0.75F);
        }
      }
    }
    service.Stop();
  }

  NOLINT_TEST_F(SceneDescriptorImportJobTest,
    RejectsPreviousSceneVersionWithRecookDiagnostic)
  {
    auto service = AsyncImportService(
      AsyncImportService::Config { .thread_pool_size = 1U });
    const auto root = MakeTempCookedRoot("previous_scene_version");
    const auto report = SubmitAndWait(service,
      MakeRequest(
        root, R"({"version":3,"name":"Old","nodes":[{"name":"Root"}]})"));
    EXPECT_FALSE(report.success);
    EXPECT_EQ(report.scenes_written, 0U);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "scene.descriptor.recook_required"));
    service.Stop();
  }

} // namespace

} // namespace oxygen::content::import::test
