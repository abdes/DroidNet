//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <unordered_map>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Internal/ResourceStateTracker.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Command.h>
#include <Oxygen/Graphics/Headless/CommandList.h>
#include <Oxygen/Graphics/Headless/CommandQueue.h>
#include <Oxygen/Graphics/Headless/CommandRecorder.h>
#include <Oxygen/Graphics/Headless/Commands/BufferToTextureCommand.h>
#include <Oxygen/Graphics/Headless/Commands/ClearDepthStencilCommand.h>
#include <Oxygen/Graphics/Headless/Commands/ClearFramebufferCommand.h>
#include <Oxygen/Graphics/Headless/Commands/CopyBufferCommand.h>
#include <Oxygen/Graphics/Headless/Commands/QueueSignalCommand.h>
#include <Oxygen/Graphics/Headless/Commands/QueueWaitCommand.h>
#include <Oxygen/Graphics/Headless/Commands/ResourceBarrierCommand.h>
#include <Oxygen/Graphics/Headless/api_export.h>

namespace oxygen::graphics::headless {
// Out-of-line destructor to ensure vtable emission in this translation unit.
CommandRecorder::~CommandRecorder() = default;

// Basic barrier execution: log the barrier and perform simple validation.
// The heavy lifting of tracking is done by the ResourceStateTracker in the
// common CommandRecorder; here we only simulate execution and assert on
// obviously invalid uses.

auto CommandRecorder::ExecuteBarriers(std::span<const detail::Barrier> barriers)
  -> void
{
  if (barriers.empty()) {
    return;
  }

  // Package the pending barriers into an in-stream command. The command will
  // be executed by the CommandQueue worker and is responsible for updating
  // the execution-time observed state (CommandContext). The recorder must not
  // perform execution-time validation here.
  std::vector<::oxygen::graphics::detail::Barrier> v;
  v.reserve(barriers.size());
  for (const auto& b : barriers) {
    v.push_back(b);
  }

  LOG_F(INFO,
    "CommandRecorder: packaging {} barriers into ResourceBarrierCommand",
    v.size());

  QueueCommand(std::make_shared<ResourceBarrierCommand>(std::move(v)));
}

inline auto CommandRecorder::QueueCommand(std::shared_ptr<Command> cmd) -> void
{
  DCHECK_NOTNULL_F(cmd);
  auto& cmd_list = static_cast<CommandList&>(GetCommandList());
  cmd_list.QueueCommand(std::move(cmd));
}

auto CommandRecorder::CopyBuffer(graphics::Buffer& dst, size_t dst_offset,
  const graphics::Buffer& src, size_t src_offset, size_t size) -> void
{
  QueueCommand(std::make_shared<CopyBufferCommand>(
    &dst, dst_offset, &src, src_offset, size));
}

auto CommandRecorder::ClearFramebuffer(const Framebuffer& fb,
  std::optional<std::vector<std::optional<Color>>> color_clear_values,
  std::optional<float> depth_clear_value,
  std::optional<uint8_t> stencil_clear_value) -> void
{
  QueueCommand(std::make_shared<ClearFramebufferCommand>(&fb,
    std::move(color_clear_values), depth_clear_value, stencil_clear_value));
}

auto CommandRecorder::ClearDepthStencilView(const graphics::Texture& texture,
  const NativeObject& dsv, ClearFlags flags, float depth, uint8_t stencil)
  -> void
{
  QueueCommand(std::make_shared<ClearDepthStencilCommand>(
    &texture, dsv, flags, depth, stencil));
}

auto CommandRecorder::CopyBufferToTexture(const graphics::Buffer& src,
  const TextureUploadRegion& region, graphics::Texture& dst) -> void
{
  QueueCommand(std::make_shared<BufferToTextureCommand>(&src, region, &dst));
}

auto CommandRecorder::CopyBufferToTexture(const graphics::Buffer& src,
  std::span<const TextureUploadRegion> regions, graphics::Texture& dst) -> void
{
  for (const auto& r : regions) {
    QueueCommand(std::make_shared<BufferToTextureCommand>(&src, r, &dst));
  }
}

auto CommandRecorder::PerformCopy(graphics::Buffer& dst, size_t dst_offset,
  const graphics::Buffer& src, size_t src_offset, size_t size) -> void
{
  auto dst_h = static_cast<Buffer*>(&dst);
  auto src_h = static_cast<const Buffer*>(&src);
  if (dst_h == nullptr || src_h == nullptr) {
    LOG_F(WARNING,
      "Headless PerformCopy: one or both buffers are not headless-backed");
    return;
  }

  std::vector<std::uint8_t> temp(size);
  src_h->ReadBacking(temp.data(), src_offset, size);
  dst_h->WriteBacking(temp.data(), dst_offset, size);
}

auto CommandRecorder::RecordQueueSignal(uint64_t value) -> void
{
  auto queue = GetTargetQueue();
  DCHECK_NOTNULL_F(queue);
  QueueCommand(std::make_shared<QueueSignalCommand>(queue, value));
}

auto CommandRecorder::RecordQueueWait(uint64_t value) -> void
{
  auto queue = GetTargetQueue();
  DCHECK_NOTNULL_F(queue);
  QueueCommand(std::make_shared<QueueWaitCommand>(queue, value));
}

} // namespace oxygen::graphics::headless
