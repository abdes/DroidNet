//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: the content hash an asset descriptor records in its header, which a
// cook stamps and every reader of cooked content checks.

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

#include <Oxygen/Cooker/testing/DescriptorDefects.h>
#include <Oxygen/Cooker/testing/SceneBuilder.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/DescriptorHash.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace {
namespace data = oxygen::data;
using data::CookedDefect;
using oxygen::cooker::testing::DescriptorDefects;
using oxygen::cooker::testing::SceneBuilder;

auto SceneBytes() -> std::vector<std::byte>
{
  auto scene = SceneBuilder { "DescriptorHashTest" };
  scene.Node("Root");
  return scene.Bytes();
}

//! The rule `CheckDescriptorHash` refuses `bytes` for; empty when it passes.
auto HashDefect(const std::span<const std::byte> bytes)
  -> std::optional<CookedDefect>
{
  try {
    data::CheckDescriptorHash(bytes);
  } catch (const data::CookedFormatError& error) {
    return error.Defect();
  }
  return std::nullopt;
}

//! A stamped descriptor records its hash, which its asset reports.
NOLINT_TEST(DescriptorHashTest, StampedDescriptorRecordsItsHash)
{
  const auto bytes = SceneBytes();
  const auto scene = data::SceneAsset(
    data::AssetKey::FromVirtualPath("/test/Hash.oscene"), bytes);

  EXPECT_EQ(HashDefect(bytes), std::nullopt);
  EXPECT_EQ(scene.GetContentHash(), data::ComputeDescriptorHash(bytes));
}

//! A descriptor without a hash, with another one, or too small to hold one
//! is refused; stamping it again makes it pass.
NOLINT_TEST(DescriptorHashTest, RefusesADescriptorThatDoesNotRecordItsHash)
{
  const auto valid = SceneBytes();
  auto cleared = valid;
  DescriptorDefects::ClearContentHash(cleared);
  auto corrupted = valid;
  DescriptorDefects::CorruptContentHash(corrupted);
  const auto truncated
    = std::span(valid).first(sizeof(data::pak::core::AssetHeader) - 1U);
  auto restamped = corrupted;
  data::StampDescriptorHash(restamped);

  EXPECT_EQ(HashDefect(cleared), CookedDefect::kMissingDigest);
  EXPECT_EQ(HashDefect(corrupted), CookedDefect::kDigestMismatch);
  EXPECT_EQ(HashDefect(truncated), CookedDefect::kTooSmall);
  EXPECT_EQ(HashDefect(restamped), std::nullopt);
}

} // namespace
