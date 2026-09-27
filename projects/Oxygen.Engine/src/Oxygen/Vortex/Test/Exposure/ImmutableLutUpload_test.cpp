//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/ByteUnits.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/FrameCaptureController.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Environment/Internal/IblBrdfLookup.h>
#include <Oxygen/Vortex/Environment/Internal/IblGpuProcessor.h>
#include <Oxygen/Vortex/Lighting/Internal/BrdfEnergyData.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightingPreparationFailure.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#include <Oxygen/Vortex/Types/LightingFrameBindings.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/ImmutableTextureUpload.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  constexpr std::uint32_t kLutTexelProbe = 262144U;
  using Texel = std::array<std::uint32_t, 4>;

  auto Texels(const ShaderVisibleIndex descriptor, const std::uint32_t width,
    const std::uint32_t height, const bool unorm) -> std::vector<Texel>
  {
    auto texels = std::vector<Texel> {};
    texels.reserve(static_cast<std::size_t>(width) * height);
    for (std::uint32_t y = 0U; y < height; ++y) {
      for (std::uint32_t x = 0U; x < width; ++x) {
        texels.push_back(Texel { descriptor.get(), x, y, unorm ? 1U : 0U });
      }
    }
    return texels;
  }

  auto PrepareEnergyPublication(LightingService& service)
    -> std::expected<void, LightingPreparationFailure>
  {
    service.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
    auto selection = FrameLightSelection {};
    selection.selection_epoch = 1U;
    selection.scene_generation = 1U;
    auto parameters = ResolvedView::Params {};
    constexpr float kViewExtent = 16.0F;
    parameters.view_config.viewport = ViewPort {
      .width = kViewExtent,
      .height = kViewExtent,
    };
    const auto resolved = ResolvedView(parameters);
    const auto view = ViewId { 1U };
    const auto views = std::array {
      PreparedViewLightingInput {
        .view_id = view,
        .prepared_scene = {},
        .resolved_view = observer_ptr { &resolved },
        .composition_view = {},
      },
    };
    return service.BuildLightGrid({
      .frame_light_set = &selection,
      .active_views = views,
    });
  }
} // namespace

NOLINT_TEST_F(
  ExposureGpuTest, ImmutableIblLutPreservesEveryTexelAfterOwnerRelease)
{
  namespace env = environment::internal;
  const auto capture = BeginOptionalCapture();
  auto owner = std::make_unique<env::IblBrdfResources>(*renderer_);
  auto prepared = owner->Prepare();
  ASSERT_TRUE(prepared);
  auto product = std::move(*prepared);
  const auto texture_lifetime = std::weak_ptr(product->texture);
  const auto texels
    = Texels(product->srv, env::kIblBrdfWidth, env::kIblBrdfHeight, true);
  const auto output = RunToneProbe(std::as_bytes(std::span(texels)),
    static_cast<std::uint32_t>(texels.size()), kLutTexelProbe, false,
    [&](graphics::CommandRecorder& recorder) -> void {
      ASSERT_TRUE(recorder.RetainRegistration(
        Backend().GetResourceRegistry(), product->registration));
      recorder.RecordDependency(product->producer);
      product.reset();
      owner.reset();
      EXPECT_FALSE(texture_lifetime.expired());
    });
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  const auto expected = env::GetIblBrdfLookup();
  ASSERT_EQ(output.size(), expected.size());
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    const auto texel = expected.subspan(index, 1U).front();
    EXPECT_EQ(output.at(index).at(0), static_cast<float>(texel.at(0)));
    EXPECT_EQ(output.at(index).at(1), static_cast<float>(texel.at(1)));
  }
  WaitForQueueIdle();
  Backend().PollCompletedUses();
  EXPECT_TRUE(texture_lifetime.expired());
}

NOLINT_TEST_F(
  ExposureGpuTest, ImmutableEnergyLutPreservesEveryBitAfterServiceRelease)
{
  const auto model = lighting::internal::GetBrdfEnergyData();
  ASSERT_TRUE(model);
  const auto capture = BeginOptionalCapture();
  auto service = std::make_unique<LightingService>(*renderer_);
  ASSERT_TRUE(PrepareEnergyPublication(*service));
  const auto view = ViewId { 1U };
  const auto* bindings = service->InspectForwardLightBindings(view);
  ASSERT_NE(bindings, nullptr);
  const auto texels = Texels(bindings->brdf_energy_srv, model->view_nodes,
    model->roughness_nodes, false);
  const auto output = RunToneProbe(std::as_bytes(std::span(texels)),
    static_cast<std::uint32_t>(texels.size()), kLutTexelProbe, false,
    [&](graphics::CommandRecorder& recorder) -> void {
      ASSERT_TRUE(service->AttachResources(view, recorder));
      service.reset();
    });
  if (capture) {
    EXPECT_TRUE(capture->EndCapture());
  }
  ASSERT_EQ(output.size() * sizeof(float) * 2U, model->energy.size());
  for (std::size_t index = 0U; index < output.size(); ++index) {
    auto expected = std::array<std::uint32_t, 2> {};
    std::memcpy(expected.data(),
      model->energy.subspan(index * sizeof(expected), sizeof(expected)).data(),
      sizeof(expected));
    EXPECT_EQ(
      std::bit_cast<std::uint32_t>(output.at(index).at(0)), expected.at(0));
    EXPECT_EQ(
      std::bit_cast<std::uint32_t>(output.at(index).at(1)), expected.at(1));
  }
}
NOLINT_TEST_F(
  ExposureGpuTest, ImmutableLutSubmissionFailuresRemainUnpublishedAndRetryable)
{
  using graphics::internal::SubmissionFailurePoint;
  using graphics::internal::SubmissionFaultTestAccess;
  namespace env = environment::internal;
  const auto queue = Backend().GetCommandQueue(graphics::QueueRole::kGraphics);
  ASSERT_NE(queue, nullptr);
  FailureBackend().track_resources = true;
  for (const auto point : {
         SubmissionFailurePoint::kBeforeIssue,
         SubmissionFailurePoint::kAfterFirstList,
         SubmissionFailurePoint::kAfterIssueBeforeMarker,
       }) {
    auto owner = env::IblBrdfResources(*renderer_);
    SubmissionFaultTestAccess::FailNext(*queue, point);
    const auto failed = owner.Prepare();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error(), env::IblProcessError::kSubmissionFailed);
    ASSERT_FALSE(FailureBackend().tracked_textures.empty());
    const auto candidate = FailureBackend().tracked_textures.back();
    if (point != SubmissionFailurePoint::kBeforeIssue) {
      EXPECT_FALSE(candidate.expired());
      ASSERT_TRUE(Backend().RecoverSubmissionFault());
    }
    Backend().PollCompletedUses();
    EXPECT_TRUE(candidate.expired());
    const auto retried = owner.Prepare();
    ASSERT_TRUE(retried);
    EXPECT_TRUE((*retried)->srv.IsValid());
    const auto cached = owner.Prepare();
    ASSERT_TRUE(cached);
    EXPECT_EQ(*cached, *retried);
    WaitForQueueIdle();
  }
}

NOLINT_TEST_F(ExposureGpuTest, ImmutableLutStagingUsesDestinationBudget)
{
  constexpr auto kSingleNativeAllocation = SizeBytes { 64ULL * 1024ULL };
  auto budget = std::make_shared<graphics::AllocationBudget>(
    graphics::AllocationBudgetLimits {
      .total = kSingleNativeAllocation,
      .compact_indices = kSingleNativeAllocation,
      .driver_headroom = SizeBytes { 0U },
    });
  auto destination = Backend().GetResourceRegistry().RegisterManagedTexture(
    Backend().CreateTexture({
      .width = 1U,
      .height = 1U,
      .format = Format::kRG32Float,
      .texture_type = TextureType::kTexture2D,
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon,
      .allocation_budget = { .owner = budget },
    }));
  ASSERT_TRUE(destination);
  EXPECT_GT(budget->Snapshot().allocated.get(), 0U);
  const auto pixels = std::array<float, 2> { 1.0F, 0.0F };
  const auto source = upload::UploadTextureSourceView {
    .subresources = { upload::UploadTextureSourceSubresource {
      .bytes = std::as_bytes(std::span(pixels)),
      .row_pitch = sizeof(pixels), .slice_pitch = sizeof(pixels),
    }, },
  };
  const auto failed
    = renderer_->GetUploadCoordinator().PrepareImmutableTexture2D(
      std::move(*destination), source);
  ASSERT_FALSE(failed);
  EXPECT_EQ(failed.error(), upload::UploadError::kBudgetExceeded);
  EXPECT_EQ(budget->Snapshot().rejected_requests, 1U);
  Backend().PollCompletedUses();
  EXPECT_EQ(budget->Snapshot().allocated.get(), 0U);
}

NOLINT_TEST_F(ExposureGpuTest, ImmutableLutDiscardRetiresRecordedAllocations)
{
  auto destination = Backend().GetResourceRegistry().RegisterManagedTexture(
    Backend().CreateTexture({
      .width = 1U,
      .height = 1U,
      .format = Format::kRG32Float,
      .texture_type = TextureType::kTexture2D,
      .is_shader_resource = true,
      .initial_state = graphics::ResourceStates::kCommon,
    }));
  ASSERT_TRUE(destination);
  const auto lifetime = std::weak_ptr(destination->resource);
  const auto pixels = std::array<float, 2> { 1.0F, 0.0F };
  const auto source = upload::UploadTextureSourceView {
    .subresources = { upload::UploadTextureSourceSubresource {
      .bytes = std::as_bytes(std::span(pixels)),
      .row_pitch = sizeof(pixels), .slice_pitch = sizeof(pixels),
    }, },
  };
  {
    auto recording = Backend().AcquireCommandRecorder(QueueKeyFor(),
      "Discard immutable LUT", graphics::SubmissionPolicy::kExplicit);
    ASSERT_TRUE(recording);
    {
      auto prepared
        = renderer_->GetUploadCoordinator().PrepareImmutableTexture2D(
          std::move(*destination), source);
      ASSERT_TRUE(prepared);
      ASSERT_TRUE(prepared->Record(*recording));
    }
    EXPECT_FALSE(lifetime.expired());
    recording.Discard();
  }
  Backend().PollCompletedUses();
  EXPECT_TRUE(lifetime.expired());
}

NOLINT_TEST_F(ExposureGpuTest,
  ImmutableEnergyLutRejectsPublicationUntilSuccessfulSubmission)
{
  using graphics::internal::SubmissionFailurePoint;
  using graphics::internal::SubmissionFaultTestAccess;
  const auto queue = Backend().GetCommandQueue(graphics::QueueRole::kGraphics);
  ASSERT_NE(queue, nullptr);
  for (const auto point : {
         SubmissionFailurePoint::kBeforeIssue,
         SubmissionFailurePoint::kAfterFirstList,
         SubmissionFailurePoint::kAfterIssueBeforeMarker,
       }) {
    auto service = LightingService(*renderer_);
    SubmissionFaultTestAccess::FailNext(*queue, point);
    const auto failed = PrepareEnergyPublication(service);
    ASSERT_FALSE(failed);
    EXPECT_EQ(service.InspectForwardLightBindings(ViewId { 1U }), nullptr);
    if (point != SubmissionFailurePoint::kBeforeIssue) {
      ASSERT_TRUE(Backend().RecoverSubmissionFault());
    }
    ASSERT_TRUE(PrepareEnergyPublication(service));
    const auto* published = service.InspectForwardLightBindings(ViewId { 1U });
    ASSERT_NE(published, nullptr);
    EXPECT_TRUE(published->brdf_energy_srv.IsValid());
    WaitForQueueIdle();
  }
}

} // namespace oxygen::vortex::testing::exposure
