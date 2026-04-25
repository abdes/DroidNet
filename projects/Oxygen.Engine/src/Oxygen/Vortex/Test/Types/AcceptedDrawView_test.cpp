//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/Types/AcceptedDrawView.h>

namespace {

using oxygen::vortex::AcceptedDrawView;
using oxygen::vortex::DrawMetadata;
using oxygen::vortex::PassMask;
using oxygen::vortex::PassMaskBit;
using oxygen::vortex::PreparedSceneFrame;

auto MakeDrawMetadata(const std::uint32_t first_index, const PassMask flags)
  -> DrawMetadata
{
  auto metadata = DrawMetadata {};
  metadata.first_index = first_index;
  metadata.flags = flags;
  metadata.instance_count = 1U;
  return metadata;
}

auto MakeFrameWithMetadata(std::span<const DrawMetadata> metadata)
  -> PreparedSceneFrame
{
  auto frame = PreparedSceneFrame {};
  frame.draw_metadata_bytes = std::as_bytes(metadata);
  return frame;
}

NOLINT_TEST(AcceptedDrawViewTest, PreparedSceneFrameExposesTypedDrawMetadata)
{
  auto metadata = std::array {
    MakeDrawMetadata(11U, PassMask { PassMaskBit::kOpaque }),
    MakeDrawMetadata(22U, PassMask { PassMaskBit::kMasked }),
  };
  const auto frame = MakeFrameWithMetadata(metadata);

  const auto typed_metadata = frame.GetDrawMetadata();

  ASSERT_EQ(typed_metadata.size(), metadata.size());
  EXPECT_EQ(typed_metadata[0].first_index, 11U);
  EXPECT_EQ(typed_metadata[1].first_index, 22U);
  EXPECT_EQ(typed_metadata[0].flags, PassMask(PassMaskBit::kOpaque));
  EXPECT_EQ(typed_metadata[1].flags, PassMask(PassMaskBit::kMasked));
}

NOLINT_TEST(AcceptedDrawViewTest, PartitionedIterationSkipsRejectedPartitions)
{
  auto metadata = std::array {
    MakeDrawMetadata(10U, PassMask { PassMaskBit::kTransparent }),
    MakeDrawMetadata(20U, PassMask { PassMaskBit::kOpaque }),
    MakeDrawMetadata(30U, PassMask { PassMaskBit::kOpaque }),
    MakeDrawMetadata(40U, PassMask { PassMaskBit::kUi }),
    MakeDrawMetadata(50U, PassMask { PassMaskBit::kMasked }),
  };
  auto frame = MakeFrameWithMetadata(metadata);

  const auto partitions = std::array {
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kTransparent },
      .begin = 0U,
      .end = 1U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kOpaque },
      .begin = 1U,
      .end = 3U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kUi },
      .begin = 3U,
      .end = 4U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kMasked },
      .begin = 4U,
      .end = 10U,
    },
  };
  frame.partitions = partitions;

  auto accepted_indices = std::vector<std::uint32_t> {};
  auto accepted_first_indices = std::vector<std::uint32_t> {};
  const auto accept_mask = PassMask {
    PassMaskBit::kOpaque,
    PassMaskBit::kMasked,
  };
  for (const auto accepted_draw : AcceptedDrawView(frame, accept_mask)) {
    accepted_indices.push_back(accepted_draw.draw_index);
    accepted_first_indices.push_back(accepted_draw.metadata->first_index);
  }

  EXPECT_THAT(accepted_indices, ::testing::ElementsAre(1U, 2U, 4U));
  EXPECT_THAT(accepted_first_indices, ::testing::ElementsAre(20U, 30U, 50U));
}

NOLINT_TEST(AcceptedDrawViewTest, FlatIterationFiltersByDrawFlags)
{
  auto metadata = std::array {
    MakeDrawMetadata(10U, PassMask { PassMaskBit::kTransparent }),
    MakeDrawMetadata(20U, PassMask { PassMaskBit::kMasked }),
    MakeDrawMetadata(
      30U, PassMask { PassMaskBit::kOpaque, PassMaskBit::kMainViewVisible }),
    MakeDrawMetadata(40U, PassMask { PassMaskBit::kUi }),
  };
  const auto frame = MakeFrameWithMetadata(metadata);

  auto accepted_indices = std::vector<std::uint32_t> {};
  for (const auto accepted_draw : AcceptedDrawView(
         frame, PassMask { PassMaskBit::kOpaque, PassMaskBit::kMasked })) {
    accepted_indices.push_back(accepted_draw.draw_index);
  }

  EXPECT_THAT(accepted_indices, ::testing::ElementsAre(1U, 2U));
}

NOLINT_TEST(AcceptedDrawViewTest, EmptyMetadataYieldsNoAcceptedDraws)
{
  const auto frame = PreparedSceneFrame {};

  const auto accepted_draws
    = AcceptedDrawView(frame, PassMask { PassMaskBit::kOpaque });

  EXPECT_TRUE(accepted_draws.empty());
  EXPECT_EQ(accepted_draws.begin(), accepted_draws.end());
}

} // namespace
