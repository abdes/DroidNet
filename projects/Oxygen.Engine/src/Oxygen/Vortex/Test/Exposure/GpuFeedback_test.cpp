//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>

#include <combaseapi.h>
#include <d3d12.h>
#include <winerror.h>
#include <wrl/client.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/ScopeGuard.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Core/Types/ResolvedView.h>
#include <Oxygen/Core/Types/View.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/CommandRecording.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/ReadbackErrors.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/CommandQueue.h>
#include <Oxygen/Graphics/Direct3D12/Graphics.h>
#include <Oxygen/Scene/Types/NodeHandle.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Vortex/Lighting/LightingService.h>
#include <Oxygen/Vortex/Lighting/Types/FrameLightingInputs.h>
#include <Oxygen/Vortex/Lighting/Types/LightGridBuildStatus.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureTestGraphics.h>
#include <Oxygen/Vortex/Types/FrameLightSelection.h>
#if defined(_MSC_VER) && defined(_DEBUG)
#  include <Oxygen/Graphics/Common/Test/HeapAllocationFailure.h>
#endif
#include <Oxygen/Vortex/Internal/GpuFeedback.h>
#include <Oxygen/Vortex/Test/Exposure/Fixtures/ExposureGpuFixture.h>

namespace oxygen::vortex::testing::exposure {
namespace {
  using graphics::QueueRole;
  using graphics::ResourceStates;
  using graphics::SubmissionPolicy;
  using oxygen::vortex::internal::FeedbackPayloadSize;
  using oxygen::vortex::internal::FeedbackReserveError;
  using oxygen::vortex::internal::GpuFeedbackPool;
  using oxygen::vortex::internal::GpuFeedbackReservation;
  using Payload = std::array<std::uint32_t, 4>;
  constexpr auto kPayload = Payload { 4U, 3U, 2U, 1U };

  auto NativeQueue(graphics::CommandQueue& queue)
    -> graphics::d3d12::CommandQueue&
  {
    // This fixture creates only D3D12 queues; engine RTTI is disabled.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<graphics::d3d12::CommandQueue&>(queue);
  }

} // namespace

NOLINT_TEST_F(ExposureGpuTest, FeedbackUnusedReservationsStayBoundedAndMoveOnce)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 2U, "Feedback capacity");
  auto first = pool.Reserve();
  auto second = pool.Reserve();
  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  const auto busy = pool.Reserve();
  ASSERT_FALSE(busy);
  EXPECT_EQ(busy.error(), FeedbackReserveError::kBusy);
  auto moved = std::move(*first);
  EXPECT_FALSE(static_cast<bool>(*first));
  moved.Reset();
  auto reused = pool.Reserve();
  ASSERT_TRUE(reused);
  EXPECT_EQ(pool.InspectStats().occupied, 2U);
  EXPECT_EQ(pool.InspectStats().created_readbacks, 2U);
}

NOLINT_TEST_F(ExposureGpuTest, FeedbackAbandonedRecordingWaitsForDiscard)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 1U, "Feedback abandoned recording");
  auto source = CreateRegisteredBuffer({
    .size_bytes = sizeof(Payload),
    .memory = graphics::BufferMemory::kUpload,
  });
  source->Update(kPayload.data(), sizeof(Payload), 0U);
  auto recording = AcquireRecorder(
    "Unsubmitted feedback", QueueRole::kGraphics, SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  auto feedback = pool.Reserve();
  ASSERT_TRUE(feedback);
  ASSERT_TRUE(
    feedback->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
  EXPECT_FALSE(feedback->Commit());
  feedback->Reset();
  for (unsigned attempt = 0U; attempt < 4U; ++attempt) {
    const auto busy = pool.Reserve();
    ASSERT_FALSE(busy);
    EXPECT_EQ(busy.error(), FeedbackReserveError::kBusy);
    EXPECT_EQ(pool.InspectStats().created_readbacks, 1U);
  }
  recording.Discard();
  EXPECT_TRUE(pool.Reserve());
}

NOLINT_TEST_F(ExposureGpuTest, FeedbackFailedEnqueueKeepsRecordingCapacity)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 1U, "Feedback enqueue failure");
  auto source = CreateRegisteredBuffer({
    .size_bytes = sizeof(Payload),
    .memory = graphics::BufferMemory::kUpload,
  });
  auto recording = AcquireRecorder("Invalid feedback source state",
    QueueRole::kGraphics, SubmissionPolicy::kExplicit);
  auto feedback = pool.Reserve();
  ASSERT_TRUE(feedback);
  // Readback has allocated and retained staging before rejecting untracked
  // input.
  EXPECT_FALSE(
    feedback->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
  feedback->Reset();
  const auto busy = pool.Reserve();
  ASSERT_FALSE(busy);
  EXPECT_EQ(busy.error(), FeedbackReserveError::kBusy);
  recording.Discard();
  EXPECT_TRUE(pool.Reserve());
}

NOLINT_TEST_F(ExposureGpuTest, FeedbackTypedPollingChecksSizeAndReusesStorage)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 1U, "Feedback typed payload");
  auto source = CreateRegisteredBuffer({
    .size_bytes = sizeof(Payload),
    .memory = graphics::BufferMemory::kUpload,
  });
  source->Update(kPayload.data(), sizeof(Payload), 0U);
  for (unsigned iteration = 0U; iteration < 3U; ++iteration) {
    auto feedback = pool.Reserve();
    ASSERT_TRUE(feedback);
    auto recording = AcquireRecorder(
      "Typed feedback", QueueRole::kGraphics, SubmissionPolicy::kExplicit);
    recording->BeginTrackingResourceState(
      *source, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      feedback->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
    ASSERT_TRUE(recording.Submit());
    ASSERT_TRUE(feedback->Commit());
    WaitForQueueIdle();
    const auto packet = feedback->Poll<Payload>();
    if (!packet || !packet->has_value()) {
      FAIL() << "Expected a complete feedback payload";
      return;
    }
    EXPECT_EQ(**packet, kPayload);
    const auto prefix
      = feedback->Poll<std::uint32_t>(FeedbackPayloadSize::kAtLeast);
    if (!prefix || !prefix->has_value()) {
      FAIL() << "Expected the payload prefix";
      return;
    }
    EXPECT_EQ(**prefix, kPayload.front());
    feedback->Reset();
  }
  EXPECT_EQ(pool.InspectStats().created_readbacks, 1U);
  auto invalid = pool.Reserve();
  ASSERT_TRUE(invalid);
  auto recording = AcquireRecorder("Wrong feedback payload size",
    QueueRole::kGraphics, SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  ASSERT_TRUE(
    invalid->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
  ASSERT_TRUE(recording.Submit());
  ASSERT_TRUE(invalid->Commit());
  WaitForQueueIdle();
  const auto wrong_size = invalid->Poll<std::uint32_t>();
  ASSERT_FALSE(wrong_size);
  EXPECT_EQ(wrong_size.error(), graphics::ReadbackError::kInvalidArgument);
  invalid->Reset();
  EXPECT_TRUE(pool.Reserve());
  EXPECT_EQ(pool.InspectStats().created_readbacks, 2U);
}

NOLINT_TEST_F(ExposureGpuTest, FeedbackAbandonedSubmissionWaitsForGpuCompletion)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 1U, "Feedback delayed GPU");
  auto source = CreateRegisteredBuffer({
    .size_bytes = sizeof(Payload),
    .memory = graphics::BufferMemory::kUpload,
  });
  source->Update(kPayload.data(), sizeof(Payload), 0U);
  Microsoft::WRL::ComPtr<ID3D12Fence> gate;
  ASSERT_TRUE(SUCCEEDED(Backend().GetCurrentDevice()->CreateFence(
    0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf()))));
  const ScopeGuard release_gate(
    [gate] noexcept -> void { static_cast<void>(gate->Signal(1U)); });
  auto& queue = NativeQueue(*GetQueue());
  ASSERT_TRUE(SUCCEEDED(queue.GetCommandQueue()->Wait(gate.Get(), 1U)));
  auto recording = AcquireRecorder(
    "Blocked feedback", QueueRole::kGraphics, SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  auto feedback = pool.Reserve();
  ASSERT_TRUE(feedback);
  ASSERT_TRUE(
    feedback->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
  ASSERT_TRUE(recording.Submit());
  ASSERT_TRUE(feedback->Commit());
  const auto pending = feedback->Poll<Payload>();
  ASSERT_TRUE(pending);
  EXPECT_FALSE(pending->has_value());
  feedback->Reset();
  const auto busy = pool.Reserve();
  ASSERT_FALSE(busy);
  EXPECT_EQ(busy.error(), FeedbackReserveError::kBusy);
  EXPECT_EQ(pool.InspectStats().created_readbacks, 1U);
  ASSERT_TRUE(SUCCEEDED(gate->Signal(1U)));
  WaitForQueueIdle();
  EXPECT_TRUE(pool.Reserve());
  EXPECT_EQ(pool.InspectStats().created_readbacks, 1U);
}

NOLINT_TEST_F(
  ExposureGpuTest, FeedbackReservationsOutliveFacadeAndPollIndependently)
{
  auto pool = std::make_unique<GpuFeedbackPool>(
    GetGraphicsShared(), 2U, "Feedback lifetime");
  std::array<std::shared_ptr<graphics::Buffer>, 2> sources;
  std::array<Payload, 2> expected { kPayload, kPayload };
  std::array<GpuFeedbackReservation, 2> requests;
  for (std::size_t index = 0; index < requests.size(); ++index) {
    auto& source = sources.at(index);
    source = CreateRegisteredBuffer({
      .size_bytes = sizeof(Payload),
      .memory = graphics::BufferMemory::kUpload,
    });
    expected.at(index).front() += static_cast<std::uint32_t>(index);
    source->Update(expected.at(index).data(), sizeof(Payload), 0U);
    auto reserved = pool->Reserve();
    ASSERT_TRUE(reserved);
    auto& request = requests.at(index);
    request = std::move(*reserved);
    auto recording = AcquireRecorder("Independent feedback",
      QueueRole::kGraphics, SubmissionPolicy::kExplicit);
    recording->BeginTrackingResourceState(
      *source, ResourceStates::kGenericRead);
    ASSERT_TRUE(
      request.EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
    ASSERT_TRUE(recording.Submit());
    ASSERT_TRUE(request.Commit());
  }
  pool.reset();
  WaitForQueueIdle();
  for (auto remaining = requests.size(); remaining != 0; --remaining) {
    const auto index = remaining - 1U;
    const auto packet = requests.at(index).Poll<Payload>();
    if (!packet || !packet->has_value()) {
      FAIL() << "Expected independently retained feedback";
      return;
    }
    EXPECT_EQ(**packet, expected.at(index));
  }
}

NOLINT_TEST_F(ExposureGpuTest, FeedbackGridFailurePreservesEarlierPendingCopy)
{
  auto service = LightingService(*renderer_);
  auto selection = FrameLightSelection {};
  selection.scene_generation = 1U;
  selection.selection_epoch = 1U;
  selection.local_lights.push_back({
    .source_node = scene::NodeHandle(1U, 1U),
    .range = 4.0F,
    .luminous_flux_lm = 1.0F,
  });
  auto parameters = ResolvedView::Params {};
  constexpr auto kViewExtent = 16.0F;
  parameters.view_config.viewport
    = ViewPort { .width = kViewExtent, .height = kViewExtent };
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
  const auto inputs = FrameLightingInputs {
    .frame_light_set = &selection,
    .active_views = views,
  };
  service.OnFrameStart(frame::SequenceNumber { 1U }, frame::Slot { 0U });
  ASSERT_TRUE(service.BuildLightGrid(inputs));
  WaitForQueueIdle();
  ASSERT_TRUE(service.InspectCompletedGrid(view));

  Microsoft::WRL::ComPtr<ID3D12Fence> gate;
  ASSERT_TRUE(SUCCEEDED(Backend().GetCurrentDevice()->CreateFence(
    0U, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gate.GetAddressOf()))));
  const ScopeGuard release_gate(
    [gate] noexcept -> void { static_cast<void>(gate->Signal(1U)); });
  ASSERT_TRUE(SUCCEEDED(
    NativeQueue(*GetQueue()).GetCommandQueue()->Wait(gate.Get(), 1U)));
  service.OnFrameStart(frame::SequenceNumber { 2U }, frame::Slot { 1U });
  selection.selection_epoch = 2U;
  ASSERT_TRUE(service.BuildLightGrid(inputs));

  // A second recording in this same frame must not own the first copy's state.
  selection.selection_epoch = 3U;
  FailureBackend().recorder_names.clear();
  graphics::internal::SubmissionFaultTestAccess::FailNext(
    *GetQueue(), graphics::internal::SubmissionFailurePoint::kBeforeIssue);
  EXPECT_FALSE(service.BuildLightGrid(inputs));
  ASSERT_FALSE(FailureBackend().recorder_names.empty());
  EXPECT_EQ(
    FailureBackend().recorder_names.back(), "LightingService.SpatialGrid");
  ASSERT_TRUE(SUCCEEDED(gate->Signal(1U)));
  WaitForQueueIdle();
  const auto completed = service.InspectCompletedGrid(view);
  if (!completed) {
    FAIL() << "The earlier accepted grid copy was abandoned";
    return;
  }
  EXPECT_EQ(completed->sequence, frame::SequenceNumber { 2U });
  EXPECT_EQ(completed->status.state, kLightGridBuildValid);
}

#if defined(_MSC_VER) && defined(_DEBUG)
NOLINT_TEST_F(ExposureGpuTest, FeedbackMapFailureDropsRequestAndRecovers)
{
  GpuFeedbackPool pool(GetGraphicsShared(), 1U, "Feedback map failure");
  auto source = CreateRegisteredBuffer({
    .size_bytes = sizeof(Payload),
    .memory = graphics::BufferMemory::kUpload,
  });
  source->Update(kPayload.data(), sizeof(Payload), 0U);
  auto feedback = pool.Reserve();
  ASSERT_TRUE(feedback);
  auto recording = AcquireRecorder("Feedback map allocation",
    QueueRole::kGraphics, SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  ASSERT_TRUE(
    feedback->EnqueueCopy(*recording, *source, { 0U, sizeof(Payload) }));
  ASSERT_TRUE(recording.Submit());
  ASSERT_TRUE(feedback->Commit());
  WaitForQueueIdle();
  const auto ready = feedback->IsReady();
  ASSERT_TRUE(ready);
  ASSERT_TRUE(*ready);
  auto packet = Result<std::optional<Payload>, graphics::ReadbackError>(
    Err(graphics::ReadbackError::kNotReady));
  {
    const graphics::testing::HeapAllocationFailure fail_mapping_guard;
    packet = feedback->Poll<Payload>();
  }
  ASSERT_FALSE(packet);
  EXPECT_EQ(packet.error(), graphics::ReadbackError::kBackendFailure);
  feedback->Reset();
  EXPECT_TRUE(pool.Reserve());
  EXPECT_EQ(pool.InspectStats().created_readbacks, 2U);
}
#endif
} // namespace oxygen::vortex::testing::exposure
