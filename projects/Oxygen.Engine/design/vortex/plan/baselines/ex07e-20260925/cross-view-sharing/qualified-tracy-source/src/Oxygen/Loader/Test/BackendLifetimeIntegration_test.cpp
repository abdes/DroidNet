//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstring>
#include <thread>

#include <Oxygen/Config/GraphicsConfig.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Graphics/Common/AllocationBudget.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Internal/SubmissionFaultTestAccess.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Loader/GraphicsBackendLoader.h>
#include <Oxygen/Testing/GTest.h>

namespace {
using namespace oxygen;
using namespace oxygen::graphics;

class BackendLifetimeIntegration : public testing::TestWithParam<BackendType> {
protected:
  auto TearDown() -> void override
  {
    GraphicsBackendLoader::GetInstance().UnloadBackend();
  }
};

NOLINT_TEST_P(BackendLifetimeIntegration,
  NativeOwnersOutliveClosedFacadeAndWeakObserversAllowReload)
{
  auto& loader = GraphicsBackendLoader::GetInstance();
  GraphicsConfig config;
  config.headless = true;
  config.enable_debug_layer = true;
  auto graphics = loader.LoadBackend(GetParam(), config, {}).lock();
  ASSERT_TRUE(graphics);
  EXPECT_FALSE(graphics->IsRunning());
  graphics->CreateCommandQueues(SingleQueueStrategy());
  auto budget = std::make_shared<AllocationBudget>(
    AllocationBudgetLimits { .total = SizeBytes { 16U * 1024U * 1024U },
      .compact_indices = SizeBytes { 0 },
      .driver_headroom = SizeBytes { 0 } });
  auto desc = TextureDesc {};
  desc.width = desc.height = 32;
  desc.format = Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.is_shader_resource = true;
  desc.debug_name = "Lifetime retained texture";
  desc.allocation_budget.owner = budget;
  auto texture = graphics->CreateTexture(desc);
  ASSERT_TRUE(texture);
  EXPECT_EQ(texture->shared_from_this(), texture);
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096,
    .debug_name = "Lifetime retained buffer",
    .allocation_budget = { .owner = budget } });
  auto& registry = graphics->GetResourceRegistry();
  registry.Register(texture);
  registry.Register(buffer);
  auto descriptor = graphics->GetDescriptorAllocator().AllocateBindless(
    bindless::generated::kTexturesDomain, ResourceViewType::kTexture_SRV);
  ASSERT_TRUE(descriptor.IsValid());
  const auto view = registry.RegisterView(*texture, std::move(descriptor),
    TextureViewDescription {
      .format = desc.format, .dimension = desc.texture_type });
  EXPECT_TRUE(view->IsValid());

  {
    auto submitted = graphics->AcquireCommandRecorder(
      graphics->QueueKeyFor(QueueRole::kGraphics), "Lifetime submitted",
      SubmissionPolicy::kExplicit);
    ASSERT_TRUE(submitted.Submit());
  }
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Lifetime unresolved",
    SubmissionPolicy::kExplicit);
  const auto first_id = graphics->GetBackendLifetime()->Id();
  const std::weak_ptr<Graphics> weak_graphics = graphics;
  const std::weak_ptr<Texture> weak_texture = texture;
  const std::weak_ptr<Buffer> weak_buffer = buffer;
  const auto charged = budget->Snapshot().allocated;
  if (GetParam() == BackendType::kDirect3D12) {
    EXPECT_GT(charged.get(), 0U);
  }
  loader.UnloadBackend();
  graphics->Close(); // Repeated close must not begin a second drain.
  EXPECT_EQ(registry.GetRegisteredResourceCount(), 0U);
  EXPECT_THROW((void)graphics->CreateTexture(desc), std::logic_error);
  graphics.reset();
  EXPECT_FALSE(weak_graphics.expired()); // unresolved recording owns the facade
  EXPECT_FALSE(recording.Submit()); // its own thread ends/discards the list
  recording = CommandRecording {};
  EXPECT_TRUE(weak_graphics.expired());
  EXPECT_EQ(budget->Snapshot().allocated, charged);
  EXPECT_THROW((void)loader.LoadBackend(GetParam(), config, {}),
    loader::InvalidOperationError);
  EXPECT_TRUE(texture->GetNativeResource()->IsValid());
  texture.reset();
  EXPECT_TRUE(weak_texture.expired());
  EXPECT_THROW((void)loader.LoadBackend(GetParam(), config, {}),
    loader::InvalidOperationError);
  buffer.reset();
  EXPECT_TRUE(weak_buffer.expired());
  EXPECT_EQ(budget->Snapshot().allocated.get(), 0U);
  auto next = loader.LoadBackend(GetParam(), config, {}).lock();
  ASSERT_TRUE(next);
  EXPECT_NE(next->GetBackendLifetime()->Id(), first_id);
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, ManagedCubeViewsSurviveCloseWithInternalUseOnly)
{
  auto& loader = GraphicsBackendLoader::GetInstance();
  GraphicsConfig config;
  config.headless = true;
  config.enable_debug_layer = true;
  auto graphics = loader.LoadBackend(GetParam(), config, {}).lock();
  ASSERT_TRUE(graphics);
  auto budget = std::make_shared<AllocationBudget>(
    AllocationBudgetLimits { .total = SizeBytes { 16U * 1024U * 1024U },
      .compact_indices = SizeBytes { 0 },
      .driver_headroom = SizeBytes { 0 } });
  TextureDesc desc;
  desc.width = desc.height = 32;
  desc.array_size = 6;
  desc.format = Format::kDepth32;
  desc.texture_type = TextureType::kTextureCubeArray;
  desc.is_shader_resource = desc.is_render_target = desc.is_typeless = true;
  desc.use_clear_value = true;
  desc.clear_value = Color { 0, 0, 0, 0 };
  desc.initial_state = ResourceStates::kDepthWrite;
  desc.allocation_budget.owner = budget;
  auto texture = graphics->CreateTexture(desc);
  auto& registry = graphics->GetResourceRegistry();
  auto lease = registry.RegisterManaged(texture);
  ASSERT_TRUE(lease);
  const auto id = lease->Identity();
  const TextureViewDescription srv_desc { .format = Format::kR32Float,
    .dimension = TextureType::kTextureCubeArray };
  const auto srv = registry.AcquireManagedView<Texture>(*lease, srv_desc);
  ASSERT_TRUE(srv);
  EXPECT_TRUE(srv->shader_visible_index.IsValid());
  EXPECT_EQ(registry.AcquireManagedView<Texture>(*lease, srv_desc)
              ->shader_visible_index,
    srv->shader_visible_index);
  for (uint32_t layer = 0; layer < 6; ++layer) {
    const TextureViewDescription dsv_desc { .view_type
      = ResourceViewType::kTexture_DSV,
      .visibility = DescriptorVisibility::kCpuOnly,
      .format = Format::kDepth32,
      .dimension = TextureType::kTexture2DArray,
      .sub_resources = { .base_array_slice = layer, .num_array_slices = 1 } };
    const auto dsv = registry.AcquireManagedView<Texture>(*lease, dsv_desc);
    ASSERT_TRUE(dsv);
    EXPECT_TRUE(dsv->view->IsValid());
    EXPECT_EQ(
      registry.AcquireManagedView<Texture>(*lease, dsv_desc)->view, dsv->view);
  }
  auto use = registry.RetainUse(*lease);
  ASSERT_TRUE(use);
  const auto charged = budget->Snapshot().allocated;
  *lease = RegistrationLease {};
  EXPECT_EQ(registry.AcquireManaged(id).error(), RegistrationError::kClosed);
  const std::weak_ptr<Texture> weak_texture = texture;
  const std::weak_ptr<Graphics> weak_graphics = graphics;
  texture.reset();
  loader.UnloadBackend();
  graphics.reset();
  EXPECT_TRUE(weak_graphics.expired());
  EXPECT_FALSE(weak_texture.expired());
  EXPECT_EQ(budget->Snapshot().allocated, charged);
  EXPECT_THROW((void)loader.LoadBackend(GetParam(), config, {}),
    loader::InvalidOperationError);
  std::jthread cleanup(
    [pin = std::move(*use)]() mutable { pin = RegistrationUse {}; });
  cleanup.join();
  EXPECT_TRUE(weak_texture.expired());
  EXPECT_EQ(budget->Snapshot().allocated.get(), 0U);
  EXPECT_FALSE(loader.LoadBackend(GetParam(), config, {}).expired());
}

auto LoadSubmissionBackend(BackendType type) -> std::shared_ptr<Graphics>
{
  GraphicsConfig config;
  config.headless = true;
  config.enable_debug_layer = true;
  auto graphics
    = GraphicsBackendLoader::GetInstance().LoadBackend(type, config, {}).lock();
  graphics->CreateCommandQueues(SharedTransferQueueStrategy());
  return graphics;
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, ManagedSubmissionRetiresOutsideFrameLoop)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto& registry = graphics->GetResourceRegistry();
  auto lease = registry.RegisterManaged(buffer);
  ASSERT_TRUE(lease);
  const std::weak_ptr<Buffer> weak_buffer = buffer;
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Managed submission",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(recording->RetainRegistration(registry, *lease));
  ASSERT_TRUE(recording->RetainRegistration(registry, *lease)); // deduplicated
  auto result = recording.SubmitWithReceipt();
  ASSERT_EQ(result.outcome, SubmissionOutcome::kSubmitted);
  ASSERT_TRUE(result.receipt);
  *lease = {};
  buffer.reset();
  EXPECT_FALSE(weak_buffer.expired());
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  queue->Flush();
  graphics->PollCompletedUses();
  EXPECT_EQ(
    queue->QueryCompletion(*result.receipt), CompletionStatus::kComplete);
  EXPECT_TRUE(weak_buffer.expired());
  EXPECT_EQ(recording.SubmitWithReceipt().receipt, result.receipt);
  const std::weak_ptr<Graphics> weak_graphics = graphics;
  GraphicsBackendLoader::GetInstance().UnloadBackend();
  graphics.reset();
  EXPECT_TRUE(
    weak_graphics.expired()); // the resolved recording shell owns no backend
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, LateManagedSubmitIsIndependentOfFrameRetirement)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto& registry = graphics->GetResourceRegistry();
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto lease = registry.RegisterManaged(buffer);
  ASSERT_TRUE(lease);
  const std::weak_ptr<Buffer> weak = buffer;
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Late managed submission",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(recording->RetainRegistration(registry, *lease));
  const auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  const auto frame_marker = queue->SignalSubmittedWork();
  const auto result = recording.SubmitWithReceipt();
  ASSERT_TRUE(result.receipt);
  *lease = {};
  buffer.reset();
  queue->Wait(frame_marker);
  // Drain the frame reclaimer directly: it must have no managed-list
  // retirement.
  graphics->GetDeferredReclaimer().ProcessAllDeferredReleases();
  registry.PollManagedRetirements();
  EXPECT_FALSE(weak.expired());
  queue->Flush();
  graphics->PollCompletedUses();
  EXPECT_TRUE(weak.expired());
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, TwoQueuesRetainUsesAndWaitForProducerReceipt)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto& registry = graphics->GetResourceRegistry();
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto lease = registry.RegisterManaged(buffer);
  ASSERT_TRUE(lease);
  const std::weak_ptr<Buffer> weak = buffer;
  auto writer = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kTransfer), "Transfer producer",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(writer->RetainRegistration(registry, *lease));
  const auto produced = writer.SubmitWithReceipt();
  ASSERT_TRUE(produced.receipt);
  auto reader = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Graphics consumer",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(reader->RetainRegistration(registry, *lease));
  reader->RecordDependency(*produced.receipt);
  const auto consumed = reader.SubmitWithReceipt();
  ASSERT_TRUE(consumed.receipt);
  EXPECT_NE(produced.receipt->Queue(), consumed.receipt->Queue());
  auto producer = graphics->GetCommandQueue(QueueRole::kTransfer);
  auto consumer = graphics->GetCommandQueue(QueueRole::kGraphics);
  EXPECT_EQ(
    consumer->QueryCompletion(*produced.receipt), CompletionStatus::kInvalid);
  *lease = {};
  buffer.reset();
  producer->Flush();
  registry.PollManagedRetirements();
  EXPECT_FALSE(
    weak.expired()); // consumer has not collected its completion proof
  consumer->Flush();
  graphics->PollCompletedUses();
  EXPECT_TRUE(weak.expired());
}

NOLINT_TEST_P(BackendLifetimeIntegration, RejectsStaleReservedSignalBeforeIssue)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  const auto key = graphics->QueueKeyFor(QueueRole::kGraphics);
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  auto first = graphics->AcquireCommandRecorder(
    key, "Older reservation", SubmissionPolicy::kExplicit);
  first->RecordQueueSignal(queue->Signal());
  auto second = graphics->AcquireCommandRecorder(
    key, "Newer reservation", SubmissionPolicy::kExplicit);
  second->RecordQueueSignal(queue->Signal());
  ASSERT_TRUE(second.Submit());
  EXPECT_EQ(first.SubmitWithReceipt().outcome, SubmissionOutcome::kDiscarded);
  EXPECT_FALSE(graphics->GetBackendLifetime()->IsFaulted());
  graphics->Flush();
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  IssuedFailureQuarantinesAndReconcilesManualAndManagedStates)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto& registry = graphics->GetResourceRegistry();
  auto managed = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto manual = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto lease = registry.RegisterManaged(managed);
  ASSERT_TRUE(lease);
  registry.Register(manual);
  const auto key = graphics->QueueKeyFor(QueueRole::kGraphics);
  auto stale = graphics->AcquireCommandRecorder(
    key, "Pre-fault recording", SubmissionPolicy::kExplicit);
  auto failed = graphics->AcquireCommandRecorder(
    key, "Issued failure", SubmissionPolicy::kExplicit);
  ASSERT_TRUE(failed->RetainRegistration(registry, *lease));
  for (const auto& buffer : { managed, manual }) {
    failed->BeginTrackingResourceState(*buffer, ResourceStates::kCommon);
    failed->RequireResourceState(*buffer, ResourceStates::kCopyDest);
  }
  failed->FlushBarriers();
  SubmissionOutcome callback = SubmissionOutcome::kSubmitted;
  failed->OnSubmission([&](SubmissionOutcome outcome) { callback = outcome; });
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  internal::SubmissionFaultTestAccess::FailNext(
    *queue, internal::SubmissionFailurePoint::kAfterIssueBeforeMarker);
  const auto result = failed.SubmitWithReceipt();
  EXPECT_EQ(result.outcome, SubmissionOutcome::kExecutionUncertain);
  EXPECT_FALSE(result.receipt);
  EXPECT_EQ(callback, SubmissionOutcome::kExecutionUncertain);
  EXPECT_TRUE(graphics->GetBackendLifetime()->IsFaulted());
  EXPECT_THROW(
    graphics->BeginFrame(frame::SequenceNumber { 1 }, frame::Slot { 0 }),
    std::runtime_error);
  EXPECT_THROW(
    (void)graphics->AcquireCommandRecorder(key, "Faulted"), std::logic_error);
  ASSERT_TRUE(graphics->RecoverSubmissionFault());
  EXPECT_FALSE(graphics->GetBackendLifetime()->IsFaulted());
  EXPECT_EQ(graphics->TryGetKnownResourceState(manual->GetNativeResource()),
    ResourceStates::kCopyDest);
  EXPECT_EQ(graphics->TryGetKnownResourceState(managed->GetNativeResource()),
    ResourceStates::kCopyDest);
  EXPECT_EQ(registry.AcquireManaged(lease->Identity()).error(),
    RegistrationError::kClosed);
  EXPECT_EQ(stale.SubmitWithReceipt().outcome, SubmissionOutcome::kDiscarded);
  registry.UnRegisterResource(*manual);
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, PreIssueFailureDiscardsWithoutFaultingBackend)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  const auto key = graphics->QueueKeyFor(QueueRole::kGraphics);
  auto recording = graphics->AcquireCommandRecorder(
    key, "Before issue failure", SubmissionPolicy::kExplicit);
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  internal::SubmissionFaultTestAccess::FailNext(
    *queue, internal::SubmissionFailurePoint::kBeforeIssue);
  EXPECT_EQ(
    recording.SubmitWithReceipt().outcome, SubmissionOutcome::kDiscarded);
  EXPECT_FALSE(graphics->GetBackendLifetime()->IsFaulted());
  auto retry = graphics->AcquireCommandRecorder(
    key, "Retry", SubmissionPolicy::kExplicit);
  EXPECT_EQ(retry.SubmitWithReceipt().outcome, SubmissionOutcome::kSubmitted);
  graphics->Flush();
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, DeviceLossNeverReportsSuccessfulCompletion)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto& registry = graphics->GetResourceRegistry();
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto lease = registry.RegisterManaged(buffer);
  ASSERT_TRUE(lease);
  const std::weak_ptr<Buffer> weak = buffer;
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Device loss",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(recording->RetainRegistration(registry, *lease));
  const auto result = recording.SubmitWithReceipt();
  ASSERT_TRUE(result.receipt);
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  internal::SubmissionFaultTestAccess::LoseDevice(*queue);
  EXPECT_EQ(
    queue->QueryCompletion(*result.receipt), CompletionStatus::kDeviceLost);
  EXPECT_TRUE(graphics->GetBackendLifetime()->IsFaulted());
  *lease = {};
  buffer.reset();
  EXPECT_FALSE(graphics->RecoverSubmissionFault());
  EXPECT_EQ(
    graphics->GetBackendLifetime()->State(), BackendLifecycle::kRetiring);
  EXPECT_TRUE(weak.expired());
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, QueueReplacementRejectsOutstandingOwners)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  const auto key = graphics->QueueKeyFor(QueueRole::kGraphics);
  auto recording = graphics->AcquireCommandRecorder(
    key, "Outstanding", SubmissionPolicy::kExplicit);
  EXPECT_THROW(
    graphics->CreateCommandQueues(SingleQueueStrategy()), std::logic_error);
  recording.Discard();
  auto buffer = graphics->CreateBuffer(BufferDesc { .size_bytes = 4096 });
  auto lease = graphics->GetResourceRegistry().RegisterManaged(buffer);
  ASSERT_TRUE(lease);
  EXPECT_THROW(
    graphics->CreateCommandQueues(SingleQueueStrategy()), std::logic_error);
  *lease = {};
  buffer.reset();
  graphics->PollCompletedUses();
  EXPECT_NO_THROW(graphics->CreateCommandQueues(SingleQueueStrategy()));
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, RejectsWaitForSignalRecordedInTheSameList)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Impossible self wait",
    SubmissionPolicy::kExplicit);
  const auto value = queue->Signal();
  recording->RecordQueueSignal(value);
  recording->RecordQueueWait(value);
  EXPECT_EQ(
    recording.SubmitWithReceipt().outcome, SubmissionOutcome::kDiscarded);
  EXPECT_FALSE(graphics->GetBackendLifetime()->IsFaulted());
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, PartiallyIssuedArrayRetainsAllListsUntilDrain)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  std::array<std::shared_ptr<CommandList>, 2> lists;
  for (auto& list : lists) {
    list = graphics->AcquireCommandList(QueueRole::kGraphics, "Partial array");
    list->BindBackend(graphics->GetBackendLifetime(), queue->Identity());
    list->OnBeginRecording();
    list->OnEndRecording();
  }
  internal::SubmissionFaultTestAccess::FailNext(
    *queue, internal::SubmissionFailurePoint::kAfterFirstList);
  try {
    queue->Submit(lists);
    FAIL() << "Injected partial issue must fail";
  } catch (const SubmissionException& error) {
    EXPECT_EQ(error.Result().outcome, SubmissionOutcome::kExecutionUncertain);
  }
  for (const auto& list : lists) {
    EXPECT_EQ(list->GetState(), CommandList::State::kExecutionUncertain);
  }
  ASSERT_TRUE(graphics->RecoverSubmissionFault());
  for (const auto& list : lists) {
    EXPECT_TRUE(list->IsFree());
  }
}

NOLINT_TEST_P(
  BackendLifetimeIntegration, CrossQueueReceiptOrdersActualBufferContents)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  constexpr uint32_t expected = 0x7351A9B2;
  auto upload = graphics->CreateBuffer(
    BufferDesc { .size_bytes = 4, .memory = BufferMemory::kUpload });
  auto storage = graphics->CreateBuffer(BufferDesc { .size_bytes = 4 });
  auto readback = graphics->CreateBuffer(
    BufferDesc { .size_bytes = 4, .memory = BufferMemory::kReadBack });
  *static_cast<uint32_t*>(upload->Map()) = expected;
  upload->UnMap();
  auto& registry = graphics->GetResourceRegistry();
  auto lease = registry.RegisterManaged(storage);
  ASSERT_TRUE(lease);
  auto producer = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kTransfer), "Actual producer",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(producer->RetainRegistration(registry, *lease));
  producer->BeginTrackingResourceState(*upload, ResourceStates::kGenericRead);
  producer->BeginTrackingResourceState(*storage, ResourceStates::kCommon);
  producer->RequireResourceState(*storage, ResourceStates::kCopyDest);
  producer->FlushBarriers();
  producer->CopyBuffer(*storage, 0, *upload, 0, 4);
  producer->RequireResourceStateFinal(*storage, ResourceStates::kCommon);
  const auto written = producer.SubmitWithReceipt();
  ASSERT_TRUE(written.receipt);
  auto consumer = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Actual consumer",
    SubmissionPolicy::kExplicit);
  ASSERT_TRUE(consumer->RetainRegistration(registry, *lease));
  consumer->RecordDependency(*written.receipt);
  consumer->BeginTrackingResourceState(*storage, ResourceStates::kCommon);
  consumer->BeginTrackingResourceState(*readback, ResourceStates::kCopyDest);
  consumer->RequireResourceState(*storage, ResourceStates::kCopySource);
  consumer->FlushBarriers();
  consumer->CopyBuffer(*readback, 0, *storage, 0, 4);
  const auto read = consumer.SubmitWithReceipt();
  ASSERT_TRUE(read.receipt);
  graphics->GetCommandQueue(QueueRole::kGraphics)->Flush();
  EXPECT_EQ(*static_cast<const uint32_t*>(readback->Map()), expected);
  readback->UnMap();
  graphics->Flush();
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  LegacyClientCanSupplyItsOwnMonotonicCompletionValue)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics),
    "Client-managed legacy timeline", SubmissionPolicy::kExplicit);
  const auto value = queue->GetCurrentValue() + 7;
  recording->RecordQueueSignal(value);
  ASSERT_TRUE(recording.Submit());
  queue->Wait(value);
  EXPECT_GE(queue->GetCompletedValue(), value);
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  OtherQueueMarkerCannotCompleteAnUnsubmittedReadback)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto source = graphics->CreateBuffer(
    BufferDesc { .size_bytes = 4, .memory = BufferMemory::kUpload });
  auto readback = graphics->GetReadbackManager()->CreateBufferReadback(
    "Unsubmitted readback");
  ASSERT_TRUE(readback);
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Unsubmitted copy",
    SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  const auto ticket = readback->EnqueueCopy(*recording, *source);
  ASSERT_TRUE(ticket);
  const auto queue = graphics->GetCommandQueue(QueueRole::kGraphics);
  queue->Wait(queue->SignalSubmittedWork());
  ASSERT_TRUE(readback->IsReady().has_value());
  EXPECT_FALSE(*readback->IsReady());
  EXPECT_EQ(readback->MapNow().error(), ReadbackError::kWouldDeadlock);
  EXPECT_FALSE(recording.Submit()); // its earlier legacy value is stale
  EXPECT_EQ(readback->MapNow().error(), ReadbackError::kCancelled);
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  ReadbackFacadesKeepCanonicalOwnerUntilTheirCommonDeletersReturn)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto buffer_readback = graphics->GetReadbackManager()->CreateBufferReadback(
    "Retained readback facade");
  auto texture_readback = graphics->GetReadbackManager()->CreateTextureReadback(
    "Retained texture readback facade");
  const std::weak_ptr<Graphics> weak_graphics = graphics;
  const std::weak_ptr<GpuBufferReadback> weak_readback = buffer_readback;
  const auto before = graphics->GetBackendLifetime()->Id();
  GraphicsBackendLoader::GetInstance().UnloadBackend();
  graphics.reset();
  EXPECT_FALSE(weak_graphics.expired());
  buffer_readback.reset();
  EXPECT_TRUE(weak_readback.expired());
  EXPECT_FALSE(weak_graphics.expired());
  texture_readback.reset();
  EXPECT_TRUE(weak_graphics.expired());
  auto next = LoadSubmissionBackend(GetParam());
  EXPECT_NE(next->GetBackendLifetime()->Id(), before);
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  DroppedReadbackRetainsStagingUntilItsRecordingCompletes)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto source = graphics->CreateBuffer(
    BufferDesc { .size_bytes = 4, .memory = BufferMemory::kUpload });
  auto& registry = graphics->GetResourceRegistry();
  const auto before = registry.GetRegisteredResourceCount();
  auto readback
    = graphics->GetReadbackManager()->CreateBufferReadback("Dropped readback");
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Retained staging",
    SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  ASSERT_TRUE(readback->EnqueueCopy(*recording, *source));
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before + 1);
  readback.reset();
  graphics->PollCompletedUses();
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before + 1);
  ASSERT_TRUE(recording.Submit());
  graphics->Flush();
  EXPECT_EQ(registry.GetRegisteredResourceCount(), before);
}

NOLINT_TEST_P(BackendLifetimeIntegration,
  MappingGuardKeepsReadbackAndBackendAliveAcrossClose)
{
  auto graphics = LoadSubmissionBackend(GetParam());
  auto source = graphics->CreateBuffer(
    BufferDesc { .size_bytes = 4, .memory = BufferMemory::kUpload });
  constexpr uint32_t expected = 0xCB5124;
  *static_cast<uint32_t*>(source->Map()) = expected;
  source->UnMap();
  auto readback
    = graphics->GetReadbackManager()->CreateBufferReadback("Retained mapping");
  auto recording = graphics->AcquireCommandRecorder(
    graphics->QueueKeyFor(QueueRole::kGraphics), "Readback mapping",
    SubmissionPolicy::kExplicit);
  recording->BeginTrackingResourceState(*source, ResourceStates::kGenericRead);
  ASSERT_TRUE(readback->EnqueueCopy(*recording, *source));
  ASSERT_TRUE(recording.Submit());
  auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped);
  const std::weak_ptr<Graphics> weak_graphics = graphics;
  const std::weak_ptr<GpuBufferReadback> weak_readback = readback;
  readback.reset();
  source.reset();
  GraphicsBackendLoader::GetInstance().UnloadBackend();
  graphics.reset();
  EXPECT_FALSE(weak_graphics.expired());
  EXPECT_FALSE(weak_readback.expired());
  uint32_t actual = 0;
  std::memcpy(&actual, mapped->Bytes().data(), sizeof(actual));
  EXPECT_EQ(actual, expected);
  mapped = std::unexpected(ReadbackError::kNotReady);
  EXPECT_TRUE(weak_readback.expired());
  EXPECT_TRUE(weak_graphics.expired());
  EXPECT_TRUE(LoadSubmissionBackend(GetParam()));
}

INSTANTIATE_TEST_SUITE_P(NativeBackends, BackendLifetimeIntegration,
  testing::Values(BackendType::kHeadless, BackendType::kDirect3D12));
} // namespace
