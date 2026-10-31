//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>

#include "./AssetLoader_test.h"

#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::testing::AssetLoaderBasicTest;

auto AssetLoaderBasicTest::SetUp() -> void
{
  asset_loader_ = std::make_unique<AssetLoader>(Tag::Get());
}

auto AssetLoaderBasicTest::TearDown() -> void
{
  // Reset AssetLoader first to close PAK files
  asset_loader_.reset();
}
