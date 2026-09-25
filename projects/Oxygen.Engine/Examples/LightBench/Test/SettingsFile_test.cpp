//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>

#ifdef _WIN32
#  include <Windows.h> // IWYU pragma: keep
#  include <fileapi.h>
#  include <handleapi.h>
#  include <minwindef.h>
#  include <winnt.h>
#endif

#include "LightBench/LightBenchSettings.h"

#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::examples::light_bench::testing {
namespace {
  class LightBenchSettingsFile : public ::testing::Test {
  private:
    std::filesystem::path directory_;
    std::filesystem::path path_;

  protected:
    [[nodiscard]] auto Path() const -> const std::filesystem::path&
    {
      return path_;
    }
    [[nodiscard]] auto EntryCount() const -> std::ptrdiff_t
    {
      return std::distance(std::filesystem::directory_iterator(directory_),
        std::filesystem::directory_iterator());
    }

    auto SetUp() -> void override
    {
      directory_ = std::filesystem::temp_directory_path()
        / ("oxygen-lightbench-" + Uuid::Generate().ToString());
      ASSERT_TRUE(std::filesystem::create_directory(directory_));
      path_ = directory_ / "settings.json";
    }
    auto TearDown() -> void override
    {
      std::error_code error;
      std::filesystem::remove_all(directory_, error);
      EXPECT_FALSE(error) << error.message();
    }
    [[nodiscard]] auto ReadBytes() const -> std::string
    {
      std::ifstream input(path_, std::ios::binary);
      return { std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>() };
    }
  };
} // namespace

NOLINT_TEST_F(LightBenchSettingsFile, ReplacesCompleteFileAndRejectsInvalidSave)
{
  ASSERT_TRUE(SaveSettingsFile(Path(), ReferenceSettings()));
  const auto indoor = PresetSettings(LightBenchPreset::kIndoor);
  ASSERT_TRUE(SaveSettingsFile(Path(), indoor));
  const auto loaded = LoadSettingsFile(Path());
  ASSERT_TRUE(loaded) << loaded.error().message;
  EXPECT_EQ(loaded->preset, LightBenchPreset::kIndoor);
  EXPECT_TRUE(IsPresetSettings(*loaded));
  const auto previous = ReadBytes();
  auto invalid = indoor;
  invalid.spot.outer_angle_deg = -1;
  EXPECT_FALSE(SaveSettingsFile(Path(), invalid));
  EXPECT_EQ(ReadBytes(), previous);
  EXPECT_EQ(EntryCount(), 1);
}

NOLINT_TEST_F(LightBenchSettingsFile, RejectsMissingMalformedAndOversizedFiles)
{
  EXPECT_FALSE(LoadSettingsFile(Path()));
  {
    std::ofstream output(Path(), std::ios::binary);
    output << "{";
  }
  EXPECT_FALSE(LoadSettingsFile(Path()));
  {
    std::ofstream output(Path(), std::ios::binary);
    constexpr std::size_t kOversizedBytes = 1'048'577U;
    output << std::string(kOversizedBytes, ' ');
  }
  const auto oversized = LoadSettingsFile(Path());
  ASSERT_FALSE(oversized);
  EXPECT_NE(oversized.error().message.find("1 MiB"), std::string::npos);
  EXPECT_FALSE(SaveSettingsFile(
    Path().parent_path() / "missing" / "settings.json", ReferenceSettings()));
}

#ifdef _WIN32
NOLINT_TEST_F(
  LightBenchSettingsFile, FailedReplacementPreservesPreviousFileAndCanRetry)
{
  ASSERT_TRUE(SaveSettingsFile(Path(), ReferenceSettings()));
  const auto previous = ReadBytes();
  auto* blocker = CreateFileW(Path().c_str(), GENERIC_READ,
    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL, nullptr);
  ASSERT_NE(blocker, INVALID_HANDLE_VALUE);
  const auto cleanup = ScopeGuard([&blocker] noexcept -> void {
    if (blocker != INVALID_HANDLE_VALUE) {
      CloseHandle(blocker);
    }
  });
  const auto indoor = PresetSettings(LightBenchPreset::kIndoor);
  EXPECT_FALSE(SaveSettingsFile(Path(), indoor));
  EXPECT_EQ(ReadBytes(), previous);
  EXPECT_EQ(EntryCount(), 1);
  ASSERT_NE(CloseHandle(blocker), FALSE);
  blocker = INVALID_HANDLE_VALUE;
  ASSERT_TRUE(SaveSettingsFile(Path(), indoor));
  const auto loaded = LoadSettingsFile(Path());
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded->preset, LightBenchPreset::kIndoor);
}
#endif

} // namespace oxygen::examples::light_bench::testing
