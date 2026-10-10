//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

#include <fmt/format.h>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float4.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Internal/MeshRasterState.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/DrawCullPass.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Internal/IndirectListBuilder.h>
#include <Oxygen/Vortex/SceneRenderer/Stages/Occlusion/Types/DrawVisibility.h>
#include <Oxygen/Vortex/Test/Occlusion/OcclusionGpuTest.h>
#include <Oxygen/Vortex/Types/DrawCullRecord.h>
#include <Oxygen/Vortex/Types/DrawMetadata.h>

namespace {

using oxygen::graphics::Buffer;
using oxygen::graphics::CommandRecorder;
using oxygen::vortex::DrawCullFlagBits;
using oxygen::vortex::DrawCullRecord;
using oxygen::vortex::DrawMetadata;
using oxygen::vortex::DrawVisibilityBit;
using oxygen::vortex::DrawVisibilityPredicate;
using oxygen::vortex::DrawVisibilityProducts;
using oxygen::vortex::ToUnderlying;
using oxygen::vortex::internal::MeshRasterState;
using oxygen::vortex::occlusion::internal::DrawCullInputs;
using oxygen::vortex::occlusion::internal::DrawCullPass;
using oxygen::vortex::occlusion::internal::IndirectDrawCandidate;
using oxygen::vortex::occlusion::internal::IndirectDrawCommand;
using oxygen::vortex::occlusion::internal::IndirectDrawList;
using oxygen::vortex::occlusion::internal::IndirectListBuilder;

constexpr auto kCommandUints
  = sizeof(IndirectDrawCommand) / sizeof(std::uint32_t);

//! Deterministic pseudo-random words.
class Noise {
public:
  auto Next() -> std::uint32_t
  {
    state_ = (state_ * 1664525U) + 1013904223U;
    return state_ >> 8U;
  }

private:
  std::uint32_t state_ { 0x9E3779B9U };
};

//! Candidates whose raster state changes every few draws, so the list has
//! many segments. Draw indices run backwards, like a back-to-front sort.
auto MakeCandidates(const std::uint32_t count)
  -> std::vector<IndirectDrawCandidate>
{
  constexpr auto kStates = std::array {
    MeshRasterState {},
    MeshRasterState { .double_sided = true },
    MeshRasterState { .alpha_test = true, .reverse_winding = true },
  };
  auto noise = Noise {};
  auto candidates = std::vector<IndirectDrawCandidate> {};
  auto state = std::size_t { 0U };
  for (std::uint32_t index = 0U; index < count; ++index) {
    if (noise.Next() % 37U == 0U) {
      state = (state + 1U) % kStates.size();
    }
    candidates.push_back(IndirectDrawCandidate {
      .draw_index = count - 1U - index,
      .vertex_count = 3U * ((index % 7U) + 1U),
      .instance_count = (index % 3U) + 1U,
      .raster_state = kStates.at(state),
    });
  }
  return candidates;
}

//! The commands the CPU expects for one segment: its kept candidates in order.
auto ExpectedSegmentCommands(
  const std::span<const IndirectDrawCandidate> segment,
  const std::vector<std::uint32_t>& visibility,
  const DrawVisibilityPredicate predicate) -> std::vector<std::uint32_t>
{
  auto commands = std::vector<std::uint32_t> {};
  for (const auto& candidate : segment) {
    if (!predicate.KeepsAll()
      && (visibility.at(candidate.draw_index) & predicate.Mask()) == 0U) {
      continue;
    }
    commands.insert(commands.end(),
      {
        candidate.vertex_count,
        candidate.instance_count,
        0U,
        candidate.draw_index,
      });
  }
  return commands;
}

class IndirectListsGpuTest
  : public oxygen::vortex::testing::occlusion::OcclusionGpuTest {
protected:
  //! Builds a list on the GPU and waits for it.
  auto BuildList(IndirectListBuilder& builder,
    const std::span<const IndirectDrawCandidate> candidates,
    const DrawVisibilityProducts& visibility,
    const DrawVisibilityPredicate predicate) -> IndirectDrawList
  {
    NextFrame();
    auto list = SubmitCommands("Indirect list build",
      [&](CommandRecorder& recorder) -> IndirectDrawList {
        return builder.Build(recorder, ctx_.frame_sequence, ctx_.frame_slot,
          candidates, visibility, predicate);
      });
    WaitForQueueIdle();
    return list;
  }

  //! Each segment's count followed by its compacted commands.
  auto ReadList(const IndirectDrawList& list)
    -> std::vector<std::vector<std::uint32_t>>
  {
    const auto counts = ReadUints(*list.counts, 0U, list.segments.size());
    auto segments = std::vector<std::vector<std::uint32_t>> {};
    for (std::size_t index = 0U; index < list.segments.size(); ++index) {
      const auto& segment = list.segments.at(index);
      const auto count = counts.at(index);
      auto commands = count == 0U
        ? std::vector<std::uint32_t> {}
        : ReadUints(*list.arguments,
            static_cast<std::uint64_t>(segment.first_candidate)
              * sizeof(IndirectDrawCommand),
            count * kCommandUints);
      segments.push_back(std::move(commands));
    }
    return segments;
  }

  auto ExpectListMatchesCpu(const IndirectDrawList& list,
    const std::span<const IndirectDrawCandidate> candidates,
    const std::vector<std::uint32_t>& visibility,
    const DrawVisibilityPredicate predicate) -> void
  {
    ASSERT_FALSE(list.IsEmpty());
    const auto segments = ReadList(list);
    auto covered = std::size_t { 0U };
    for (std::size_t index = 0U; index < list.segments.size(); ++index) {
      const auto& segment = list.segments.at(index);
      EXPECT_EQ(segment.first_candidate, covered);
      covered += segment.candidate_count;
      const auto expected = ExpectedSegmentCommands(
        candidates.subspan(segment.first_candidate, segment.candidate_count),
        visibility, predicate);
      EXPECT_EQ(segments.at(index), expected) << "segment " << index;
    }
    EXPECT_EQ(covered, candidates.size());
  }
};

//! With every draw kept, the GPU lists are the CPU draw sets in CPU order, for
//! sizes around the scan group and above the two-level scan's 65,536.
NOLINT_TEST_F(IndirectListsGpuTest, KeepAllListsEqualCpuDrawOrder)
{
  auto builder = IndirectListBuilder(*renderer_, "Indirect list test");
  for (const auto count : { 1U, 255U, 256U, 257U, 1000U, 70001U }) {
    TRACE_GCHECK_F(([&] -> void {
      const auto candidates = MakeCandidates(count);
      const auto list = BuildList(builder, candidates,
        DrawVisibilityProducts {}, DrawVisibilityPredicate {});
      ExpectListMatchesCpu(list, candidates, {}, DrawVisibilityPredicate {});
    }()),
      fmt::format("{} candidates", count));
  }
}

//! The lists keep exactly the candidates whose visibility matches the
//! predicate, in candidate order within every segment.
NOLINT_TEST_F(IndirectListsGpuTest, CompactionMatchesCpuFilter)
{
  auto builder = IndirectListBuilder(*renderer_, "Indirect list test");
  for (const auto count : { 300U, 70001U }) {
    TRACE_GCHECK_F(([&] -> void {
      const auto candidates = MakeCandidates(count);
      auto noise = Noise {};
      auto visibility = std::vector<std::uint32_t>(count);
      for (auto& bits : visibility) {
        bits = noise.Next() & 0xFU;
      }
      const auto [buffer, srv] = MakeDeviceBuffer(
        std::span<const std::uint32_t>(visibility), "Indirect list visibility");
      const auto products = DrawVisibilityProducts {
        .buffer = oxygen::observer_ptr<const Buffer> { buffer.get() },
        .srv = srv,
        .draw_count = count,
      };
      for (const auto predicate : {
             DrawVisibilityPredicate { DrawVisibilityBit::kPhase1Drawn },
             DrawVisibilityPredicate::Drawn(),
             DrawVisibilityPredicate { DrawVisibilityBit::kInFrustum },
           }) {
        const auto list = BuildList(builder, candidates, products, predicate);
        ExpectListMatchesCpu(list, candidates, visibility, predicate);
      }
    }()),
      fmt::format("{} candidates", count));
  }
}

//! Building the same list twice produces the same commands and counts.
NOLINT_TEST_F(IndirectListsGpuTest, RebuildsAreIdentical)
{
  auto builder = IndirectListBuilder(*renderer_, "Indirect list test");
  constexpr auto kCount = 5000U;
  const auto candidates = MakeCandidates(kCount);
  auto noise = Noise {};
  auto visibility = std::vector<std::uint32_t>(kCount);
  for (auto& bits : visibility) {
    bits = noise.Next() & 0x3U;
  }
  const auto [buffer, srv] = MakeDeviceBuffer(
    std::span<const std::uint32_t>(visibility), "Indirect list visibility");
  const auto products = DrawVisibilityProducts {
    .buffer = oxygen::observer_ptr<const Buffer> { buffer.get() },
    .srv = srv,
    .draw_count = kCount,
  };
  const auto predicate
    = DrawVisibilityPredicate { DrawVisibilityBit::kPhase1Drawn };

  const auto first
    = ReadList(BuildList(builder, candidates, products, predicate));
  const auto second
    = ReadList(BuildList(builder, candidates, products, predicate));
  EXPECT_EQ(first, second);
}

//! An orthographic 64 x 64 view: NDC equals world xy, device depth equals
//! world z, and pixel x = (ndc x + 1) * 32.
class DrawCullGpuTest : public IndirectListsGpuTest {
protected:
  static constexpr float kExtent = 64.0F;

  //! The NDC coordinate of pixel-space position `pixel`.
  static constexpr auto Ndc(const float pixel) -> float
  {
    return (pixel / (kExtent * 0.5F)) - 1.0F;
  }

  //! A world box spanning the pixel rect (x0, x1, y0, y1) at depth 0.5.
  static auto PixelBox(const glm::vec4& rect, const std::uint32_t flags = 0U)
    -> DrawCullRecord
  {
    // Pixel y grows down; NDC y grows up.
    const auto min = glm::vec3 { Ndc(rect.x), -Ndc(rect.w), 0.4F };
    const auto max = glm::vec3 { Ndc(rect.y), -Ndc(rect.z), 0.6F };
    return DrawCullRecord {
      .box_center = (min + max) * 0.5F,
      .box_extent = (max - min) * 0.5F,
      .flags = flags,
    };
  }

  //! Culls `records` (one draw each, world matrix `worlds[transform]`) and
  //! returns each draw's visibility bits.
  auto Cull(const std::span<const DrawCullRecord> records,
    const std::vector<std::uint32_t>& transforms,
    const std::span<const glm::mat4> worlds, const glm::mat4& view_projection)
    -> std::vector<std::uint32_t>
  {
    auto metadata = std::vector<DrawMetadata>(records.size());
    for (const auto& [draw, transform] :
      std::views::zip(metadata, transforms)) {
      draw.transform_index = transform;
      draw.instance_count = 1U;
    }
    const auto [metadata_buffer, metadata_srv] = MakeUploadBuffer(
      std::span<const DrawMetadata>(metadata), "Cull test metadata");
    const auto [records_buffer, records_srv]
      = MakeUploadBuffer(records, "Cull test records");
    const auto [worlds_buffer, worlds_srv]
      = MakeUploadBuffer(worlds, "Cull test worlds");

    auto frame = oxygen::vortex::PreparedSceneFrame {};
    frame.draw_metadata_bytes = std::as_bytes(std::span(metadata));
    frame.bindless_draw_metadata_slot = metadata_srv;
    frame.bindless_draw_cull_records_slot = records_srv;
    frame.bindless_worlds_slot = worlds_srv;

    auto pass = DrawCullPass(*renderer_, "Cull test");
    NextFrame();
    const auto products = SubmitCommands(
      "Cull test", [&](CommandRecorder& recorder) -> DrawVisibilityProducts {
        return pass.RunPhase1(recorder,
          DrawCullInputs {
            .frame_sequence = ctx_.frame_sequence,
            .frame_slot = ctx_.frame_slot,
            .prepared_frame
            = oxygen::observer_ptr<const oxygen::vortex::PreparedSceneFrame> {
                &frame,
              },
            .projection_matrix = view_projection,
            .viewport = {
              .top_left_x = 0.0F,
              .top_left_y = 0.0F,
              .width = kExtent,
              .height = kExtent,
              .min_depth = 0.0F,
              .max_depth = 1.0F,
            },
            .scissors = {
              .left = 0,
              .top = 0,
              .right = static_cast<std::int32_t>(kExtent),
              .bottom = static_cast<std::int32_t>(kExtent),
            },
          });
      });
    WaitForQueueIdle();
    CHECK_F(products.IsValid());
    return ReadUints(*products.buffer, 0U, records.size());
  }
};

constexpr auto kDrawn = ToUnderlying(DrawVisibilityBit::kInFrustum)
  | ToUnderlying(DrawVisibilityBit::kPhase1Drawn)
  | ToUnderlying(DrawVisibilityBit::kVisible);

//! A box is culled exactly when the rasterizer would produce no pixel for it:
//! outside a clip plane, or with no pixel center inside its projected rect.
NOLINT_TEST_F(DrawCullGpuTest, CullsBoxesThatCoverNoPixelCenter)
{
  const auto nan = std::numeric_limits<float>::quiet_NaN();
  auto non_finite = PixelBox({ 10.0F, 20.0F, 10.0F, 20.0F },
    ToUnderlying(DrawCullFlagBits::kAlwaysVisible));
  non_finite.box_center.x = nan;
  struct Case {
    const char* name { nullptr };
    DrawCullRecord record;
    std::uint32_t transform { 0U };
    std::uint32_t expected { 0U };
  };
  const auto cases = std::array {
    Case {
      .name="covers many pixels", .record=PixelBox({ 10.0F, 20.0F, 10.0F, 20.0F }), .transform=0U, .expected=kDrawn, },
    // Pixel centers sit at k + 0.5.
    Case {
      .name="between centers in x", .record=PixelBox({ 10.6F, 11.4F, 10.0F, 20.0F }), .transform=0U, .expected=0U, },
    Case {
      .name="between centers in y", .record=PixelBox({ 10.0F, 20.0F, 30.6F, 31.4F }), .transform=0U, .expected=0U, },
    Case { .name="thin box over a center", .record=PixelBox({ 10.45F, 10.55F, 30.45F, 30.55F }),
      .transform=0U, .expected=kDrawn, },
    Case { .name="right of the view", .record=PixelBox({ 64.2F, 70.0F, 10.0F, 20.0F }), .transform=0U, .expected=0U },
    Case { .name="over the last column center", .record=PixelBox({ 63.4F, 70.0F, 10.0F, 20.0F }),
      .transform=0U, .expected=kDrawn, },
    Case { .name="behind the far plane",
      .record=DrawCullRecord { .box_center = { 0.0F, 0.0F, -0.5F },
          .box_extent = { 0.2F, 0.2F, 0.1F },
        .flags = 0U, },
      .transform=0U, .expected=0U, },
    Case { .name="moved into view by its world matrix",
      .record=PixelBox({ 100.0F, 110.0F, 10.0F, 20.0F }), .transform=1U, .expected=kDrawn, },
    Case { .name="world-space box ignores its world matrix",
      .record=PixelBox({ 100.0F, 110.0F, 10.0F, 20.0F },
        ToUnderlying(DrawCullFlagBits::kWorldSpaceBox)),
      .transform=1U, .expected=0U, },
    Case { .name="non-finite bounds", .record=non_finite, .transform=0U, .expected=kDrawn },
  };

  auto records = std::vector<DrawCullRecord> {};
  auto transforms = std::vector<std::uint32_t> {};
  for (const auto& test_case : cases) {
    records.push_back(test_case.record);
    transforms.push_back(test_case.transform);
  }
  // Transform 1 moves pixel x 100 to pixel x 20.
  const auto worlds = std::array {
    glm::mat4 { 1.0F },
    glm::translate(
      glm::mat4 { 1.0F }, glm::vec3 { Ndc(20.0F) - Ndc(100.0F), 0.0F, 0.0F }),
  };
  const auto visibility = Cull(records, transforms, worlds, glm::mat4 { 1.0F });
  for (std::size_t index = 0U; index < cases.size(); ++index) {
    EXPECT_EQ(visibility.at(index), cases.at(index).expected)
      << cases.at(index).name;
  }
}

//! A box crossing the near plane is drawn even though its corners behind the
//! camera have no meaningful projection.
NOLINT_TEST_F(DrawCullGpuTest, BoxCrossingTheNearPlaneIsDrawn)
{
  // Columns of an infinite reversed-Z perspective: clip = (x, y, 0.1, -z), so
  // points with z > 0 are behind the camera.
  const auto projection = glm::mat4 {
    glm::vec4 { 1.0F, 0.0F, 0.0F, 0.0F },
    glm::vec4 { 0.0F, 1.0F, 0.0F, 0.0F },
    glm::vec4 { 0.0F, 0.0F, 0.0F, -1.0F },
    glm::vec4 { 0.0F, 0.0F, 0.1F, 0.0F },
  };
  const auto records = std::array {
    // Straddles the camera, centered off to the side.
    DrawCullRecord {
      .box_center = { 0.5F, 0.0F, 0.0F },
      .box_extent = { 0.2F, 0.2F, 1.0F },
      .flags = 0U,
    },
    // Fully behind the camera.
    DrawCullRecord {
      .box_center = { 0.0F, 0.0F, 5.0F },
      .box_extent = { 0.2F, 0.2F, 1.0F },
      .flags = 0U,
    },
  };
  const auto transforms = std::vector { 0U, 0U };
  const auto worlds = std::array { glm::mat4 { 1.0F } };
  const auto visibility = Cull(records, transforms, worlds, projection);
  EXPECT_EQ(visibility.at(0), kDrawn);
  EXPECT_EQ(visibility.at(1), 0U);
}

} // namespace
