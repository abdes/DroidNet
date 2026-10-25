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
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
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

  //! The content import keys the model import settings use.
  constexpr auto kModelImportKeys = std::array {
    std::string_view { "content.import.up_axis" },
    std::string_view { "content.import.smoothing_angle" },
    std::string_view { "content.import.lights" },
    std::string_view { "content.import.cameras" },
    std::string_view { "content.import.light_intensity_scale" },
    std::string_view { "content.import.tuning.max_size" },
  };

  NOLINT_TEST(ContentImportSettingsTest, FastDefaultsPreserveExplicitUserTuning)
  {
    constexpr auto kTuningKeys = std::array {
      std::string_view { "content.import.tuning.enabled" },
      std::string_view { "content.import.tuning.mip_policy" },
      std::string_view { "content.import.tuning.mip_filter" },
      std::string_view { "content.import.tuning.bc7_quality" },
    };
    const auto settings = DemoSettings(kTuningKeys);
    const ContentSettingsService service;
    const auto defaults = service.GetTextureTuning();
    EXPECT_TRUE(defaults.enabled);
    EXPECT_EQ(defaults.mip_policy, content::import::MipPolicy::kFullChain);
    EXPECT_EQ(defaults.mip_filter, content::import::MipFilter::kBox);
    EXPECT_EQ(defaults.bc7_quality, content::import::Bc7Quality::kFast);

    settings->SetBool("content.import.tuning.enabled", false);
    settings->SetFloat("content.import.tuning.mip_policy",
      static_cast<float>(content::import::MipPolicy::kNone));
    settings->SetFloat("content.import.tuning.mip_filter",
      static_cast<float>(content::import::MipFilter::kKaiser));
    settings->SetFloat("content.import.tuning.bc7_quality",
      static_cast<float>(content::import::Bc7Quality::kHigh));
    const auto authored = service.GetTextureTuning();
    EXPECT_FALSE(authored.enabled);
    EXPECT_EQ(authored.mip_policy, content::import::MipPolicy::kNone);
    EXPECT_EQ(authored.mip_filter, content::import::MipFilter::kKaiser);
    EXPECT_EQ(authored.bc7_quality, content::import::Bc7Quality::kHigh);
    EXPECT_FALSE(std::filesystem::exists(DemoSettingsPath()));
  }

  NOLINT_TEST(ContentImportSettingsTest, ModelImportOptionsDefaultWhenAbsent)
  {
    DemoSettings(kModelImportKeys);
    const ContentSettingsService service;

    const auto options = service.GetImportOptions();
    EXPECT_EQ(
      options.coordinate.up_axis, content::import::SourceUpAxis::kFromFile);
    EXPECT_FLOAT_EQ(options.normals_smoothing_angle_deg, 60.0F);
    EXPECT_TRUE(options.import_lights);
    EXPECT_TRUE(options.import_cameras);
    EXPECT_FLOAT_EQ(options.light_intensity_scale, 1.0F);
    EXPECT_EQ(service.GetTextureTuning().max_size, 0U);
  }

  NOLINT_TEST(ContentImportSettingsTest, ModelImportOptionsRoundTrip)
  {
    DemoSettings(kModelImportKeys);
    ContentSettingsService service;

    auto options = service.GetImportOptions();
    options.coordinate.up_axis = content::import::SourceUpAxis::kZ;
    options.normals_smoothing_angle_deg = 35.5F;
    options.import_lights = false;
    options.import_cameras = false;
    options.light_intensity_scale = 2.5F;
    service.SetImportOptions(options);
    auto tuning = service.GetTextureTuning();
    tuning.max_size = 2048U;
    service.SetTextureTuning(tuning);

    const auto restored = service.GetImportOptions();
    EXPECT_EQ(restored.coordinate.up_axis, content::import::SourceUpAxis::kZ);
    EXPECT_FLOAT_EQ(restored.normals_smoothing_angle_deg, 35.5F);
    EXPECT_FALSE(restored.import_lights);
    EXPECT_FALSE(restored.import_cameras);
    EXPECT_FLOAT_EQ(restored.light_intensity_scale, 2.5F);
    EXPECT_EQ(service.GetTextureTuning().max_size, 2048U);
  }

} // namespace
} // namespace oxygen::examples
