//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <glm/vec4.hpp>

#include <Oxygen/Renderer/Internal/ConventionalShadowDrawRecordBuilder.h>
#include <Oxygen/Renderer/PreparedSceneFrame.h>
#include <Oxygen/Renderer/Types/DrawMetadata.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::engine::DrawMetadata;
using oxygen::engine::DrawPrimitiveFlagBits;
using oxygen::engine::PassMask;
using oxygen::engine::PassMaskBit;
using oxygen::engine::PreparedSceneFrame;
using oxygen::engine::internal::BuildConventionalShadowDrawRecords;
using oxygen::renderer::ConventionalShadowDrawRecord;

[[nodiscard]] auto MakeDraw(
  const PassMask pass_mask, const std::uint32_t primitive_flags) -> DrawMetadata
{
  return DrawMetadata {
    .vertex_buffer_index = oxygen::ShaderVisibleIndex { 0U },
    .index_buffer_index = oxygen::ShaderVisibleIndex { 0U },
    .first_index = 0U,
    .base_vertex = 0,
    .is_indexed = 1U,
    .instance_count = 1U,
    .index_count = 3U,
    .vertex_count = 0U,
    .material_handle = 0U,
    .transform_index = 0U,
    .instance_metadata_buffer_index = 0U,
    .instance_metadata_offset = 0U,
    .flags = pass_mask,
    .transform_generation = 1U,
    .submesh_index = 0U,
    .primitive_flags = primitive_flags,
  };
}

NOLINT_TEST(ConventionalShadowDrawRecordBuilderTest,
  BuildConventionalShadowDrawRecords_RetainsShadowOnlyCastersAndPreservesFlags)
{
  const auto kStaticShadowCaster
    = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kStaticShadowCaster);
  const auto kMainViewVisible
    = static_cast<std::uint32_t>(DrawPrimitiveFlagBits::kMainViewVisible);

  const auto draw_metadata = std::to_array<DrawMetadata>({
    MakeDraw(PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque },
      kStaticShadowCaster),
    MakeDraw(PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque,
               PassMaskBit::kMainViewVisible },
      kMainViewVisible),
    MakeDraw(PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kMasked,
               PassMaskBit::kDoubleSided, PassMaskBit::kMainViewVisible },
      kStaticShadowCaster | kMainViewVisible),
    MakeDraw(
      PassMask { PassMaskBit::kTransparent, PassMaskBit::kMainViewVisible },
      kMainViewVisible),
  });

  const auto draw_bounds = std::to_array<glm::vec4>({
    { 1.0F, 0.0F, 0.0F, 2.0F },
    { 2.0F, 0.0F, 0.0F, 3.0F },
    { 3.0F, 0.0F, 0.0F, 4.0F },
    { 4.0F, 0.0F, 0.0F, 5.0F },
  });

  const auto partitions = std::to_array<PreparedSceneFrame::PartitionRange>({
    PreparedSceneFrame::PartitionRange {
      .pass_mask
      = PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque },
      .begin = 0U,
      .end = 1U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque,
        PassMaskBit::kMainViewVisible },
      .begin = 1U,
      .end = 2U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask = PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kMasked,
        PassMaskBit::kDoubleSided, PassMaskBit::kMainViewVisible },
      .begin = 2U,
      .end = 3U,
    },
    PreparedSceneFrame::PartitionRange {
      .pass_mask
      = PassMask { PassMaskBit::kTransparent, PassMaskBit::kMainViewVisible },
      .begin = 3U,
      .end = 4U,
    },
  });

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes
    = std::as_bytes(std::span { draw_metadata.data(), draw_metadata.size() });
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.partitions = std::span(partitions);

  auto records = std::vector<ConventionalShadowDrawRecord> {};
  BuildConventionalShadowDrawRecords(prepared_frame, records);

  ASSERT_EQ(records.size(), 3U);

  EXPECT_EQ(records[0].draw_index, 0U);
  EXPECT_EQ(records[0].partition_index, 0U);
  EXPECT_EQ(records[0].world_bounding_sphere, draw_bounds[0]);
  EXPECT_TRUE(oxygen::renderer::IsStaticShadowCaster(records[0]));
  EXPECT_FALSE(oxygen::renderer::IsMainViewVisible(records[0]));

  EXPECT_EQ(records[1].draw_index, 1U);
  EXPECT_EQ(records[1].partition_index, 1U);
  EXPECT_EQ(records[1].world_bounding_sphere, draw_bounds[1]);
  EXPECT_FALSE(oxygen::renderer::IsStaticShadowCaster(records[1]));
  EXPECT_TRUE(oxygen::renderer::IsMainViewVisible(records[1]));
  EXPECT_TRUE(records[1].partition_pass_mask.IsSet(PassMaskBit::kOpaque));

  EXPECT_EQ(records[2].draw_index, 2U);
  EXPECT_EQ(records[2].partition_index, 2U);
  EXPECT_EQ(records[2].world_bounding_sphere, draw_bounds[2]);
  EXPECT_TRUE(oxygen::renderer::IsStaticShadowCaster(records[2]));
  EXPECT_TRUE(oxygen::renderer::IsMainViewVisible(records[2]));
  EXPECT_TRUE(records[2].partition_pass_mask.IsSet(PassMaskBit::kMasked));
  EXPECT_TRUE(records[2].partition_pass_mask.IsSet(PassMaskBit::kDoubleSided));
}

NOLINT_TEST(ConventionalShadowDrawRecordBuilderTest,
  BuildConventionalShadowDrawRecords_RejectsMismatchedDrawBounds)
{
  const auto draw_metadata = std::to_array<DrawMetadata>({
    MakeDraw(PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque }, 0U),
    MakeDraw(PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque }, 0U),
  });

  const auto draw_bounds = std::to_array<glm::vec4>({
    { 0.0F, 0.0F, 0.0F, 1.0F },
  });

  const auto partitions = std::to_array<PreparedSceneFrame::PartitionRange>({
    PreparedSceneFrame::PartitionRange {
      .pass_mask
      = PassMask { PassMaskBit::kShadowCaster, PassMaskBit::kOpaque },
      .begin = 0U,
      .end = 2U,
    },
  });

  auto prepared_frame = PreparedSceneFrame {};
  prepared_frame.draw_metadata_bytes
    = std::as_bytes(std::span { draw_metadata.data(), draw_metadata.size() });
  prepared_frame.draw_bounding_spheres = std::span(draw_bounds);
  prepared_frame.partitions = std::span(partitions);

  auto records = std::vector<ConventionalShadowDrawRecord> {};
  BuildConventionalShadowDrawRecords(prepared_frame, records);

  EXPECT_TRUE(records.empty());
}

} // namespace
