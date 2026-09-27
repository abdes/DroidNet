//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/ManagedResource.h>
#include <Oxygen/Graphics/Common/Registration.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Test/Fixtures/UploadCoordinatorTest.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadPlanner.h>
#include <Oxygen/Vortex/Upload/UploadPolicy.h>

namespace oxygen::vortex::upload::testing {
namespace {
  constexpr std::uint32_t kWidth = 3U;
  constexpr std::uint32_t kHeight = 2U;
  constexpr std::uint32_t kPitchedRow = 5U;
  constexpr std::array kPixels {
    std::byte { 1 },
    std::byte { 2 },
    std::byte { 3 },
    std::byte { 99 },
    std::byte { 99 },
    std::byte { 4 },
    std::byte { 5 },
    std::byte { 6 },
  };

  class ImmutableTextureUploadTest : public UploadCoordinatorTest {
  protected:
    auto MakeDestination() -> graphics::ManagedTexture
    {
      auto result = Gfx().GetResourceRegistry().RegisterManagedTexture(
        Gfx().CreateTexture({
          .width = kWidth,
          .height = kHeight,
          .format = Format::kR8UNorm,
          .texture_type = TextureType::kTexture2D,
          .is_shader_resource = true,
          .initial_state = graphics::ResourceStates::kCommon,
        }));
      CHECK_F(result.has_value());
      return std::move(*result);
    }

    static auto Source() -> UploadTextureSourceView
    {
      return {
        .subresources = { UploadTextureSourceSubresource {
          .bytes = kPixels,
          .row_pitch = kPitchedRow,
          .slice_pitch = static_cast<std::uint32_t>(kPixels.size()),
        }, },
      };
    }
  };
} // namespace

NOLINT_TEST_F(ImmutableTextureUploadTest, PacksPitchedRowsAndInitializesPadding)
{
  const auto destination = MakeDestination();
  const auto policy = DefaultUploadPolicy();
  const auto plan
    = UploadPlanner::PlanTexture2D({ .dst = destination.resource }, {}, policy);
  ASSERT_TRUE(plan);
  auto packed = std::vector<std::byte>(plan->total_bytes, std::byte { 99 });
  ASSERT_TRUE(plan->Pack2D(
    destination.resource->GetDescriptor(), Source(), packed, policy.filler));
  const auto pitch = plan->regions.front().buffer_row_pitch;
  auto expected = std::vector<std::byte>(plan->total_bytes);
  const auto first = std::span(kPixels).first(kWidth);
  const auto second = std::span(kPixels).subspan(kPitchedRow, kWidth);
  std::ranges::copy(first, std::span(expected).first(kWidth).begin());
  std::ranges::copy(second, std::span(expected).subspan(pitch, kWidth).begin());
  EXPECT_EQ(packed, expected);
}

NOLINT_TEST_F(ImmutableTextureUploadTest, RejectsShortAndOverflowingLayouts)
{
  const auto destination = MakeDestination();
  const auto description = destination.resource->GetDescriptor();
  const auto policy = DefaultUploadPolicy();
  const auto planned
    = UploadPlanner::PlanTexture2D({ .dst = destination.resource }, {}, policy);
  ASSERT_TRUE(planned);
  auto packed = std::vector<std::byte>(planned->total_bytes);
  auto source = Source();
  source.subresources.front().bytes
    = std::span(kPixels).first(kPixels.size() - 1U);
  EXPECT_FALSE(planned->Pack2D(description, source, packed, policy.filler));
  source = Source();
  source.subresources.front().row_pitch = kWidth - 1U;
  EXPECT_FALSE(planned->Pack2D(description, source, packed, policy.filler));
  source = Source();
  source.subresources.front().slice_pitch = kWidth;
  EXPECT_FALSE(planned->Pack2D(description, source, packed, policy.filler));
  EXPECT_FALSE(planned->Pack2D(description, Source(),
    std::span(packed).first(packed.size() - 1U), policy.filler));
  auto plan = *planned;
  plan.regions.front().buffer_offset
    = std::numeric_limits<std::uint64_t>::max();
  EXPECT_FALSE(plan.Pack2D(description, Source(), packed, policy.filler));
  plan = *planned;
  plan.regions.front().buffer_row_pitch
    = std::numeric_limits<std::uint64_t>::max();
  EXPECT_FALSE(plan.Pack2D(description, Source(), packed, policy.filler));
}

NOLINT_TEST_F(
  ImmutableTextureUploadTest, RecordingOwnsResourcesAfterPreparationDies)
{
  auto recording = Gfx().AcquireCommandRecorder(
    Gfx().QueueKeyFor(graphics::QueueRole::kGraphics), "Immutable upload test",
    graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(recording);
  auto destination_identity = graphics::RegistrationIdentity {};
  auto staging_identity = graphics::RegistrationIdentity {};
  {
    auto prepared
      = Uploader().PrepareImmutableTexture2D(MakeDestination(), Source());
    ASSERT_TRUE(prepared);
    EXPECT_FALSE(Gfx().texture_log_.copy_called);
    destination_identity = prepared->Destination().registration.Identity();
    ASSERT_TRUE(prepared->Record(*recording));
    ASSERT_TRUE(Gfx().texture_log_.copy_called);
    const auto staging = Gfx().GetResourceRegistry().InspectManagedIdentity(
      *Gfx().texture_log_.src);
    if (!staging.has_value()) {
      FAIL() << "Recorded staging buffer lost its managed registration";
    }
    staging_identity = staging.value();
    const auto repeated = prepared->Record(*recording);
    ASSERT_FALSE(repeated);
    EXPECT_EQ(repeated.error(), UploadError::kInvalidRequest);
  }
  EXPECT_TRUE(recording->RetainsRegistration(destination_identity));
  EXPECT_TRUE(recording->RetainsRegistration(staging_identity));
  const auto submission = recording.SubmitWithReceipt();
  EXPECT_EQ(submission.outcome, graphics::SubmissionOutcome::kSubmitted);
  EXPECT_TRUE(submission.receipt);
}

NOLINT_TEST_F(ImmutableTextureUploadTest, MappingFailureIsRetryable)
{
  Gfx().SetFailMap(true);
  const auto failed
    = Uploader().PrepareImmutableTexture2D(MakeDestination(), Source());
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), UploadError::kStagingMapFailed);
  Gfx().SetFailMap(false);
  EXPECT_TRUE(
    Uploader().PrepareImmutableTexture2D(MakeDestination(), Source()));
}

NOLINT_TEST_F(ImmutableTextureUploadTest, RejectsBoxedUpdatesBeforeRecording)
{
  const auto boxes = std::array {
    UploadSubresource { .width = 1U, .height = 1U },
    UploadSubresource { .x = 1U },
  };
  for (const auto& box : boxes) {
    const auto prepared = Uploader().PrepareImmutableTexture2D(
      MakeDestination(), Source(), std::span(&box, 1U));
    ASSERT_FALSE(prepared);
    EXPECT_EQ(prepared.error(), UploadError::kInvalidRequest);
  }
  EXPECT_FALSE(Gfx().texture_log_.copy_called);
}

NOLINT_TEST_F(ImmutableTextureUploadTest, StagingAllocationFailureIsRetryable)
{
  Gfx().SetThrowOnCreateBuffer(true);
  const auto failed
    = Uploader().PrepareImmutableTexture2D(MakeDestination(), Source());
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), UploadError::kStagingAllocFailed);
  Gfx().SetThrowOnCreateBuffer(false);
  EXPECT_TRUE(
    Uploader().PrepareImmutableTexture2D(MakeDestination(), Source()));
}

NOLINT_TEST_F(ImmutableTextureUploadTest, BackendCloseRejectsPreparedRecording)
{
  auto prepared
    = Uploader().PrepareImmutableTexture2D(MakeDestination(), Source());
  ASSERT_TRUE(prepared);
  auto recording = Gfx().AcquireCommandRecorder(
    Gfx().QueueKeyFor(graphics::QueueRole::kGraphics),
    "Closed immutable upload", graphics::SubmissionPolicy::kExplicit);
  ASSERT_TRUE(recording);
  Gfx().GetResourceRegistry().Close();
  const auto recorded = prepared->Record(*recording);
  ASSERT_FALSE(recorded);
  EXPECT_EQ(recorded.error(), UploadError::kDeviceLost);
  EXPECT_FALSE(Gfx().texture_log_.copy_called);
}
} // namespace oxygen::vortex::upload::testing
