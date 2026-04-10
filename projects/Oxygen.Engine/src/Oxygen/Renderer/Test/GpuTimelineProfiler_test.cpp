//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/GpuEventScope.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/PipelineState.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Surface.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/TimestampQueryProvider.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Internal/GpuTimelineProfiler.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::Graphics;
using oxygen::observer_ptr;
using oxygen::engine::internal::GpuTimelineFrame;
using oxygen::engine::internal::GpuTimelineProfiler;
using oxygen::engine::internal::GpuTimelineSink;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::DescriptorAllocator;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::NativeView;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueRole;
using oxygen::graphics::Surface;
using oxygen::graphics::Texture;
using oxygen::graphics::TimestampQueryProvider;

class NullDescriptorAllocator final : public DescriptorAllocator {
public:
  auto AllocateRaw(oxygen::graphics::ResourceViewType view_type,
    oxygen::graphics::DescriptorVisibility visibility)
    -> oxygen::graphics::DescriptorAllocationHandle override
  {
    return CreateRawDescriptorHandle(
      oxygen::bindless::HeapIndex { 0U }, view_type, visibility);
  }

  auto AllocateBindless(oxygen::bindless::DomainToken domain,
    oxygen::graphics::ResourceViewType view_type)
    -> oxygen::graphics::DescriptorAllocationHandle override
  {
    return CreateBindlessHandle(
      oxygen::graphics::DescriptorAllocationHandle::PackBindlessSlot(
        domain, 0U),
      domain, view_type);
  }

  auto Release(oxygen::graphics::DescriptorAllocationHandle& handle)
    -> void override
  {
    handle.Invalidate();
  }

  auto CopyDescriptor(const oxygen::graphics::DescriptorAllocationHandle&,
    const oxygen::graphics::DescriptorAllocationHandle&) -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(
    oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 1024U };
  }

  [[nodiscard]] auto GetDomainBaseIndex(oxygen::bindless::DomainToken) const
    -> oxygen::bindless::ShaderVisibleIndex override
  {
    return oxygen::bindless::ShaderVisibleIndex { 0U };
  }

  [[nodiscard]] auto ReserveRaw(oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility, oxygen::bindless::Count count)
    -> std::optional<oxygen::bindless::HeapIndex> override
  {
    if (count.get() == 0U) {
      return std::nullopt;
    }
    return oxygen::bindless::HeapIndex { 0U };
  }

  [[nodiscard]] auto Contains(
    const oxygen::graphics::DescriptorAllocationHandle& handle) const
    -> bool override
  {
    return handle.IsValid();
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    oxygen::graphics::ResourceViewType,
    oxygen::graphics::DescriptorVisibility) const
    -> oxygen::bindless::Count override
  {
    return oxygen::bindless::Count { 0U };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const oxygen::graphics::DescriptorAllocationHandle&) const noexcept
    -> oxygen::bindless::ShaderVisibleIndex override
  {
    return oxygen::kInvalidShaderVisibleIndex;
  }
};

class TestCommandList final : public CommandList {
public:
  explicit TestCommandList(std::string_view name)
    : CommandList(name, QueueRole::kGraphics)
  {
  }
};

class TestQueue final : public CommandQueue {
public:
  TestQueue()
    : CommandQueue("TestQueue")
  {
  }

  auto Signal(uint64_t value) const -> void override
  {
    current_ = value;
    completed_ = value;
  }

  auto Signal() const -> uint64_t override
  {
    ++current_;
    completed_ = current_;
    return current_;
  }

  auto Wait(uint64_t, std::chrono::milliseconds) const -> void override { }
  auto Wait(uint64_t) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override
  {
    return completed_;
  }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override
  {
    return current_;
  }

  auto TryGetTimestampFrequency(uint64_t& out_hz) const -> bool override
  {
    out_hz = frequency_hz_;
    return true;
  }

  auto Submit(std::shared_ptr<CommandList>) -> void override { }
  auto Submit(std::span<std::shared_ptr<CommandList>>) -> void override { }
  [[nodiscard]] auto GetQueueRole() const -> QueueRole override
  {
    return QueueRole::kGraphics;
  }

private:
  auto SignalImmediate(uint64_t value) const -> void override
  {
    current_ = value;
    completed_ = value;
  }

  mutable uint64_t current_ { 0U };
  mutable uint64_t completed_ { 0U };
  uint64_t frequency_hz_ { 1'000'000U };
};

class TestTimestampProvider final : public TimestampQueryProvider {
public:
  auto SetNextTick(const uint64_t next_tick) -> void { next_tick_ = next_tick; }

  auto EnsureCapacity(uint32_t required_query_count) -> bool override
  {
    ticks_.resize(required_query_count, 0U);
    return true;
  }

  [[nodiscard]] auto GetCapacity() const noexcept -> uint32_t override
  {
    return static_cast<uint32_t>(ticks_.size());
  }

  auto WriteTimestamp(CommandRecorder&, uint32_t query_slot) -> bool override
  {
    if (query_slot >= ticks_.size()) {
      return false;
    }
    ++write_count_;
    ticks_[query_slot] = next_tick_;
    next_tick_ += 100U;
    return true;
  }

  auto RecordResolve(CommandRecorder&, uint32_t used_query_slots)
    -> bool override
  {
    ++resolve_count_;
    last_resolved_query_count_ = used_query_slots;
    return true;
  }

  [[nodiscard]] auto GetResolvedTicks() const
    -> std::span<const uint64_t> override
  {
    return ticks_;
  }

  [[nodiscard]] auto WriteCount() const -> uint32_t { return write_count_; }
  [[nodiscard]] auto ResolveCount() const -> uint32_t { return resolve_count_; }
  [[nodiscard]] auto LastResolvedQueryCount() const -> uint32_t
  {
    return last_resolved_query_count_;
  }

private:
  std::vector<uint64_t> ticks_ {};
  uint64_t next_tick_ { 1000U };
  uint32_t write_count_ { 0U };
  uint32_t resolve_count_ { 0U };
  uint32_t last_resolved_query_count_ { 0U };
};

class TestRecorder final : public CommandRecorder {
public:
  TestRecorder(
    std::shared_ptr<CommandList> command_list, observer_ptr<CommandQueue> queue)
    : CommandRecorder(std::move(command_list), queue)
  {
  }

  auto SetPipelineState(oxygen::graphics::GraphicsPipelineDesc) -> void override
  {
  }
  auto SetPipelineState(oxygen::graphics::ComputePipelineDesc) -> void override
  {
  }
  auto SetGraphicsRootConstantBufferView(uint32_t, uint64_t) -> void override {
  }
  auto SetComputeRootConstantBufferView(uint32_t, uint64_t) -> void override { }
  auto SetGraphicsRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }
  auto SetComputeRoot32BitConstant(uint32_t, uint32_t, uint32_t)
    -> void override
  {
  }
  auto SetRenderTargets(std::span<NativeView>, std::optional<NativeView>)
    -> void override
  {
  }
  auto SetViewport(const oxygen::ViewPort&) -> void override { }
  auto SetScissors(const oxygen::Scissors&) -> void override { }
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }
  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto ExecuteIndirect(const Buffer&, const IndirectCommandDesc&,
    const IndirectExecutionDesc&) -> void override
  {
  }
  auto SetVertexBuffers(uint32_t, const std::shared_ptr<Buffer>*,
    const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer&, oxygen::Format) -> void override { }
  auto BindFrameBuffer(const Framebuffer&) -> void override { }
  auto ClearDepthStencilView(const Texture&, const NativeView&,
    oxygen::graphics::ClearFlags, float, uint8_t) -> void override
  {
  }
  auto ClearDepthStencilView(const Texture&, const NativeView&,
    oxygen::graphics::ClearFlags, float, uint8_t,
    std::span<const oxygen::Scissors>) -> void override
  {
  }
  auto ClearFramebuffer(const Framebuffer&,
    std::optional<std::vector<std::optional<oxygen::graphics::Color>>>,
    std::optional<float>, std::optional<uint8_t>) -> void override
  {
  }
  auto CopyBuffer(Buffer&, std::size_t, const Buffer&, std::size_t, std::size_t)
    -> void override
  {
  }
  auto CopyBufferToTexture(const Buffer&,
    const oxygen::graphics::TextureUploadRegion&, Texture&) -> void override
  {
  }
  auto CopyBufferToTexture(const Buffer&,
    std::span<const oxygen::graphics::TextureUploadRegion>, Texture&)
    -> void override
  {
  }
  auto CopyTextureToBuffer(Buffer&, const Texture&,
    const oxygen::graphics::TextureBufferCopyRegion&) -> void override
  {
  }
  auto CopyTexture(const Texture&, const oxygen::graphics::TextureSlice&,
    const oxygen::graphics::TextureSubResourceSet&, Texture&,
    const oxygen::graphics::TextureSlice&,
    const oxygen::graphics::TextureSubResourceSet&) -> void override
  {
  }

protected:
  auto ExecuteBarriers(std::span<const oxygen::graphics::detail::Barrier>)
    -> void override
  {
  }
};

class TestGraphics final : public Graphics {
public:
  TestGraphics()
    : Graphics("TestGraphics")
    , queue_(std::make_shared<TestQueue>())
  {
  }

  [[nodiscard]] auto GetDescriptorAllocator() const
    -> const DescriptorAllocator& override
  {
    return descriptor_allocator_;
  }

  [[nodiscard]] auto GetTimestampQueryProvider() const
    -> observer_ptr<TimestampQueryProvider> override
  {
    return observer_ptr<TimestampQueryProvider>(
      const_cast<TestTimestampProvider*>(&provider_));
  }

  auto QueueKeyFor(QueueRole) const -> QueueKey override
  {
    return QueueKey { std::string("gfx") };
  }

  auto GetCommandQueue(const QueueKey&) const
    -> observer_ptr<CommandQueue> override
  {
    return observer_ptr<CommandQueue>(queue_.get());
  }

  auto GetCommandQueue(QueueRole) const -> observer_ptr<CommandQueue> override
  {
    return observer_ptr<CommandQueue>(queue_.get());
  }

  auto AcquireCommandRecorder(const QueueKey&, std::string_view name, bool)
    -> std::unique_ptr<CommandRecorder,
      std::function<void(CommandRecorder*)>> override
  {
    auto* recorder = new TestRecorder(std::make_shared<TestCommandList>(name),
      observer_ptr<CommandQueue>(queue_.get()));
    recorder->Begin();
    return { recorder, [](CommandRecorder* r) {
              if (r != nullptr) {
                static_cast<void>(r->End());
                delete r;
              }
            } };
  }

  [[nodiscard]] auto Provider() const -> const TestTimestampProvider&
  {
    return provider_;
  }

  [[nodiscard]] auto Provider() -> TestTimestampProvider& { return provider_; }

private:
  [[nodiscard]] auto CreateSurface(std::weak_ptr<oxygen::platform::Window>,
    observer_ptr<CommandQueue>) const -> std::unique_ptr<Surface> override
  {
    return {};
  }
  [[nodiscard]] auto CreateSurfaceFromNative(void*,
    observer_ptr<CommandQueue>) const -> std::shared_ptr<Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(const oxygen::graphics::ShaderRequest&) const
    -> std::shared_ptr<oxygen::graphics::IShaderByteCode> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTexture(const oxygen::graphics::TextureDesc&) const
    -> std::shared_ptr<Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(
    const oxygen::graphics::TextureDesc&,
    const oxygen::graphics::NativeResource&) const
    -> std::shared_ptr<Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateBuffer(const oxygen::graphics::BufferDesc&) const
    -> std::shared_ptr<Buffer> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandQueue(const QueueKey&, QueueRole)
    -> std::shared_ptr<CommandQueue> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandListImpl(QueueRole, std::string_view)
    -> std::unique_ptr<CommandList> override
  {
    return {};
  }
  [[nodiscard]] auto CreateCommandRecorder(std::shared_ptr<CommandList>,
    observer_ptr<CommandQueue>) -> std::unique_ptr<CommandRecorder> override
  {
    return {};
  }

  std::shared_ptr<TestQueue> queue_;
  mutable NullDescriptorAllocator descriptor_allocator_ {};
  mutable TestTimestampProvider provider_ {};
};

class CapturingSink final : public GpuTimelineSink {
public:
  auto ConsumeFrame(const GpuTimelineFrame& frame) -> bool override
  {
    frames.push_back(frame);
    return true;
  }

  std::vector<GpuTimelineFrame> frames;
};

auto WaitForFile(const std::filesystem::path& path) -> bool
{
  for (int i = 0; i < 50; ++i) {
    if (std::filesystem::exists(path)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

NOLINT_TEST(GpuTimelineProfilerTest, DisabledFastPathProducesNoWritesOrResolve)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });

  profiler.SetEnabled(false);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 1U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "DisabledFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "DisabledScope", profiler.MakeScopeOptions());
  }

  profiler.OnFrameRecordTailResolve();

  EXPECT_EQ(graphics.Provider().WriteCount(), 0U);
  EXPECT_EQ(graphics.Provider().ResolveCount(), 0U);
}

NOLINT_TEST(GpuTimelineProfilerTest, PublishesNestedTimelineOnNextFrame)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 1U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "FrameOne", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", profiler.MakeScopeOptions());
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", profiler.MakeScopeOptions());
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 2U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.frame_sequence, 1U);
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_EQ(frame.used_query_slots, 4U);
  EXPECT_TRUE(frame.scopes[0].valid);
  EXPECT_TRUE(frame.scopes[1].valid);
  EXPECT_EQ(frame.scopes[1].parent_scope_id, 0U);
  EXPECT_EQ(frame.scopes[0].child_scope_ids.size(), 1U);
  EXPECT_EQ(frame.scopes[0].child_scope_ids.front(), 1U);
  EXPECT_GT(frame.scopes[0].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].duration_ms, 0.0F);
  EXPECT_EQ(graphics.Provider().ResolveCount(), 1U);
  EXPECT_EQ(graphics.Provider().LastResolvedQueryCount(), 4U);
}

NOLINT_TEST(
  GpuTimelineProfilerTest, RetainedLatestFramePublishesWithoutExternalSink)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });

  profiler.SetEnabled(true);
  profiler.SetRetainLatestFrame(true);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 31U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "RetainedFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Retained", profiler.MakeScopeOptions());
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 32U });

  const auto frame = profiler.GetLastPublishedFrame();
  ASSERT_TRUE(frame.has_value());
  EXPECT_EQ(frame->frame_sequence, 31U);
  ASSERT_EQ(frame->scopes.size(), 1U);
  EXPECT_EQ(frame->scopes.front().display_name, "Retained");
  EXPECT_TRUE(frame->scopes.front().valid);
}

NOLINT_TEST(
  GpuTimelineProfilerTest, LargeAbsoluteTicksStillProduceNonZeroDurations)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  auto sink = std::make_shared<CapturingSink>();

  graphics.Provider().SetNextTick(1'000'000'000'000U);
  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(8U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 41U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "LargeAbsoluteTicks", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope outer(
      *recorder, "Outer", profiler.MakeScopeOptions());
    {
      oxygen::graphics::GpuEventScope inner(
        *recorder, "Inner", profiler.MakeScopeOptions());
    }
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 42U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 2U);
  EXPECT_TRUE(frame.scopes[0].valid);
  EXPECT_TRUE(frame.scopes[1].valid);
  EXPECT_GT(frame.scopes[0].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].duration_ms, 0.0F);
  EXPECT_GT(frame.scopes[1].start_ms, 0.0F);
}

NOLINT_TEST(GpuTimelineProfilerTest, DeeplyNestedScopesPreserveParentChain)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(32U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 41U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "NestedFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  std::vector<std::unique_ptr<oxygen::graphics::GpuEventScope>> scopes;
  scopes.reserve(8U);
  for (int i = 0; i < 8; ++i) {
    scopes.push_back(std::make_unique<oxygen::graphics::GpuEventScope>(
      *recorder, "Nested", profiler.MakeScopeOptions()));
  }
  scopes.clear();

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 42U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 8U);
  for (uint32_t i = 0; i < frame.scopes.size(); ++i) {
    EXPECT_TRUE(frame.scopes[i].valid);
    EXPECT_EQ(frame.scopes[i].depth, i);
    if (i == 0U) {
      EXPECT_EQ(frame.scopes[i].parent_scope_id, 0xFFFFFFFFU);
    } else {
      EXPECT_EQ(frame.scopes[i].parent_scope_id, i - 1U);
    }
  }
  EXPECT_EQ(graphics.Provider().LastResolvedQueryCount(), 16U);
}

NOLINT_TEST(
  GpuTimelineProfilerTest, OverflowStopsFurtherScopesAndPublishesDiagnostic)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.SetMaxScopesPerFrame(1U);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 7U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "OverflowFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope first(
      *recorder, "First", profiler.MakeScopeOptions());
  }
  {
    oxygen::graphics::GpuEventScope second(
      *recorder, "Second", profiler.MakeScopeOptions());
  }

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 8U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  EXPECT_TRUE(frame.overflowed);
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(
      testing::Field(&oxygen::engine::internal::GpuTimelineDiagnostic::code,
        "gpu.timestamp.overflow")));
}

NOLINT_TEST(GpuTimelineProfilerTest, IncompleteScopeIsMarkedInvalid)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  auto sink = std::make_shared<CapturingSink>();

  profiler.SetEnabled(true);
  profiler.AddSink(sink);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 12U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "IncompleteFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  const auto token
    = profiler.BeginScope(*recorder, "Leaked", profiler.MakeScopeOptions());
  EXPECT_NE(
    token.flags & oxygen::graphics::kGpuScopeTokenFlagTimestampEnabled, 0U);

  profiler.OnFrameRecordTailResolve();
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 13U });

  ASSERT_EQ(sink->frames.size(), 1U);
  const auto& frame = sink->frames.front();
  ASSERT_EQ(frame.scopes.size(), 1U);
  EXPECT_FALSE(frame.scopes.front().valid);
  EXPECT_THAT(frame.diagnostics,
    testing::Contains(
      testing::Field(&oxygen::engine::internal::GpuTimelineDiagnostic::code,
        "gpu.timestamp.incomplete_scope")));
}

NOLINT_TEST(GpuTimelineProfilerTest, OneShotExportWritesJsonFrame)
{
  TestGraphics graphics;
  GpuTimelineProfiler profiler(observer_ptr<Graphics> { &graphics });
  const auto export_path
    = std::filesystem::temp_directory_path() / "oxygen_gpu_timeline_test.json";
  std::filesystem::remove(export_path);

  profiler.SetEnabled(true);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 21U });

  auto recorder = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), "ExportFrame", true);
  recorder->SetProfileScopeHandler(
    observer_ptr<oxygen::graphics::IGpuProfileScopeHandler>(&profiler));

  {
    oxygen::graphics::GpuEventScope scope(
      *recorder, "Exported", profiler.MakeScopeOptions());
  }

  profiler.OnFrameRecordTailResolve();
  profiler.RequestOneShotExport(export_path);
  profiler.OnFrameStart(oxygen::frame::SequenceNumber { 22U });

  ASSERT_TRUE(WaitForFile(export_path));

  std::ifstream in(export_path);
  std::string text(
    (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();
  EXPECT_THAT(text, testing::HasSubstr("\"frame_seq\": 21"));
  EXPECT_THAT(text, testing::HasSubstr("\"name\": \"Exported\""));

  std::filesystem::remove(export_path);
}

} // namespace
