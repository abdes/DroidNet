//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <vector>

#include <Oxygen/Data/GeometryIndices.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/HistorySlotAllocator.h>
#include <Oxygen/Vortex/Types/DrawCullRecord.h>

namespace {

using oxygen::vortex::kNoHistorySlot;
using oxygen::vortex::PreparedSceneFrame;
using oxygen::vortex::occlusion::internal::HistorySlotAllocator;
using Assignment = HistorySlotAllocator::Assignment;
using Draws = std::vector<PreparedSceneFrame::DrawSource>;

auto Draw(const std::uint32_t node, const std::uint32_t lod = 0U,
  const std::uint32_t view = 0U) -> PreparedSceneFrame::DrawSource
{
  return PreparedSceneFrame::DrawSource {
    .node = oxygen::scene::NodeHandle { node, 1U },
    .lod_index = oxygen::data::LodIndex { lod },
    .submesh_index = oxygen::data::SubmeshIndex { 0U },
    .mesh_view_index = oxygen::data::MeshViewIndex { view },
  };
}

constexpr auto Kept(const std::uint32_t slot) -> Assignment
{
  return Assignment { .slot = slot, .fresh = false };
}

constexpr auto Fresh(const std::uint32_t slot) -> Assignment
{
  return Assignment { .slot = slot, .fresh = true };
}

constexpr auto kNone = Assignment { .slot = kNoHistorySlot, .fresh = false };

//! Keys seen last frame keep their slot whatever the draw order; new keys
//! get the lowest free slot and are fresh.
NOLINT_TEST(HistorySlotAllocatorTest, PersistentKeysKeepSlotsInAnyOrder)
{
  auto allocator = HistorySlotAllocator {};

  const auto first = allocator.Update(Draws { Draw(1), Draw(2), Draw(3) });
  const auto second = allocator.Update(Draws { Draw(3), Draw(4), Draw(1) });

  EXPECT_THAT(first, ::testing::ElementsAre(Fresh(0), Fresh(1), Fresh(2)));
  EXPECT_THAT(second, ::testing::ElementsAre(Kept(2), Fresh(1), Kept(0)));
  EXPECT_EQ(allocator.Capacity(), 3U);
}

//! Slots of absent keys are freed and reused, so a returning key is fresh.
NOLINT_TEST(HistorySlotAllocatorTest, AbsentKeysFreeTheirSlots)
{
  auto allocator = HistorySlotAllocator {};
  static_cast<void>(allocator.Update(Draws { Draw(1), Draw(2) }));

  const auto without = allocator.Update(Draws { Draw(2) });
  const auto returned = allocator.Update(Draws { Draw(2), Draw(1) });

  EXPECT_THAT(without, ::testing::ElementsAre(Kept(1)));
  EXPECT_THAT(returned, ::testing::ElementsAre(Kept(1), Fresh(0)));
  EXPECT_EQ(allocator.Capacity(), 2U);
}

//! A LOD or mesh-view change is a new key, so it starts without history.
NOLINT_TEST(HistorySlotAllocatorTest, LodAndMeshViewChangesProduceNewKeys)
{
  auto allocator = HistorySlotAllocator {};
  static_cast<void>(allocator.Update(Draws { Draw(1, 0U, 0U) }));

  const auto lod_switch = allocator.Update(Draws { Draw(1, 1U, 0U) });
  const auto view_switch = allocator.Update(Draws { Draw(1, 1U, 1U) });

  EXPECT_THAT(lod_switch, ::testing::ElementsAre(Fresh(0)));
  EXPECT_THAT(view_switch, ::testing::ElementsAre(Fresh(0)));
}

//! A key that produced more than one draw gets no slot and loses its old one.
NOLINT_TEST(HistorySlotAllocatorTest, DuplicateKeysGetNoSlot)
{
  auto allocator = HistorySlotAllocator {};
  static_cast<void>(allocator.Update(Draws { Draw(1), Draw(2) }));

  const auto duplicated = allocator.Update(Draws { Draw(1), Draw(2), Draw(1) });
  const auto unique_again = allocator.Update(Draws { Draw(1), Draw(2) });

  EXPECT_THAT(duplicated, ::testing::ElementsAre(kNone, Kept(1), kNone));
  EXPECT_THAT(unique_again, ::testing::ElementsAre(Fresh(0), Kept(1)));
}

//! Growing the capacity never moves an existing slot.
NOLINT_TEST(HistorySlotAllocatorTest, GrowthPreservesExistingSlots)
{
  constexpr std::uint32_t kGrownDrawCount = 100U;
  auto allocator = HistorySlotAllocator {};
  static_cast<void>(allocator.Update(Draws { Draw(7) }));
  auto grown = Draws {};
  for (std::uint32_t node = 0U; node < kGrownDrawCount; ++node) {
    grown.push_back(Draw(node));
  }

  const auto assignments = allocator.Update(grown);

  EXPECT_EQ(assignments.at(7), Kept(0));
  EXPECT_EQ(allocator.Capacity(), kGrownDrawCount);
  for (std::uint32_t node = 0U; node < kGrownDrawCount; ++node) {
    if (node != 7U) {
      EXPECT_TRUE(assignments.at(node).fresh) << "node " << node;
    }
  }
}

//! After a reset every draw is fresh, and the capacity is reused.
NOLINT_TEST(HistorySlotAllocatorTest, ResetMakesEveryDrawFresh)
{
  auto allocator = HistorySlotAllocator {};
  static_cast<void>(allocator.Update(Draws { Draw(1), Draw(2) }));

  allocator.Reset();
  const auto after = allocator.Update(Draws { Draw(2), Draw(1) });

  EXPECT_THAT(after, ::testing::ElementsAre(Fresh(0), Fresh(1)));
  EXPECT_EQ(allocator.Capacity(), 2U);
}

} // namespace
