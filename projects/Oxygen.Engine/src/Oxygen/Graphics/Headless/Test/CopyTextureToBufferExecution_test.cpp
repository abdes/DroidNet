//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <stdexcept>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Texture.h>

extern "C" auto GetGraphicsModuleApi() -> void*;

namespace {

using Role = oxygen::graphics::QueueRole;
using Alloc = oxygen::graphics::QueueAllocationPreference;
using Share = oxygen::graphics::QueueSharingPreference;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueSpecification;

class MultiNamedQueueStrategy final : public oxygen::graphics::QueuesStrategy {
public:
  [[nodiscard]] auto Specifications() const
    -> std::vector<QueueSpecification> override
  {
    return {
      {
        .key = QueueKey { "multi-gfx" },
        .role = Role::kGraphics,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
      {
        .key = QueueKey { "multi-cpu" },
        .role = Role::kCompute,
        .allocation_preference = Alloc::kDedicated,
        .sharing_preference = Share::kNamed,
      },
    };
  }

  [[nodiscard]] auto KeyFor(const Role role) const -> QueueKey override
  {
    switch (role) {
    case Role::kGraphics:
    case Role::kTransfer:
    case Role::kPresent:
      return QueueKey { "multi-gfx" };
    case Role::kCompute:
      return QueueKey { "multi-cpu" };
    case oxygen::graphics::QueueRole::kMax:;
    }
    return QueueKey { "__invalid__" };
  }

  [[nodiscard]] auto Clone() const -> std::unique_ptr<QueuesStrategy> override
  {
    return std::make_unique<MultiNamedQueueStrategy>(*this);
  }
};

class HeadlessExecutionHarness {
public:
  HeadlessExecutionHarness()
  {
    module_api_ = static_cast<oxygen::graphics::GraphicsModuleApi*>(
      GetGraphicsModuleApi());
    if (module_api_ == nullptr) {
      throw std::runtime_error("GetGraphicsModuleApi returned null");
    }

    oxygen::SerializedBackendConfig cfg { .json_data = "{}", .size = 2 };
    oxygen::SerializedPathFinderConfig path_cfg { .json_data = "{}",
      .size = 2 };
    backend_ = module_api_->CreateBackend(cfg, path_cfg);
    if (backend_ == nullptr) {
      throw std::runtime_error("CreateBackend returned null");
    }

    graphics_ = static_cast<oxygen::graphics::headless::Graphics*>(backend_);
    if (graphics_ == nullptr) {
      throw std::runtime_error("Backend is not a headless graphics device");
    }

    graphics_->CreateCommandQueues(queue_strategy_);
    queue_key_ = queue_strategy_.KeyFor(Role::kGraphics);
    queue_ = graphics_->GetCommandQueue(queue_key_);
    if (queue_ == nullptr) {
      throw std::runtime_error("Graphics queue was not created");
    }

    cmd_list_
      = graphics_->AcquireCommandList(queue_->GetQueueRole(), kCommandListName);
    if (cmd_list_ == nullptr) {
      throw std::runtime_error("Command list acquisition failed");
    }
  }

  ~HeadlessExecutionHarness()
  {
    cmd_list_.reset();
    if (module_api_ != nullptr) {
      module_api_->DestroyBackend();
    }
  }

  auto GetGraphics() const -> oxygen::graphics::headless::Graphics&
  {
    return *graphics_;
  }

  auto GetQueueKey() const -> const QueueKey& { return queue_key_; }

  auto NextCompletionValue() const -> uint64_t
  {
    return queue_->GetCurrentValue() + 1;
  }

  auto WaitForCompletion(const uint64_t value) const -> void
  {
    queue_->Wait(value);
    try {
      cmd_list_->OnExecuted();
    } catch (const std::exception&) {
    }
  }

private:
  static constexpr auto kCommandListName = "copy-texture-to-buffer";

  oxygen::graphics::GraphicsModuleApi* module_api_ { nullptr };
  void* backend_ { nullptr };
  oxygen::graphics::headless::Graphics* graphics_ { nullptr };
  MultiNamedQueueStrategy queue_strategy_ {};
  QueueKey queue_key_ {};
  oxygen::observer_ptr<oxygen::graphics::CommandQueue> queue_;
  std::shared_ptr<oxygen::graphics::CommandList> cmd_list_;
};

NOLINT_TEST(CopyTextureToBufferExecution,
  CopiesResolvedUncompressedVolumeIntoTightBufferLayout)
{
  HeadlessExecutionHarness harness;
  auto& graphics = harness.GetGraphics();

  oxygen::graphics::TextureDesc texture_desc {};
  texture_desc.texture_type = oxygen::TextureType::kTexture3D;
  texture_desc.width = 4;
  texture_desc.height = 2;
  texture_desc.depth = 3;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  auto texture = std::static_pointer_cast<oxygen::graphics::headless::Texture>(
    graphics.CreateTexture(texture_desc));
  ASSERT_NE(texture, nullptr);

  oxygen::graphics::BufferDesc buffer_desc {};
  buffer_desc.size_bytes = 128;
  auto buffer = std::static_pointer_cast<oxygen::graphics::headless::Buffer>(
    graphics.CreateBuffer(buffer_desc));
  ASSERT_NE(buffer, nullptr);

  std::vector<uint8_t> source(texture->GetBackingSize());
  for (auto index = 0u; index < source.size(); ++index) {
    source[index] = static_cast<uint8_t>(index + 1);
  }
  texture->WriteBacking(source.data(), 0, static_cast<uint32_t>(source.size()));

  std::vector<uint8_t> sentinel(
    static_cast<size_t>(buffer_desc.size_bytes), 0xCD);
  buffer->WriteBacking(sentinel.data(), 0, sentinel.size());

  const oxygen::graphics::TextureBufferCopyRegion region {
    .buffer_offset = oxygen::OffsetBytes { 5 },
    .texture_slice = {
      .x = 1,
      .y = 0,
      .z = 1,
      .width = 2,
      .height = 2,
      .depth = 2,
      .mip_level = 0,
      .array_slice = 0,
    },
  };

  const auto completion_value = harness.NextCompletionValue();
  {
    auto recorder = graphics.AcquireCommandRecorder(
      harness.GetQueueKey(), "copy-texture-to-buffer", true);
    ASSERT_NE(recorder, nullptr);
    recorder->CopyTextureToBuffer(*buffer, *texture, region);
    recorder->RecordQueueSignal(completion_value);
  }

  harness.WaitForCompletion(completion_value);

  std::vector<uint8_t> actual(static_cast<size_t>(buffer_desc.size_bytes));
  buffer->ReadBacking(actual.data(), 0, actual.size());

  const auto row_bytes = 2u * 4u;
  const auto slice_bytes = row_bytes * 2u;
  const auto texture_row_stride = 4u * 4u;
  const auto texture_depth_stride = texture_row_stride * 2u;
  std::vector<uint8_t> expected = sentinel;
  for (auto z = 0u; z < 2u; ++z) {
    for (auto y = 0u; y < 2u; ++y) {
      const auto source_offset
        = (1u + z) * texture_depth_stride + y * texture_row_stride + 1u * 4u;
      const auto buffer_offset = 5u + z * slice_bytes + y * row_bytes;
      std::copy_n(source.begin() + source_offset, row_bytes,
        expected.begin() + buffer_offset);
    }
  }

  EXPECT_EQ(actual, expected);
}

NOLINT_TEST(CopyTextureToBufferExecution,
  CopiesBlockCompressedRegionWithExplicitBufferRowPitch)
{
  HeadlessExecutionHarness harness;
  auto& graphics = harness.GetGraphics();

  oxygen::graphics::TextureDesc texture_desc {};
  texture_desc.width = 8;
  texture_desc.height = 8;
  texture_desc.format = oxygen::Format::kBC1UNorm;
  auto texture = std::static_pointer_cast<oxygen::graphics::headless::Texture>(
    graphics.CreateTexture(texture_desc));
  ASSERT_NE(texture, nullptr);

  oxygen::graphics::BufferDesc buffer_desc {};
  buffer_desc.size_bytes = 64;
  auto buffer = std::static_pointer_cast<oxygen::graphics::headless::Buffer>(
    graphics.CreateBuffer(buffer_desc));
  ASSERT_NE(buffer, nullptr);

  std::vector<uint8_t> blocks(texture->GetBackingSize());
  for (auto index = 0u; index < blocks.size(); ++index) {
    blocks[index] = static_cast<uint8_t>(0x40 + index);
  }
  texture->WriteBacking(blocks.data(), 0, static_cast<uint32_t>(blocks.size()));

  std::vector<uint8_t> sentinel(
    static_cast<size_t>(buffer_desc.size_bytes), 0xA5);
  buffer->WriteBacking(sentinel.data(), 0, sentinel.size());

  const oxygen::graphics::TextureBufferCopyRegion region {
    .buffer_offset = oxygen::OffsetBytes { 2 },
    .buffer_row_pitch = oxygen::SizeBytes { 12 },
    .texture_slice = {
      .x = 4,
      .y = 0,
      .z = 0,
      .width = 4,
      .height = 8,
      .depth = 1,
      .mip_level = 0,
      .array_slice = 0,
    },
  };

  const auto completion_value = harness.NextCompletionValue();
  {
    auto recorder = graphics.AcquireCommandRecorder(
      harness.GetQueueKey(), "copy-texture-to-buffer", true);
    ASSERT_NE(recorder, nullptr);
    recorder->CopyTextureToBuffer(*buffer, *texture, region);
    recorder->RecordQueueSignal(completion_value);
  }

  harness.WaitForCompletion(completion_value);

  std::vector<uint8_t> actual(static_cast<size_t>(buffer_desc.size_bytes));
  buffer->ReadBacking(actual.data(), 0, actual.size());

  std::vector<uint8_t> expected = sentinel;
  const auto block_bytes = 8u;
  const auto block_row_stride = 2u * block_bytes;
  for (auto row = 0u; row < 2u; ++row) {
    const auto source_offset = row * block_row_stride + block_bytes;
    const auto buffer_offset = 2u + row * 12u;
    std::copy_n(blocks.begin() + source_offset, block_bytes,
      expected.begin() + buffer_offset);
  }

  EXPECT_EQ(actual, expected);
}

} // namespace
