//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>

#include <Oxygen/Testing/GTest.h>

#include "./AssetLoader_test.h"

using oxygen::content::testing::AssetLoaderBasicTest;

auto AssetLoaderBasicTest::SetUp() -> void
{
  // Create a per-test temporary directory to avoid cross-process fixture races
  // when tests run concurrently in separate executables.
  const auto* test_info
    = ::testing::UnitTest::GetInstance()->current_test_info();
  const auto sanitize = [](std::string_view name) {
    std::string sanitized;
    sanitized.reserve(name.size());
    for (const char c : name) {
      const bool alpha_num = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
        || (c >= '0' && c <= '9');
      sanitized.push_back(alpha_num ? c : '_');
    }
    return sanitized;
  };
  const auto unique_suffix
    = std::chrono::steady_clock::now().time_since_epoch().count();

  temp_dir_ = std::filesystem::temp_directory_path()
    / "oxygen_asset_loader_tests"
    / (sanitize(test_info->test_suite_name()) + "_"
      + sanitize(test_info->name()) + "_" + std::to_string(unique_suffix));
  std::filesystem::create_directories(temp_dir_);

  // Create AssetLoader instance
  asset_loader_ = std::make_unique<AssetLoader>(Tag::Get());
}

auto AssetLoaderBasicTest::TearDown() -> void
{
  // Reset AssetLoader first to close PAK files
  asset_loader_.reset();

  // Clean up temporary directory
  if (std::filesystem::exists(temp_dir_)) {
    std::filesystem::remove_all(temp_dir_);
  }
}
