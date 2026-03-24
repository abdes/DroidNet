//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/BackendModule.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Headless/Buffer.h>
#include <Oxygen/Graphics/Headless/Graphics.h>
#include <Oxygen/Graphics/Headless/Texture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace {

using oxygen::OffsetBytes;
using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::co::Co;
using oxygen::co::testing::TestEventLoop;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::GpuBufferReadback;
using oxygen::graphics::GpuTextureReadback;
using oxygen::graphics::MappedBufferReadback;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::ResourceAccessMode;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::headless::Buffer;
using oxygen::graphics::headless::Graphics;
using oxygen::graphics::headless::Texture;

extern "C" auto GetGraphicsModuleApi() -> void*;

auto MakePatternBytes(const size_t size, const uint8_t seed = 0x20)
  -> std::vector<std::byte>
{
  std::vector<std::byte> bytes(size);
  for (size_t index = 0; index < size; ++index) {
    bytes[index] = static_cast<std::byte>(
      static_cast<uint8_t>(seed + static_cast<uint8_t>(index * 5U + 1U)));
  }
  return bytes;
}

auto SliceBytes(const std::vector<std::byte>& bytes, const size_t offset,
  const size_t size) -> std::vector<std::byte>
{
  return {
    bytes.begin() + static_cast<std::ptrdiff_t>(offset),
    bytes.begin() + static_cast<std::ptrdiff_t>(offset + size),
  };
}

auto CopyMappedBytes(const MappedBufferReadback& mapped)
  -> std::vector<std::byte>
{
  const auto bytes = mapped.Bytes();
  return { bytes.begin(), bytes.end() };
}

auto CopyTextureRegionBytes(const std::vector<std::byte>& source,
  const TextureDesc& desc, const TextureSlice& resolved_slice)
  -> std::vector<std::byte>
{
  const auto mip_width = (std::max)(1u, desc.width >> resolved_slice.mip_level);
  const auto bytes_per_pixel = 4u;
  const auto row_stride = mip_width * bytes_per_pixel;
  std::vector<std::byte> result(static_cast<size_t>(resolved_slice.width
    * resolved_slice.height * resolved_slice.depth * bytes_per_pixel));

  size_t dst_offset = 0;
  for (uint32_t z = 0; z < resolved_slice.depth; ++z) {
    for (uint32_t y = 0; y < resolved_slice.height; ++y) {
      const auto src_offset
        = static_cast<size_t>((resolved_slice.z + z) * row_stride
            * (std::max)(1u, desc.height >> resolved_slice.mip_level))
        + static_cast<size_t>(resolved_slice.y + y) * row_stride
        + static_cast<size_t>(resolved_slice.x) * bytes_per_pixel;
      std::copy_n(source.begin() + static_cast<std::ptrdiff_t>(src_offset),
        resolved_slice.width * bytes_per_pixel,
        result.begin() + static_cast<std::ptrdiff_t>(dst_offset));
      dst_offset += resolved_slice.width * bytes_per_pixel;
    }
  }
  return result;
}

class HeadlessReadbackTestBase : public testing::Test {
protected:
  void SetUp() override
  {
    module_api_ = static_cast<oxygen::graphics::GraphicsModuleApi*>(
      GetGraphicsModuleApi());
    ASSERT_NE(module_api_, nullptr);

    oxygen::SerializedBackendConfig cfg { .json_data = "{}", .size = 2 };
    oxygen::SerializedPathFinderConfig path_cfg { .json_data = "{}",
      .size = 2 };
    backend_ = module_api_->CreateBackend(cfg, path_cfg);
    ASSERT_NE(backend_, nullptr);

    graphics_ = static_cast<Graphics*>(backend_);
    ASSERT_NE(graphics_, nullptr);
    graphics_->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy {});
    readback_manager_ = graphics_->GetReadbackManager();
    ASSERT_NE(readback_manager_, nullptr);
  }

  void TearDown() override
  {
    if (graphics_ != nullptr) {
      graphics_->Flush();
      buffer_readbacks_.clear();
      texture_readbacks_.clear();
      textures_.clear();
      buffers_.clear();
      graphics_->Stop();
      graphics_ = nullptr;
    }
    if (module_api_ != nullptr) {
      module_api_->DestroyBackend();
      module_api_ = nullptr;
      backend_ = nullptr;
    }
  }

  auto GetReadbackManager() const
    -> oxygen::observer_ptr<oxygen::graphics::ReadbackManager>
  {
    return readback_manager_;
  }

  auto CreateBufferWithBytes(const std::vector<std::byte>& bytes,
    std::string_view debug_name) -> std::shared_ptr<Buffer>
  {
    auto buffer
      = std::static_pointer_cast<Buffer>(graphics_->CreateBuffer(BufferDesc {
        .size_bytes = bytes.size(),
        .usage = BufferUsage::kNone,
        .memory = BufferMemory::kDeviceLocal,
        .debug_name = std::string(debug_name),
      }));
    CHECK_NOTNULL_F(buffer.get());
    buffer->WriteBacking(bytes.data(), 0, bytes.size());
    buffers_.push_back(buffer);
    return buffer;
  }

  auto CreateTextureWithBytes(TextureDesc desc,
    const std::vector<std::byte>& bytes) -> std::shared_ptr<Texture>
  {
    desc.initial_state = ResourceStates::kCommon;
    auto texture
      = std::static_pointer_cast<Texture>(graphics_->CreateTexture(desc));
    CHECK_NOTNULL_F(texture.get());
    if (!bytes.empty()) {
      texture->WriteBacking(
        bytes.data(), 0, static_cast<uint32_t>(bytes.size()));
    }
    textures_.push_back(texture);
    return texture;
  }

  auto CreateTexture(TextureDesc desc) -> std::shared_ptr<Texture>
  {
    return CreateTextureWithBytes(desc, {});
  }

  auto CreateBufferReadback(std::string_view debug_name = "buffer-readback")
    -> std::shared_ptr<GpuBufferReadback>
  {
    auto readback = GetReadbackManager()->CreateBufferReadback(debug_name);
    CHECK_NOTNULL_F(readback.get());
    buffer_readbacks_.push_back(readback);
    return readback;
  }

  auto CreateTextureReadback(std::string_view debug_name = "texture-readback")
    -> std::shared_ptr<GpuTextureReadback>
  {
    auto readback = GetReadbackManager()->CreateTextureReadback(debug_name);
    CHECK_NOTNULL_F(readback.get());
    texture_readbacks_.push_back(readback);
    return readback;
  }

  auto AcquireRecorder(
    std::string_view command_list_name, const bool immediate_submission = true)
  {
    return graphics_->AcquireCommandRecorder(
      graphics_->QueueKeyFor(oxygen::graphics::QueueRole::kGraphics),
      command_list_name, immediate_submission);
  }

  auto WaitForQueueIdle() -> void
  {
    auto queue
      = graphics_->GetCommandQueue(oxygen::graphics::QueueRole::kGraphics);
    CHECK_NOTNULL_F(queue);
    queue->Flush();
  }

  auto EnqueueBufferReadback(std::shared_ptr<GpuBufferReadback> readback,
    const std::shared_ptr<Buffer>& source, const BufferRange range,
    std::string_view command_list_name, const bool immediate_submission = true)
    -> ReadbackTicket
  {
    auto recorder = AcquireRecorder(command_list_name, immediate_submission);
    CHECK_NOTNULL_F(recorder.get());
    recorder->BeginTrackingResourceState(
      *source, ResourceStates::kCommon, true);

    const auto ticket = readback->EnqueueCopy(*recorder, *source, range);
    CHECK_F(ticket.has_value(), "Headless buffer readback enqueue failed");
    return *ticket;
  }

  auto EnqueueTextureReadback(std::shared_ptr<GpuTextureReadback> readback,
    const std::shared_ptr<Texture>& source, TextureReadbackRequest request,
    std::string_view command_list_name, const bool immediate_submission = true)
    -> ReadbackTicket
  {
    auto recorder = AcquireRecorder(command_list_name, immediate_submission);
    CHECK_NOTNULL_F(recorder.get());
    recorder->BeginTrackingResourceState(
      *source, ResourceStates::kCommon, true);

    const auto ticket = readback->EnqueueCopy(*recorder, *source, request);
    CHECK_F(ticket.has_value(), "Headless texture readback enqueue failed");
    return *ticket;
  }

  auto SubmitDeferred() -> void { graphics_->SubmitDeferredCommandLists(); }

private:
  oxygen::graphics::GraphicsModuleApi* module_api_ { nullptr };
  void* backend_ { nullptr };
  Graphics* graphics_ { nullptr };
  oxygen::observer_ptr<oxygen::graphics::ReadbackManager> readback_manager_ {};
  std::vector<std::shared_ptr<oxygen::graphics::Buffer>> buffers_ {};
  std::vector<std::shared_ptr<oxygen::graphics::Texture>> textures_ {};
  std::vector<std::shared_ptr<GpuBufferReadback>> buffer_readbacks_ {};
  std::vector<std::shared_ptr<GpuTextureReadback>> texture_readbacks_ {};
};

class BufferReadbackCreationTest : public HeadlessReadbackTestBase { };
class BufferReadbackSubmissionTest : public HeadlessReadbackTestBase { };
class BufferReadbackValidationTest : public HeadlessReadbackTestBase { };
class BufferReadbackMappingTest : public HeadlessReadbackTestBase { };
class BufferReadbackLifecycleTest : public HeadlessReadbackTestBase { };
class BufferReadbackCoroutineTest : public HeadlessReadbackTestBase { };
class TextureReadbackMappingTest : public HeadlessReadbackTestBase { };
class TextureReadbackValidationTest : public HeadlessReadbackTestBase { };
class BlockingReadbackTest : public HeadlessReadbackTestBase { };
class ReadbackSurfaceTest : public HeadlessReadbackTestBase { };
class ReadbackShutdownTest : public HeadlessReadbackTestBase { };

NOLINT_TEST_F(BufferReadbackCreationTest, NewBufferReadbackStartsIdle)
{
  auto readback = CreateBufferReadback();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, EnqueueCopyReturnsPendingTicket)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(64, 0x31), "pending-source");
  auto readback = CreateBufferReadback();

  const auto ticket = EnqueueBufferReadback(
    readback, source, BufferRange { 8, 24 }, "buffer-pending");

  EXPECT_GT(ticket.id.get(), 0U);
  EXPECT_EQ(ticket.fence.get(), 1U);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, SecondEnqueueWhilePendingIsRejected)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(64, 0x36), "pending-twice");
  auto readback = CreateBufferReadback();

  const auto first_ticket = EnqueueBufferReadback(
    readback, source, BufferRange { 8, 24 }, "buffer-pending-twice", false);
  EXPECT_GT(first_ticket.id.get(), 0U);

  auto recorder = AcquireRecorder("buffer-pending-twice-reject", false);
  ASSERT_NE(recorder, nullptr);
  recorder->BeginTrackingResourceState(*source, ResourceStates::kCommon, true);

  const auto second_ticket
    = readback->EnqueueCopy(*recorder, *source, BufferRange { 0, 8 });
  ASSERT_FALSE(second_ticket.has_value());
  EXPECT_EQ(second_ticket.error(), ReadbackError::kAlreadyPending);

  const auto cancelled = GetReadbackManager()->Cancel(first_ticket);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
}

NOLINT_TEST_F(BufferReadbackSubmissionTest, IsReadyIsFalseBeforeDeferredSubmit)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(48, 0x44), "deferred-source");
  auto readback = CreateBufferReadback();

  EnqueueBufferReadback(
    readback, source, BufferRange { 4, 20 }, "buffer-deferred-ready", false);

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);

  SubmitDeferred();
  WaitForQueueIdle();

  const auto ready_after_submit = readback->IsReady();
  ASSERT_TRUE(ready_after_submit.has_value());
  EXPECT_TRUE(*ready_after_submit);
}

NOLINT_TEST_F(BufferReadbackValidationTest, InvalidBufferRangeIsRejected)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(24, 0x4A), "invalid-range");
  auto readback = CreateBufferReadback();

  auto recorder = AcquireRecorder("buffer-invalid-range");
  ASSERT_NE(recorder, nullptr);
  recorder->BeginTrackingResourceState(*source, ResourceStates::kCommon, true);

  const auto ticket
    = readback->EnqueueCopy(*recorder, *source, BufferRange { 32, 8 });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST_F(BufferReadbackMappingTest, MapNowReturnsExpectedBytes)
{
  const auto source_bytes = MakePatternBytes(72, 0x55);
  auto source = CreateBufferWithBytes(source_bytes, "map-now-source");
  auto readback = CreateBufferReadback();

  EnqueueBufferReadback(
    readback, source, BufferRange { 12, 28 }, "buffer-map-now");
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(CopyMappedBytes(*mapped), SliceBytes(source_bytes, 12, 28));
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, CancelPendingReadbackTransitionsToCancelled)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(40, 0x61), "cancel-source");
  auto readback = CreateBufferReadback();

  EnqueueBufferReadback(
    readback, source, BufferRange { 0, 16 }, "buffer-cancel", false);

  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
  EXPECT_EQ(readback->GetState(), ReadbackState::kCancelled);
}

NOLINT_TEST_F(
  BufferReadbackLifecycleTest, ReusableReadbackSupportsSequentialCycles)
{
  auto first = CreateBufferWithBytes(MakePatternBytes(32, 0x71), "reuse-first");
  auto second
    = CreateBufferWithBytes(MakePatternBytes(48, 0x82), "reuse-second");
  auto readback = CreateBufferReadback();

  EnqueueBufferReadback(
    readback, first, BufferRange { 4, 12 }, "buffer-reuse-first");
  auto first_map = readback->MapNow();
  ASSERT_TRUE(first_map.has_value());
  EXPECT_EQ(first_map->Bytes().size(), 12U);

  first_map = MappedBufferReadback {};
  readback->Reset();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);

  EnqueueBufferReadback(
    readback, second, BufferRange { 8, 20 }, "buffer-reuse-second");
  auto second_map = readback->MapNow();
  ASSERT_TRUE(second_map.has_value());
  EXPECT_EQ(second_map->Bytes().size(), 20U);
}

NOLINT_TEST_F(BufferReadbackLifecycleTest, ResetAfterCancellationReturnsToIdle)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(28, 0x84), "reset-cancel");
  auto readback = CreateBufferReadback();

  EnqueueBufferReadback(
    readback, source, BufferRange { 0, 12 }, "buffer-reset-cancel", false);
  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  ASSERT_TRUE(*cancelled);

  readback->Reset();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());
}

NOLINT_TEST_F(
  BufferReadbackCoroutineTest, AwaitAsyncCompletesWhenIsReadyPumpsCompletion)
{
  auto source
    = CreateBufferWithBytes(MakePatternBytes(56, 0x91), "await-source");
  auto readback = CreateBufferReadback();

  const auto ticket = EnqueueBufferReadback(
    readback, source, BufferRange { 6, 18 }, "buffer-await");

  TestEventLoop loop;
  bool resumed = false;
  std::jthread completion_pump([this, readback] {
    WaitForQueueIdle();
    const auto ready = readback->IsReady();
    CHECK_F(
      ready.has_value() && *ready, "Headless readback should become ready");
  });

  oxygen::co::Run(loop, [&]() -> Co<> {
    co_await GetReadbackManager()->AwaitAsync(ticket);
    resumed = true;
  });

  completion_pump.join();
  EXPECT_TRUE(resumed);
}

NOLINT_TEST_F(TextureReadbackMappingTest, MapNowReturnsExpectedBytesAndLayout)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "texture-map";
  const auto source_bytes = MakePatternBytes(4 * 4 * 4, 0x11);
  auto source = CreateTextureWithBytes(desc, source_bytes);
  auto readback = CreateTextureReadback();

  TextureReadbackRequest request {
    .src_slice = {
      .x = 1,
      .y = 1,
      .width = 2,
      .height = 2,
      .depth = 1,
    },
  };

  EnqueueTextureReadback(readback, source, request, "texture-map-now");
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(mapped->Layout().width, 2U);
  EXPECT_EQ(mapped->Layout().height, 2U);
  EXPECT_EQ(mapped->Layout().row_pitch.get(), 8U);
  EXPECT_EQ(mapped->Layout().slice_pitch.get(), 16U);

  const auto resolved = request.src_slice.Resolve(desc);
  std::vector<std::byte> actual(mapped->Data(),
    mapped->Data()
      + static_cast<std::ptrdiff_t>(mapped->Layout().slice_pitch.get()));
  EXPECT_EQ(actual, CopyTextureRegionBytes(source_bytes, desc, resolved));
}

NOLINT_TEST_F(
  TextureReadbackMappingTest, MapNowReturnsExpectedBytesForMipAndArraySlice)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 8;
  desc.array_size = 2;
  desc.mip_levels = 2;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "texture-mip-array";

  auto source = CreateTexture(desc);
  auto* headless_texture = source.get();
  ASSERT_NE(headless_texture, nullptr);

  const auto pattern = MakePatternBytes(4 * 4 * 4, 0x67);
  const auto write_offset
    = headless_texture->GetLayoutStrategy().ComputeSliceMipBaseOffset(
      desc, 1, 1);
  headless_texture->WriteBacking(
    pattern.data(), write_offset, static_cast<uint32_t>(pattern.size()));

  auto readback = CreateTextureReadback();
  TextureReadbackRequest request {
    .src_slice = {
      .width = 4,
      .height = 4,
      .depth = 1,
      .mip_level = 1,
      .array_slice = 1,
    },
  };

  EnqueueTextureReadback(
    readback, source, request, "texture-mip-array-readback");
  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(mapped->Layout().mip_level, 1U);
  EXPECT_EQ(mapped->Layout().array_slice, 1U);
  EXPECT_EQ(mapped->Layout().row_pitch.get(), 16U);
  EXPECT_EQ(mapped->Layout().slice_pitch.get(), 64U);

  std::vector<std::byte> actual(mapped->Data(),
    mapped->Data()
      + static_cast<std::ptrdiff_t>(mapped->Layout().slice_pitch.get()));
  EXPECT_EQ(actual, pattern);
}

NOLINT_TEST_F(TextureReadbackValidationTest, MixedAspectMaskIsRejected)
{
  TextureDesc desc {};
  desc.width = 2;
  desc.height = 2;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "texture-invalid-aspects";
  auto source = CreateTextureWithBytes(desc, MakePatternBytes(16, 0x33));
  auto readback = CreateTextureReadback();

  auto recorder = AcquireRecorder("texture-invalid-aspects");
  ASSERT_NE(recorder, nullptr);
  recorder->BeginTrackingResourceState(*source, ResourceStates::kCommon, true);

  const auto ticket = readback->EnqueueCopy(*recorder, *source,
    TextureReadbackRequest {
      .aspects = oxygen::graphics::ClearFlags::kColor
        | oxygen::graphics::ClearFlags::kDepth,
    });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST_F(BlockingReadbackTest, ReadBufferNowReturnsRequestedRangeBytes)
{
  const auto source_bytes = MakePatternBytes(36, 0xA1);
  auto source = CreateBufferWithBytes(source_bytes, "blocking-buffer");

  const auto bytes
    = GetReadbackManager()->ReadBufferNow(*source, BufferRange { 10, 14 });
  ASSERT_TRUE(bytes.has_value());
  EXPECT_EQ(*bytes, SliceBytes(source_bytes, 10, 14));
}

NOLINT_TEST_F(
  BlockingReadbackTest, ReadTextureNowReturnsTightlyPackedBytesAndLayout)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "blocking-texture";
  const auto source_bytes = MakePatternBytes(4 * 4 * 4, 0xB1);
  auto source = CreateTextureWithBytes(desc, source_bytes);

  const auto data = GetReadbackManager()->ReadTextureNow(*source,
    TextureReadbackRequest {
      .src_slice = {
        .x = 0,
        .y = 2,
        .width = 3,
        .height = 2,
      },
    },
    true);
  ASSERT_TRUE(data.has_value());
  EXPECT_TRUE(data->tightly_packed);
  EXPECT_EQ(data->layout.row_pitch.get(), 12U);
  EXPECT_EQ(data->layout.slice_pitch.get(), 24U);
  EXPECT_EQ(data->bytes,
    CopyTextureRegionBytes(source_bytes, desc,
      TextureSlice {
        .x = 0,
        .y = 2,
        .width = 3,
        .height = 2,
        .depth = 1,
      }));
}

NOLINT_TEST_F(ReadbackSurfaceTest,
  CreateReadbackTextureSurfaceNormalizesDescriptorForReadbackUse)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 4;
  desc.sample_count = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DMultiSample;
  desc.is_shader_resource = true;
  desc.is_render_target = true;
  desc.is_uav = true;
  desc.debug_name = "surface-create";

  const auto surface = GetReadbackManager()->CreateReadbackTextureSurface(desc);
  ASSERT_TRUE(surface.has_value());
  ASSERT_EQ((*surface)->GetTypeId(), Texture::ClassTypeId());
  const auto* texture = static_cast<const Texture*>(surface->get());

  const auto& surface_desc
    = static_cast<const oxygen::graphics::Texture&>(*texture).GetDescriptor();
  EXPECT_EQ(surface_desc.cpu_access, ResourceAccessMode::kReadBack);
  EXPECT_EQ(surface_desc.sample_count, 1U);
  EXPECT_EQ(surface_desc.texture_type, TextureType::kTexture2D);
  EXPECT_FALSE(surface_desc.is_shader_resource);
  EXPECT_FALSE(surface_desc.is_render_target);
  EXPECT_FALSE(surface_desc.is_uav);
  EXPECT_TRUE(texture->IsReadbackSurface());
}

NOLINT_TEST_F(
  ReadbackSurfaceTest, MapReadbackTextureSurfaceReturnsMipPitchMetadata)
{
  TextureDesc desc {};
  desc.width = 8;
  desc.height = 4;
  desc.array_size = 2;
  desc.mip_levels = 2;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.debug_name = "surface-map";

  const auto surface = GetReadbackManager()->CreateReadbackTextureSurface(desc);
  ASSERT_TRUE(surface.has_value());

  const TextureSlice slice {
    .mip_level = 1,
    .array_slice = 1,
  };
  const auto mapping
    = GetReadbackManager()->MapReadbackTextureSurface(**surface, slice);
  ASSERT_TRUE(mapping.has_value());
  ASSERT_NE(mapping->data, nullptr);
  EXPECT_EQ(mapping->layout.row_pitch.get(), 16U);
  EXPECT_EQ(mapping->layout.slice_pitch.get(), 32U);
  EXPECT_EQ(mapping->layout.width, 4U);
  EXPECT_EQ(mapping->layout.height, 2U);

  GetReadbackManager()->UnmapReadbackTextureSurface(**surface);
}

NOLINT_TEST_F(
  ReadbackSurfaceTest, MapReadbackTextureSurfaceRejectsSecondMapUntilUnmapped)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "surface-double-map";

  const auto surface = GetReadbackManager()->CreateReadbackTextureSurface(desc);
  ASSERT_TRUE(surface.has_value());

  const auto first
    = GetReadbackManager()->MapReadbackTextureSurface(**surface, {});
  ASSERT_TRUE(first.has_value());

  const auto second
    = GetReadbackManager()->MapReadbackTextureSurface(**surface, {});
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  GetReadbackManager()->UnmapReadbackTextureSurface(**surface);
}

NOLINT_TEST_F(
  ReadbackSurfaceTest, MapReadbackTextureSurfaceRejectsNonReadbackTexture)
{
  TextureDesc desc {};
  desc.width = 4;
  desc.height = 4;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name = "surface-non-readback";

  auto texture = CreateTexture(desc);
  const auto mapping
    = GetReadbackManager()->MapReadbackTextureSurface(*texture, {});
  ASSERT_FALSE(mapping.has_value());
  EXPECT_EQ(mapping.error(), ReadbackError::kUnsupportedResource);
}

NOLINT_TEST_F(ReadbackShutdownTest,
  ShutdownReturnsBackendFailureWhileDeferredSubmissionNeverSignals)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  auto source
    = CreateBufferWithBytes(MakePatternBytes(40, 0xC1), "shutdown-source");
  auto readback = CreateBufferReadback();

  const auto ticket = EnqueueBufferReadback(
    readback, source, BufferRange { 4, 20 }, "buffer-shutdown", false);

  const auto shutdown_result
    = GetReadbackManager()->Shutdown(std::chrono::milliseconds { 0 });
  ASSERT_FALSE(shutdown_result.has_value());
  EXPECT_EQ(shutdown_result.error(), ReadbackError::kBackendFailure);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);

  const auto cancelled = GetReadbackManager()->Cancel(ticket);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
}

} // namespace
