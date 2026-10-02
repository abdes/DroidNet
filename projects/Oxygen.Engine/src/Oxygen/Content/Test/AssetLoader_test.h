//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {

//! AssetLoader fixture with isolated temporary storage.
class AssetLoaderBasicTest : public ::testing::Test {
protected:
  using Tag = oxygen::engine::internal::EngineTagFactory;

  auto SetUp() -> void override;
  auto TearDown() -> void override;

  std::filesystem::path temp_dir_;
  std::unique_ptr<AssetLoader> asset_loader_;
};

} // namespace oxygen::content::testing

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal
