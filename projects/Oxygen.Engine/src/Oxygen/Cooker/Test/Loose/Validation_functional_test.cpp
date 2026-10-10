//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Loose/Validation.cpp

#include <cstdint>
#include <exception>
#include <span>

#include <Oxygen/Content/Test/Fixtures/LooseCookedTestWriter.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Cooker/Loose/Validation.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

using oxygen::content::lc::ValidateRoot;

namespace {

class ValidationTest : public oxygen::cooker::test::TempDirTest { };

NOLINT_TEST_F(ValidationTest, ValidateRootValidSucceeds)
{
  oxygen::data::pak::render::MaterialAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
  descriptor.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  oxygen::content::import::LooseCookedWriter writer(TempDir());
  writer.WriteAssetDescriptor(
    oxygen::data::AssetKey::FromVirtualPath("/Content/test.omat"),
    oxygen::data::AssetType::kMaterial, "/Content/test.omat", "test.omat",
    std::as_bytes(std::span { &descriptor, 1 }), {});
  static_cast<void>(writer.Finish());
  EXPECT_NO_THROW({ ValidateRoot(TempDir()); });
}

NOLINT_TEST_F(ValidationTest, ValidateRootInvalidThrowsException)
{
  constexpr uint16_t kInvalidVersion = 999;
  oxygen::content::testing::LooseCookedTestWriter writer(TempDir());
  writer.SetIndexVersion(kInvalidVersion);
  static_cast<void>(writer.Finish());
  EXPECT_THROW({ ValidateRoot(TempDir()); }, std::exception);
}

} // namespace
