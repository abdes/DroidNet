//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Import/TextureSourceAssembly.h>
#include <Oxygen/Core/Types/ColorSpace.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples {
namespace {

  auto ReadSettingsBytes(const std::filesystem::path& path) -> std::string
  {
    std::ifstream input(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>() };
  }

  NOLINT_TEST(SettingsServicePersistenceTest,
    BatchEditsRemainTransientIncludingExplicitSaveAndDestruction)
  {
    const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-settings-" + Uuid::Generate().ToString() + ".json");
    [[maybe_unused]] const auto cleanup = Finally([&path] {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    });
    constexpr auto kSaved
      = R"json({ "theme": "dark", "post_process": { "gamma": 2.2 } })json"
        "\n";
    {
      std::ofstream output(path, std::ios::binary);
      output << kSaved;
    }
    {
      SettingsService settings(path);
      settings.SetPersistenceEnabled(false);
      settings.SetFloat("post_process.gamma", 1.8F);
      EXPECT_EQ(settings.GetFloat("post_process.gamma"), 1.8F);
      settings.Save();
      EXPECT_EQ(ReadSettingsBytes(path), kSaved);
    }
    EXPECT_EQ(ReadSettingsBytes(path), kSaved);
  }

  NOLINT_TEST(SettingsServicePersistenceTest, BatchDoesNotCreateSettingsFile)
  {
    const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-settings-" + Uuid::Generate().ToString() + ".json");
    {
      SettingsService settings(path);
      settings.SetPersistenceEnabled(false);
      settings.SetBool("post_process.enabled", false);
      settings.Save();
    }
    EXPECT_FALSE(std::filesystem::exists(path));
  }

  //! Loading drops settings no code reads and the objects they leave empty.
  NOLINT_TEST(SettingsServicePersistenceTest, LoadRemovesRetiredSettings)
  {
    const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-settings-" + Uuid::Generate().ToString() + ".json");
    [[maybe_unused]] const auto cleanup = Finally([&path] {
      std::error_code ignored;
      std::filesystem::remove(path, ignored);
    });
    {
      std::ofstream output(path, std::ios::binary);
      output << R"json({
        "rendering": { "view_mode": "solid", "debug_mode": 0 },
        "ui": { "stats": { "show_fps": true }, "axes": { "visible": false } }
      })json";
    }
    {
      SettingsService settings(path);
      EXPECT_FALSE(settings.GetString("rendering.view_mode").has_value());
      EXPECT_FALSE(settings.GetBool("ui.stats.show_fps").has_value());
      EXPECT_EQ(settings.GetBool("ui.axes.visible"), false);
      settings.Save();
    }
    const auto saved = ReadSettingsBytes(path);
    EXPECT_EQ(saved.find("rendering"), std::string::npos);
    EXPECT_EQ(saved.find("stats"), std::string::npos);
    EXPECT_NE(saved.find("axes"), std::string::npos);
  }

  //! Integers keep their type, and an object's entries can be listed.
  NOLINT_TEST(SettingsServicePersistenceTest, StoresIntegersAndListsChildren)
  {
    const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-settings-" + Uuid::Generate().ToString() + ".json");
    SettingsService settings(path);
    settings.SetPersistenceEnabled(false);
    settings.SetInt("scenes.a.last_use", 9007199254740993);
    settings.SetFloat("scenes.b.last_use", 2.5F);
    settings.SetBool("flag", true);

    EXPECT_EQ(settings.GetInt("scenes.a.last_use"), 9007199254740993);
    EXPECT_FALSE(settings.GetInt("scenes.b.last_use").has_value());
    EXPECT_THAT(settings.GetChildKeys("scenes"),
      ::testing::UnorderedElementsAre("a", "b"));
    EXPECT_TRUE(settings.GetChildKeys("flag").empty());
    EXPECT_TRUE(settings.GetChildKeys("missing").empty());
  }

  //! The demo app's settings file of this test process; the demo settings
  //! are initialized once per process, unsaved.
  auto DemoSettingsPath() -> const std::filesystem::path&
  {
    static const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-import-settings-" + Uuid::Generate().ToString() + ".json");
    return path;
  }

  //! The demo app's settings without the content import keys `keys`.
  auto DemoSettings(std::span<const std::string_view> keys)
    -> observer_ptr<SettingsService>
  {
    static const auto initialized = [] {
      SettingsService::InitializeForDemoApp(DemoSettingsPath());
      return true;
    }();
    static_cast<void>(initialized);
    const auto settings = SettingsService::ForDemoApp();
    settings->SetPersistenceEnabled(false);
    for (const auto key : keys) {
      settings->Remove(key);
    }
    return settings;
  }

  //! The demo app's settings without any content panel value.
  auto ContentSettings() -> observer_ptr<SettingsService>
  {
    constexpr auto kContent = std::array { std::string_view { "content" } };
    return DemoSettings(kContent);
  }

  //! A fresh install reads the fast texture defaults, the panel's importer
  //! and no mounts or scene.
  NOLINT_TEST(ContentSettingsTest, AbsentValuesReadAsDefaults)
  {
    ContentSettings();
    const ContentSettingsService service;

    const auto profile = service.GetImportProfile();
    const auto defaults = DefaultImportProfile();
    EXPECT_TRUE(profile.tuning.enabled);
    EXPECT_EQ(
      profile.tuning.mip_policy, content::import::MipPolicy::kFullChain);
    EXPECT_EQ(profile.tuning.mip_filter, content::import::MipFilter::kBox);
    EXPECT_EQ(profile.tuning.bc7_quality, content::import::Bc7Quality::kFast);
    EXPECT_EQ(
      profile.importer.thread_pool_size, defaults.importer.thread_pool_size);
    EXPECT_EQ(profile.options.coordinate.up_axis,
      content::import::SourceUpAxis::kFromFile);
    EXPECT_TRUE(service.GetMountSources().empty());
    EXPECT_TRUE(service.GetMountedImportRecords().empty());
    EXPECT_FALSE(service.GetLoadedScene().has_value());
    EXPECT_TRUE(service.GetRecentScenes().empty());
    EXPECT_EQ(service.GetImportTab(), ContentImportTabSettings {});
    EXPECT_FALSE(std::filesystem::exists(DemoSettingsPath()));
  }

  //! Every import option and importer setting the panel edits round trips,
  //! the eleven options earlier panels did not save included.
  NOLINT_TEST(ContentSettingsTest, ImportProfileRoundTrips)
  {
    ContentSettings();
    ContentSettingsService service;
    using namespace content::import; // NOLINT(*-using-namespace)
    auto profile = service.GetImportProfile();
    auto& o = profile.options;
    o.import_content
      = ImportContentFlags::kGeometry | ImportContentFlags::kScene;
    o.node_pruning = NodePruningPolicy::kDropEmptyNodes;
    o.coordinate.unit_normalization
      = UnitNormalizationPolicy::kApplyCustomFactor;
    o.coordinate.unit_scale = 0.25F;
    o.coordinate.up_axis = SourceUpAxis::kZ;
    o.normal_policy = GeometryAttributePolicy::kAlwaysRecalculate;
    o.tangent_policy = GeometryAttributePolicy::kNone;
    o.normals_smoothing_angle_deg = 35.5F;
    o.lod_generation
      = { .enabled = true, .levels = 5U, .reduction_percent = 40.0F };
    o.import_lights = false;
    o.import_cameras = false;
    o.light_intensity_scale = 2.5F;
    auto& t = profile.tuning;
    t.enabled = false;
    t.intent = TextureIntent::kNormalTS;
    t.source_color_space = ColorSpace::kLinear;
    t.mip_policy = MipPolicy::kMaxCount;
    t.max_mip_levels = 4U;
    t.max_size = 2048U;
    t.mip_filter = MipFilter::kLanczos;
    t.color_output_format = Format::kBC1UNormSRGB;
    t.data_output_format = Format::kBC5UNorm;
    t.bc7_quality = Bc7Quality::kHigh;
    t.hdr_handling = HdrHandling::kKeepFloat;
    t.bake_hdr_to_ldr = true;
    t.exposure_ev = -1.5F;
    t.flip_normal_green = true;
    t.renormalize_normals_in_mips = false;
    t.import_cubemap = true;
    t.equirect_to_cubemap = true;
    t.cubemap_face_size = 512U;
    t.cubemap_layout = CubeMapImageLayout::kVerticalCross;
    profile.importer.thread_pool_size = 7U;
    profile.importer.concurrency.texture
      = { .workers = 3U, .queue_capacity = 9U };
    profile.importer.concurrency.scene
      = { .workers = 2U, .queue_capacity = 4U };

    service.SetImportProfile(profile);
    const auto restored = service.GetImportProfile();

    const auto& ro = restored.options;
    EXPECT_EQ(ro.import_content, o.import_content);
    EXPECT_EQ(ro.node_pruning, o.node_pruning);
    EXPECT_EQ(
      ro.coordinate.unit_normalization, o.coordinate.unit_normalization);
    EXPECT_FLOAT_EQ(ro.coordinate.unit_scale, 0.25F);
    EXPECT_EQ(ro.coordinate.up_axis, SourceUpAxis::kZ);
    EXPECT_EQ(ro.normal_policy, o.normal_policy);
    EXPECT_EQ(ro.tangent_policy, o.tangent_policy);
    EXPECT_FLOAT_EQ(ro.normals_smoothing_angle_deg, 35.5F);
    EXPECT_TRUE(ro.lod_generation.enabled);
    EXPECT_EQ(ro.lod_generation.levels, 5U);
    EXPECT_FLOAT_EQ(ro.lod_generation.reduction_percent, 40.0F);
    EXPECT_FALSE(ro.import_lights);
    EXPECT_FALSE(ro.import_cameras);
    EXPECT_FLOAT_EQ(ro.light_intensity_scale, 2.5F);
    const auto& rt = restored.tuning;
    EXPECT_FALSE(rt.enabled);
    EXPECT_EQ(rt.intent, t.intent);
    EXPECT_EQ(rt.source_color_space, t.source_color_space);
    EXPECT_EQ(rt.mip_policy, t.mip_policy);
    EXPECT_EQ(rt.max_mip_levels, 4U);
    EXPECT_EQ(rt.max_size, 2048U);
    EXPECT_EQ(rt.mip_filter, t.mip_filter);
    EXPECT_EQ(rt.color_output_format, t.color_output_format);
    EXPECT_EQ(rt.data_output_format, t.data_output_format);
    EXPECT_EQ(rt.bc7_quality, t.bc7_quality);
    EXPECT_EQ(rt.hdr_handling, t.hdr_handling);
    EXPECT_TRUE(rt.bake_hdr_to_ldr);
    EXPECT_FLOAT_EQ(rt.exposure_ev, -1.5F);
    EXPECT_TRUE(rt.flip_normal_green);
    EXPECT_FALSE(rt.renormalize_normals_in_mips);
    EXPECT_TRUE(rt.import_cubemap);
    EXPECT_TRUE(rt.equirect_to_cubemap);
    EXPECT_EQ(rt.cubemap_face_size, 512U);
    EXPECT_EQ(rt.cubemap_layout, t.cubemap_layout);
    EXPECT_EQ(restored.importer.thread_pool_size, 7U);
    EXPECT_EQ(restored.importer.concurrency.texture.workers, 3U);
    EXPECT_EQ(restored.importer.concurrency.texture.queue_capacity, 9U);
    EXPECT_EQ(restored.importer.concurrency.scene.workers, 2U);
    EXPECT_EQ(restored.importer.concurrency.scene.queue_capacity, 4U);
  }

  //! An enumerator out of range reads as its default, a number out of range
  //! as its nearest limit; unknown mount lines are skipped.
  NOLINT_TEST(ContentSettingsTest, InvalidValuesReadAsDefaults)
  {
    const auto settings = ContentSettings();
    settings->SetInt("content.import.up_axis", 9);
    settings->SetInt("content.import.texture.max_mips", 99);
    settings->SetString(
      "content.mounts.sources", "zip /a.zip\npak /game.pak\nbroken");
    const ContentSettingsService service;

    const auto profile = service.GetImportProfile();
    EXPECT_EQ(profile.options.coordinate.up_axis,
      content::import::SourceUpAxis::kFromFile);
    EXPECT_EQ(profile.tuning.max_mip_levels, 16U);
    const auto sources = service.GetMountSources();
    ASSERT_EQ(sources.size(), 1U);
    EXPECT_EQ(sources.front().kind, data::CookedSourceKind::kPak);
  }

  //! The mount list keeps its order and kinds; records round trip.
  NOLINT_TEST(ContentSettingsTest, MountListRoundTripsInOrder)
  {
    ContentSettings();
    ContentSettingsService service;
    const auto root = std::filesystem::temp_directory_path() / "content";
    const auto sources = std::vector<ContentSourceSetting> {
      { .kind = data::CookedSourceKind::kPak, .path = root / "showcase.pak" },
      {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = root / "Scratch",
      },
      {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = root / "Showcase",
      },
    };
    const auto records = std::vector<std::filesystem::path> {
      root / "Scratch" / "dino.fbx.import.json",
    };

    service.SetMountSources(sources);
    service.SetMountedImportRecords(records);

    EXPECT_EQ(service.GetMountSources(), sources);
    EXPECT_EQ(service.GetMountedImportRecords(), records);
  }

  //! The loaded scene round trips and clears; recent scenes keep three.
  NOLINT_TEST(ContentSettingsTest, LoadedAndRecentScenesRoundTrip)
  {
    ContentSettings();
    ContentSettingsService service;
    const auto scene = ContentSceneSetting {
      .scene_key = "0123456789abcdef0123456789abcdef",
      .virtual_path = "/Showcase/Scenes/BottleOnBox/BottleOnBox.oscene",
      .source = {
        .kind = data::CookedSourceKind::kLooseCooked,
        .path = std::filesystem::temp_directory_path() / "Showcase",
      },
    };
    auto recent = std::vector<RecentSceneSetting> {};
    for (const std::string name : { "a", "b", "c", "d" }) {
      recent.push_back({
        .scene_key = "key-" + name,
        .virtual_path = "/Showcase/" + name + ".oscene",
      });
    }

    service.SetLoadedScene(scene);
    service.SetRecentScenes(recent);

    EXPECT_EQ(service.GetLoadedScene(), scene);
    recent.resize(ContentSettingsService::kRecentSceneCount);
    EXPECT_EQ(service.GetRecentScenes(), recent);
    service.SetLoadedScene(std::nullopt);
    EXPECT_FALSE(service.GetLoadedScene().has_value());
  }

  //! The Import tab's mount, filter, formats and load choice round trip.
  NOLINT_TEST(ContentSettingsTest, ImportTabRoundTrips)
  {
    ContentSettings();
    ContentSettingsService service;
    const auto tab = ContentImportTabSettings {
      .source_mount = "Downloads",
      .filter = "town",
      .fbx = false,
      .glb = true,
      .gltf = false,
      .load_scene_after_import = false,
    };

    service.SetImportTab(tab);

    EXPECT_EQ(service.GetImportTab(), tab);
  }

} // namespace
} // namespace oxygen::examples
