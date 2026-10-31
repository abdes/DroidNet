//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace {

auto MakeAssetKey(const std::uint8_t seed) -> oxygen::data::AssetKey
{
  auto bytes = std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {};
  bytes.at(0) = seed;
  return oxygen::data::AssetKey::FromBytes(bytes);
}

//! Test helper: write a minimal loose cooked index with one asset entry.
/*!
 Scenario: Creates a `container.index.bin` mapping the given virtual path to
 the provided `AssetKey`.
*/
auto WriteSingleAssetIndex(const std::filesystem::path& cooked_root,
  const oxygen::data::AssetKey& key, const std::string_view leaf) -> void
{
  auto loose = oxygen::cooker::testing::LooseCookedRoot { ".cooked" };
  static_cast<void>(loose.Add({ .leaf = std::string(leaf), .key = key }));
  oxygen::cooker::testing::Materialize(loose.Files(), cooked_root);
}

//! Test: Resolver returns the AssetKey for a matching virtual path
/*!
 Scenario: Mounts a single cooked root and resolves a known virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyFoundReturnsKey)
{
  // Arrange
  const oxygen::testing::ScopedTempDir temp_dir;
  const auto& root = temp_dir.Path();
  const auto cooked_root = root / "root0";

  const auto key = MakeAssetKey(0x11U);

  WriteSingleAssetIndex(cooked_root, key, "A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/A.bin");

  // Assert
  if (!resolved.has_value()) {
    FAIL() << "Expected a resolved asset key";
  }
  EXPECT_EQ(*resolved, key);
}

//! Test: Resolver prefers the last mounted root
/*!
 Scenario: Two roots contain the same virtual path, mapping to different
 * keys.
 Verifies that the last added root wins.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyDuplicatePathLastWins)
{
  // Arrange
  const oxygen::testing::ScopedTempDir temp_dir;
  const auto& root = temp_dir.Path();
  const auto cooked_root0 = root / "root0";
  const auto cooked_root1 = root / "root1";

  const auto key0 = MakeAssetKey(0x11U);
  const auto key1 = MakeAssetKey(0x22U);

  WriteSingleAssetIndex(cooked_root0, key0, "A.bin");
  WriteSingleAssetIndex(cooked_root1, key1, "A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root0);
  resolver.AddLooseCookedRoot(cooked_root1);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/A.bin");

  // Assert
  if (!resolved.has_value()) {
    FAIL() << "Expected a resolved asset key";
  }
  EXPECT_EQ(*resolved, key1);
  resolver.AddLooseCookedRoot(cooked_root0);
  EXPECT_EQ(resolver.ResolveAssetKey("/.cooked/A.bin"), key0);
}

//! Test: Resolver returns nullopt when the virtual path is not found
/*!
 Scenario: Mounts a cooked root and queries an unknown virtual path.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyNotFoundReturnsNullopt)
{
  // Arrange
  const oxygen::testing::ScopedTempDir temp_dir;
  const auto& root = temp_dir.Path();
  const auto cooked_root = root / "root0";

  const auto key = MakeAssetKey(0x11U);

  WriteSingleAssetIndex(cooked_root, key, "A.bin");

  oxygen::content::VirtualPathResolver resolver;
  resolver.AddLooseCookedRoot(cooked_root);

  // Act
  const auto resolved = resolver.ResolveAssetKey("/.cooked/DoesNotExist.bin");

  // Assert
  EXPECT_FALSE(resolved.has_value());
}

//! Test: Resolver rejects non-canonical virtual paths
/*!
 Scenario: Attempts to resolve a virtual path missing the leading '/'.
 Verifies the resolver throws.
*/
NOLINT_TEST(VirtualPathResolverTest, ResolveAssetKeyInvalidVirtualPathThrows)
{
  // Arrange
  oxygen::content::VirtualPathResolver resolver;

  // Act & Assert
  EXPECT_THROW(
    { (void)resolver.ResolveAssetKey(".cooked/A.bin"); },
    std::invalid_argument);
}

} // namespace
