//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Scissors.h>
#include <Oxygen/Core/Types/ViewPort.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ClearFlags.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::observer_ptr;
using oxygen::graphics::Buffer;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::Framebuffer;
using oxygen::graphics::GpuProfileCollectorState;
using oxygen::graphics::GpuProfileScopeInfo;
using oxygen::graphics::IGpuProfileCollector;
using oxygen::graphics::NativeView;
using oxygen::graphics::QueueRole;
using oxygen::graphics::Texture;

constexpr uint8_t kCollectorActiveFlag = 1U << 0U;

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

  auto Signal(uint64_t value) const -> void override { current_ = completed_ = value; }
  [[nodiscard]] auto Signal() const -> uint64_t override { return ++current_; }
  auto Wait(uint64_t, std::chrono::milliseconds) const -> void override { }
  auto Wait(uint64_t) const -> void override { }
  [[nodiscard]] auto GetCompletedValue() const -> uint64_t override { return completed_; }
  [[nodiscard]] auto GetCurrentValue() const -> uint64_t override { return current_; }
  auto Submit(std::shared_ptr<CommandList>) -> void override { }
  auto Submit(std::span<std::shared_ptr<CommandList>>) -> void override { }
  [[nodiscard]] auto GetQueueRole() const -> QueueRole override { return QueueRole::kGraphics; }

protected:
  auto SignalImmediate(uint64_t value) const -> void override { current_ = completed_ = value; }

private:
  mutable uint64_t current_ { 0U };
  mutable uint64_t completed_ { 0U };
};

class TestRecorder final : public CommandRecorder {
public:
  TestRecorder()
    : CommandRecorder(
        std::make_shared<TestCommandList>("Recorder"), observer_ptr<CommandQueue>(&queue_))
  {
  }

  auto SetPipelineState(oxygen::graphics::GraphicsPipelineDesc) -> void override { }
  auto SetPipelineState(oxygen::graphics::ComputePipelineDesc) -> void override { }
  auto SetGraphicsRootConstantBufferView(uint32_t, uint64_t) -> void override { }
  auto SetComputeRootConstantBufferView(uint32_t, uint64_t) -> void override { }
  auto SetGraphicsRoot32BitConstant(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetComputeRoot32BitConstant(uint32_t, uint32_t, uint32_t) -> void override { }
  auto SetRenderTargets(std::span<NativeView>, std::optional<NativeView>) -> void override { }
  auto SetViewport(const oxygen::ViewPort&) -> void override { }
  auto SetScissors(const oxygen::Scissors&) -> void override { }
  auto Draw(uint32_t, uint32_t, uint32_t, uint32_t) -> void override { }
  auto Dispatch(uint32_t, uint32_t, uint32_t) -> void override { }
  auto ExecuteIndirect(const Buffer&, const IndirectCommandDesc&,
    const IndirectExecutionDesc&) -> void override
  {
  }
  auto SetVertexBuffers(
    uint32_t, const std::shared_ptr<Buffer>*, const uint32_t*) const -> void override
  {
  }
  auto BindIndexBuffer(const Buffer&, oxygen::Format) -> void override { }
  auto BindFrameBuffer(const Framebuffer&) -> void override { }
  auto ClearDepthStencilView(
    const Texture&, const NativeView&, oxygen::graphics::ClearFlags, float, uint8_t)
    -> void override
  {
  }
  auto ClearFramebuffer(const Framebuffer&,
    std::optional<std::vector<std::optional<oxygen::graphics::Color>>>,
    std::optional<float>, std::optional<uint8_t>) -> void override
  {
  }
  auto CopyBuffer(Buffer&, size_t, const Buffer&, size_t, size_t) -> void override { }
  auto CopyBufferToTexture(const Buffer&, const oxygen::graphics::TextureUploadRegion&,
    Texture&) -> void override
  {
  }
  auto CopyBufferToTexture(
    const Buffer&, std::span<const oxygen::graphics::TextureUploadRegion>, Texture&)
    -> void override
  {
  }
  auto CopyTextureToBuffer(
    Buffer&, const Texture&, const oxygen::graphics::TextureBufferCopyRegion&) -> void override
  {
  }
  auto CopyTexture(const Texture&, const oxygen::graphics::TextureSlice&,
    const oxygen::graphics::TextureSubResourceSet&, Texture&,
    const oxygen::graphics::TextureSlice&,
    const oxygen::graphics::TextureSubResourceSet&) -> void override
  {
  }

protected:
  auto ExecuteBarriers(std::span<const oxygen::graphics::detail::Barrier>) -> void override { }

private:
  TestQueue queue_ {};
};

class CountingCollector final : public IGpuProfileCollector {
public:
  int begin_count { 0 };
  int end_count { 0 };
  int abort_count { 0 };
  bool throw_after_activate { false };

  auto BeginScope(CommandRecorder&, const GpuProfileScopeInfo&,
    GpuProfileCollectorState& state) -> void override
  {
    ++begin_count;
    state.flags = kCollectorActiveFlag;
    if (throw_after_activate) {
      throw std::runtime_error("collector begin failure");
    }
  }

  auto EndScope(CommandRecorder&, GpuProfileCollectorState& state) -> void override
  {
    if ((state.flags & kCollectorActiveFlag) == 0U) {
      return;
    }
    ++end_count;
    state.flags = 0U;
  }

  auto AbortScope(CommandRecorder&, GpuProfileCollectorState& state) -> void override
  {
    if ((state.flags & kCollectorActiveFlag) == 0U) {
      return;
    }
    ++abort_count;
    state.flags = 0U;
  }
};

auto MakeScopeDesc() -> oxygen::profiling::GpuProfileScopeDesc
{
  return {
    .label = "Test.Scope",
    .granularity = oxygen::profiling::ProfileGranularity::kTelemetry,
  };
}

TEST(CommandRecorderProfileScopeDrain, EndAbortsOpenCollectors)
{
  auto recorder = TestRecorder {};
  auto telemetry = CountingCollector {};
  auto trace = CountingCollector {};
  recorder.SetTelemetryCollector(observer_ptr<IGpuProfileCollector>(&telemetry));
  recorder.SetTraceCollector(observer_ptr<IGpuProfileCollector>(&trace));

  recorder.Begin();
  const auto token = recorder.BeginProfileScope(MakeScopeDesc());
  EXPECT_NE(token.flags & oxygen::graphics::kGpuScopeTokenFlagActive, 0U);

  const auto list = recorder.End();
  ASSERT_NE(list, nullptr);
  EXPECT_EQ(telemetry.begin_count, 1);
  EXPECT_EQ(telemetry.abort_count, 1);
  EXPECT_EQ(telemetry.end_count, 0);
  EXPECT_EQ(trace.begin_count, 1);
  EXPECT_EQ(trace.abort_count, 1);
  EXPECT_EQ(trace.end_count, 0);
}

TEST(CommandRecorderProfileScopeDrain, DestructorAbortsOpenCollectors)
{
  auto telemetry = CountingCollector {};
  auto trace = CountingCollector {};

  {
    auto recorder = TestRecorder {};
    recorder.SetTelemetryCollector(observer_ptr<IGpuProfileCollector>(&telemetry));
    recorder.SetTraceCollector(observer_ptr<IGpuProfileCollector>(&trace));
    recorder.Begin();
    static_cast<void>(recorder.BeginProfileScope(MakeScopeDesc()));
  }

  EXPECT_EQ(telemetry.begin_count, 1);
  EXPECT_EQ(telemetry.abort_count, 1);
  EXPECT_EQ(telemetry.end_count, 0);
  EXPECT_EQ(trace.begin_count, 1);
  EXPECT_EQ(trace.abort_count, 1);
  EXPECT_EQ(trace.end_count, 0);
}

TEST(CommandRecorderProfileScopeDrain, ScopeUsesCollectorThatOpenedIt)
{
  auto recorder = TestRecorder {};
  auto first = CountingCollector {};
  auto second = CountingCollector {};
  recorder.SetTelemetryCollector(observer_ptr<IGpuProfileCollector>(&first));

  recorder.Begin();
  const auto token = recorder.BeginProfileScope(MakeScopeDesc());
  recorder.SetTelemetryCollector(observer_ptr<IGpuProfileCollector>(&second));
  recorder.EndProfileScope(token);
  const auto list = recorder.End();

  ASSERT_NE(list, nullptr);
  EXPECT_EQ(first.begin_count, 1);
  EXPECT_EQ(first.end_count, 1);
  EXPECT_EQ(first.abort_count, 0);
  EXPECT_EQ(second.begin_count, 0);
  EXPECT_EQ(second.end_count, 0);
  EXPECT_EQ(second.abort_count, 0);
}

TEST(CommandRecorderProfileScopeDrain, BeginFailureAbortsOpenedCollectors)
{
  auto recorder = TestRecorder {};
  auto telemetry = CountingCollector {};
  auto trace = CountingCollector {};
  trace.throw_after_activate = true;

  recorder.SetTelemetryCollector(observer_ptr<IGpuProfileCollector>(&telemetry));
  recorder.SetTraceCollector(observer_ptr<IGpuProfileCollector>(&trace));
  recorder.Begin();

  EXPECT_THROW(recorder.BeginProfileScope(MakeScopeDesc()), std::runtime_error);
  EXPECT_EQ(telemetry.begin_count, 1);
  EXPECT_EQ(telemetry.abort_count, 1);
  EXPECT_EQ(telemetry.end_count, 0);
  EXPECT_EQ(trace.begin_count, 1);
  EXPECT_EQ(trace.abort_count, 1);
  EXPECT_EQ(trace.end_count, 0);

  const auto list = recorder.End();
  ASSERT_NE(list, nullptr);
}

} // namespace
