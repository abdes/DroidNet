//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

#include <d3dcommon.h>

#include <Oxygen/Graphics/Direct3D12/Devices/AdapterSelection.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::d3d12 {
namespace {

  constexpr std::uint64_t kGiB = 1ULL << 30U;

  // A discrete GPU without outputs, as on hybrid laptops or when the desktop
  // runs on another adapter.
  const auto kDiscrete = AdapterTraits {
    .name = "AMD Radeon RX 9070 XT",
    .device_id = 0x7550U,
    .dedicated_memory = 16U * kGiB,
    .has_display = false,
    .max_feature_level = D3D_FEATURE_LEVEL_12_2,
  };
  const auto kIntegrated = AdapterTraits {
    .name = "AMD Radeon(TM) Graphics",
    .device_id = 0x13C0U,
    .dedicated_memory = 2U * kGiB,
    .has_display = true,
    .max_feature_level = D3D_FEATURE_LEVEL_12_2,
  };
  const auto kBelowMinimum = AdapterTraits {
    .name = "Legacy GPU",
    .device_id = 0x1111U,
    .dedicated_memory = 32U * kGiB,
    .has_display = true,
    .max_feature_level = std::nullopt,
  };

  //! Without hints, the highest feature level wins, then dedicated memory;
  //! a display connection does not count unless required.
  NOLINT_TEST(AdapterSelectionTest, ScoreOrdersByFeatureLevelThenMemory)
  {
    auto older = kDiscrete;
    older.name = "Older GPU";
    older.dedicated_memory = 24U * kGiB;
    older.max_feature_level = D3D_FEATURE_LEVEL_12_1;
    const auto adapters = std::vector { kIntegrated, older, kDiscrete };

    const auto ranking = RankAdapters(adapters, {});

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kScore);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 2U, 0U, 1U }));
  }

  //! Equal scores keep the enumeration (high-performance) order.
  NOLINT_TEST(AdapterSelectionTest, EqualScoresKeepEnumerationOrder)
  {
    auto twin = kDiscrete;
    twin.name = "Second RX 9070 XT";
    const auto adapters = std::vector { kDiscrete, twin };

    const auto ranking = RankAdapters(adapters, {});

    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 0U, 1U }));
  }

  //! An adapter below the minimum feature level is never selectable.
  NOLINT_TEST(AdapterSelectionTest, BelowMinimumIsNeverRanked)
  {
    const auto adapters = std::vector { kBelowMinimum, kIntegrated };

    const auto ranking
      = RankAdapters(adapters, { .preferred_card_name = "Legacy" });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kNoPreferredMatch);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U }));
  }

  //! A name hint matches a part of the adapter name and comes first.
  NOLINT_TEST(AdapterSelectionTest, NameHintComesFirst)
  {
    const auto adapters = std::vector { kDiscrete, kIntegrated };

    const auto ranking = RankAdapters(
      adapters, { .preferred_card_name = "Radeon(TM) Graphics" });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kPreferredCard);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U, 0U }));
  }

  //! A device id hint matches the PCI device id and comes first.
  NOLINT_TEST(AdapterSelectionTest, DeviceIdHintComesFirst)
  {
    const auto adapters = std::vector { kDiscrete, kIntegrated };

    const auto ranking
      = RankAdapters(adapters, { .preferred_card_device_id = 0x13C0 });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kPreferredCard);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U, 0U }));
  }

  //! Without a usable match, the score decides and the reason says so.
  NOLINT_TEST(AdapterSelectionTest, UnmatchedHintFallsBackToScore)
  {
    const auto adapters = std::vector { kIntegrated, kDiscrete };

    const auto ranking
      = RankAdapters(adapters, { .preferred_card_name = "GeForce" });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kNoPreferredMatch);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U, 0U }));
  }

  //! The hints are mutually exclusive.
  NOLINT_TEST(AdapterSelectionTest, BothHintsAreRejected)
  {
    const auto adapters = std::vector { kDiscrete };

    EXPECT_THROW(std::ignore = RankAdapters(adapters,
                   {
                     .preferred_card_name = "Radeon",
                     .preferred_card_device_id = 0x7550,
                   }),
      std::invalid_argument);
  }

  //! Requiring a display filters the adapters that drive none.
  NOLINT_TEST(AdapterSelectionTest, RequiredDisplayFiltersScoredAdapters)
  {
    const auto adapters = std::vector { kDiscrete, kIntegrated };

    const auto ranking = RankAdapters(adapters, { .require_display = true });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kScore);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U }));
  }

  //! A hinted adapter is the user's choice; the display filter skips it.
  NOLINT_TEST(AdapterSelectionTest, RequiredDisplayDoesNotFilterHint)
  {
    const auto adapters = std::vector { kIntegrated, kDiscrete };

    const auto ranking = RankAdapters(adapters,
      {
        .require_display = true,
        .preferred_card_name = "RX 9070",
      });

    EXPECT_EQ(ranking.reason, AdapterChoiceReason::kPreferredCard);
    EXPECT_EQ(ranking.order, (std::vector<std::size_t> { 1U, 0U }));
  }

  //! Nothing is selectable when no adapter passes the filters.
  NOLINT_TEST(AdapterSelectionTest, NoSelectableAdapterYieldsEmptyRanking)
  {
    const auto adapters = std::vector { kBelowMinimum, kDiscrete };

    const auto ranking = RankAdapters(adapters, { .require_display = true });

    EXPECT_TRUE(ranking.order.empty());
  }

} // namespace
} // namespace oxygen::graphics::d3d12
