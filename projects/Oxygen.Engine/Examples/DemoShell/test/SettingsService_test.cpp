//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

#include "DemoShell/Services/ContentSettingsService.h"
#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Uuid.h>
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

  NOLINT_TEST(ContentImportSettingsTest, FastDefaultsPreserveExplicitUserTuning)
  {
    const auto path = std::filesystem::temp_directory_path()
      / ("oxygen-import-settings-" + Uuid::Generate().ToString() + ".json");
    SettingsService::InitializeForDemoApp(path);
    const auto settings = SettingsService::ForDemoApp();
    settings->SetPersistenceEnabled(false);
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
    EXPECT_FALSE(std::filesystem::exists(path));
  }

} // namespace
} // namespace oxygen::examples
