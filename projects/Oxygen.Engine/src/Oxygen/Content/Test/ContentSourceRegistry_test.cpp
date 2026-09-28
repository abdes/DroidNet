//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <memory>
#include <stdexcept>

#include "AssetLoader_test.h"
#include "Fixtures/LooseCookedTestWriter.h"

#include <Oxygen/Content/Constants.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Content/Internal/LooseCookedSource.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SourceRegistryDoesNotRecycleIdsAcrossClear)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    auto source = std::make_shared<internal::LooseCookedSource>(root, true);
    internal::ContentSourceRegistry registry;
    const auto first = registry.MountLoose(source->DebugName(), source);
    const auto token = registry.GetSourceToken(first.source_id);
    if (!token) {
      FAIL() << "Expected token";
    }
    registry.Clear();
    EXPECT_TRUE(registry.Sources().empty());
    EXPECT_EQ(registry.AcquireSource(first.source_id), source);
    EXPECT_EQ(registry.FindSourceIdByToken(*token), first.source_id);
    const auto second = registry.MountLoose(source->DebugName(), source);
    EXPECT_NE(first.source_id, second.source_id);
    EXPECT_NE(registry.GetSourceToken(second.source_id), token);
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SourceRegistryRetainsNoUnownedRetiredBytes)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    auto source = std::make_shared<internal::LooseCookedSource>(root, true);
    internal::ContentSourceRegistry registry;
    const auto mounted = registry.MountLoose(source->DebugName(), source);
    const auto key = source->GetSourceKey();
    registry.Clear();
    EXPECT_TRUE(registry.AcquireSource(mounted.source_id));
    source.reset();
    EXPECT_FALSE(registry.AcquireSource(mounted.source_id));
    EXPECT_EQ(registry.GetSourceKey(mounted.source_id), key);
    EXPECT_FALSE(registry.FindSourceIdByKey(key));
  }

  NOLINT_TEST_F(
    AssetLoaderBasicTest, SourceIdExhaustionPreservesTheCurrentMount)
  {
    const auto root = temp_dir_ / "source";
    static_cast<void>(LooseCookedTestWriter(root).Finish());
    const auto source
      = std::make_shared<internal::LooseCookedSource>(root, true);
    internal::ContentSourceRegistry registry;
    for (uint32_t expected = constants::kLooseCookedSourceIdBase;
      expected < constants::kSyntheticSourceId; ++expected) {
      const auto mounted = registry.MountLoose(source->DebugName(), source);
      ASSERT_EQ(mounted.source_id, expected);
    }
    EXPECT_THROW(
      static_cast<void>(registry.MountLoose(source->DebugName(), source)),
      std::length_error);
    ASSERT_EQ(registry.Sources().size(), 1U);
    EXPECT_EQ(registry.Sources().front(), source);
    EXPECT_EQ(registry.SourceIds().front(), constants::kSyntheticSourceId - 1U);
  }

} // namespace
} // namespace oxygen::content::testing
