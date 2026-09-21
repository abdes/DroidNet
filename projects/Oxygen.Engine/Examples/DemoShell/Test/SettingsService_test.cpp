//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "DemoShell/Services/SettingsService.h"

#include <Oxygen/Base/Finally.h>
#include <Oxygen/Base/Uuid.h>
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

} // namespace
} // namespace oxygen::examples
