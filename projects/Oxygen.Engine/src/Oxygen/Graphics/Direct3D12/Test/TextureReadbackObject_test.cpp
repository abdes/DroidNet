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
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/QueueRole.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Test/Utils/TestEventLoop.h>

namespace {

using oxygen::OffsetBytes;
using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::co::Co;
using oxygen::co::Run;
using oxygen::co::testing::TestEventLoop;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ClearFlags;
using oxygen::graphics::Color;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::GpuTextureReadback;
using oxygen::graphics::MappedTextureReadback;
using oxygen::graphics::MsaaReadbackMode;
using oxygen::graphics::QueueRole;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ReadbackState;
using oxygen::graphics::ReadbackTicket;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackRequest;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureUploadRegion;
using oxygen::graphics::d3d12::testing::ReadbackTestFixture;

constexpr auto kUploadRowPitch = 256u;

auto WriteRgbaTexel(std::vector<std::byte>& buffer, const uint32_t row_pitch,
  const uint32_t x, const uint32_t y, const std::array<uint8_t, 4>& rgba)
  -> void
{
  const auto offset = static_cast<size_t>(y) * row_pitch + (x * 4u);
  buffer[offset + 0] = static_cast<std::byte>(rgba[0]);
  buffer[offset + 1] = static_cast<std::byte>(rgba[1]);
  buffer[offset + 2] = static_cast<std::byte>(rgba[2]);
  buffer[offset + 3] = static_cast<std::byte>(rgba[3]);
}

auto ReadRowBytes(const std::byte* data, const SizeBytes row_pitch,
  const uint32_t row_index, const uint32_t byte_count) -> std::vector<uint8_t>
{
  auto bytes = std::vector<uint8_t>(byte_count);
  const auto* row = data + row_pitch.get() * row_index;
  for (uint32_t index = 0; index < byte_count; ++index) {
    bytes[index] = static_cast<uint8_t>(row[index]);
  }
  return bytes;
}

template <class Awaitable>
auto RunWithTestEventLoop(TestEventLoop& loop, Awaitable&& awaitable)
  -> decltype(auto)
{
  return oxygen::co::Run(loop, std::forward<Awaitable>(awaitable));
}

class TextureReadbackTestBase : public ReadbackTestFixture {
protected:
  auto CreateTextureReadback(std::string_view debug_name = "texture-readback")
    -> std::shared_ptr<GpuTextureReadback>
  {
    auto readback = GetReadbackManager()->CreateTextureReadback(debug_name);
    CHECK_NOTNULL_F(readback.get());
    return readback;
  }

  auto CreateInitializedColorTexture(const TextureDesc& texture_desc,
    std::span<const std::byte> bytes, std::string_view debug_name)
    -> std::shared_ptr<Texture>
  {
    auto texture = CreateTexture(texture_desc);
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = kUploadRowPitch * texture_desc.height,
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "Upload",
    });
    upload->Update(bytes.data(), bytes.size(), 0);

    const TextureUploadRegion upload_region {
      .buffer_offset = 0,
      .buffer_row_pitch = kUploadRowPitch,
      .buffer_slice_pitch = kUploadRowPitch * texture_desc.height,
      .dst_slice = {
        .x = 0,
        .y = 0,
        .z = 0,
        .width = texture_desc.width,
        .height = texture_desc.height,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    };

    auto recorder = AcquireRecorder(std::string(debug_name) + "Init");
    CHECK_NOTNULL_F(recorder.get());
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload, upload_region, *texture);
    recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);

    WaitForQueueIdle();
    return texture;
  }

  auto CreateClearedMsaaRenderTarget(const Color clear_color,
    std::string_view debug_name) -> std::shared_ptr<Texture>
  {
    TextureDesc texture_desc {};
    texture_desc.width = 2;
    texture_desc.height = 2;
    texture_desc.sample_count = 4;
    texture_desc.format = oxygen::Format::kRGBA8UNorm;
    texture_desc.texture_type = TextureType::kTexture2DMultiSample;
    texture_desc.is_render_target = true;
    texture_desc.debug_name = std::string(debug_name);

    auto texture = CreateTexture(texture_desc);
    auto framebuffer = Backend().CreateFramebuffer(
      FramebufferDesc {}.AddColorAttachment(texture));
    CHECK_NOTNULL_F(framebuffer.get());

    auto recorder = AcquireRecorder(std::string(debug_name) + "Clear");
    CHECK_NOTNULL_F(recorder.get());
    recorder->BeginTrackingResourceState(*texture, ResourceStates::kCommon);
    recorder->RequireResourceState(*texture, ResourceStates::kRenderTarget);
    recorder->FlushBarriers();
    recorder->ClearFramebuffer(*framebuffer,
      std::vector<std::optional<Color>> { clear_color }, std::nullopt,
      std::nullopt);
    recorder->RequireResourceStateFinal(*texture, ResourceStates::kCommon);

    WaitForQueueIdle();
    return texture;
  }

  auto EnqueueReadback(std::shared_ptr<GpuTextureReadback> readback,
    const std::shared_ptr<Texture>& source, TextureReadbackRequest request,
    std::string_view command_list_name,
    const QueueRole role = QueueRole::kGraphics,
    const bool immediate_submission = true) -> ReadbackTicket
  {
    auto recorder
      = AcquireRecorder(command_list_name, role, immediate_submission);
    CHECK_NOTNULL_F(recorder.get());
    EnsureTracked(*recorder, source, ResourceStates::kCommon);

    const auto ticket = readback->EnqueueCopy(*recorder, *source, request);
    CHECK_F(ticket.has_value(), "Texture readback enqueue failed");
    return *ticket;
  }
};

class TextureReadbackCreationTest : public TextureReadbackTestBase { };
class TextureReadbackSubmissionTest : public TextureReadbackTestBase { };
class TextureReadbackMappingTest : public TextureReadbackTestBase { };
class TextureReadbackValidationTest : public TextureReadbackTestBase { };
class TextureReadbackLifecycleTest : public TextureReadbackTestBase { };
class TextureReadbackManagerTest : public TextureReadbackTestBase { };
class TextureReadbackFrameLifecycleTest : public TextureReadbackTestBase { };
class TextureReadbackCoroutineTest : public TextureReadbackTestBase { };
class TextureReadbackShutdownTest : public TextureReadbackTestBase { };
class TextureReadbackResolveTest : public TextureReadbackTestBase { };

NOLINT_TEST_F(TextureReadbackCreationTest, NewTextureReadbackStartsIdle)
{
  auto readback = CreateTextureReadback();

  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);
}

NOLINT_TEST_F(TextureReadbackCreationTest, MapNowBeforeEnqueueReturnsNotReady)
{
  auto readback = CreateTextureReadback();

  const auto mapped = readback->MapNow();
  ASSERT_FALSE(mapped.has_value());
  EXPECT_EQ(mapped.error(), ReadbackError::kNotReady);
}

NOLINT_TEST_F(TextureReadbackSubmissionTest, EnqueueCopyReturnsPendingTicket)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "pending-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  WriteRgbaTexel(upload_bytes, kUploadRowPitch, 0, 0, { 1u, 2u, 3u, 4u });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "pending-texture");
  auto readback = CreateTextureReadback("pending-texture-readback");

  const auto ticket = EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    },
    "texture-readback-pending", QueueRole::kGraphics, false);

  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
  ASSERT_TRUE(readback->Ticket().has_value());
  EXPECT_EQ(readback->Ticket()->id.get(), ticket.id.get());
  EXPECT_EQ(readback->Ticket()->fence.get(), ticket.fence.get());
  EXPECT_GT(ticket.fence.get(), 0U);
}

NOLINT_TEST_F(
  TextureReadbackSubmissionTest, IsReadyIsFalseBeforeFenceCompletion)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "not-ready-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "not-ready-texture");
  auto readback = CreateTextureReadback("not-ready-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 0, .y = 0, .width = 2, .height = 2 },
    },
    "texture-readback-not-ready", QueueRole::kGraphics, false);

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_FALSE(*ready);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
}

NOLINT_TEST_F(TextureReadbackSubmissionTest, SecondEnqueueWhilePendingFails)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "double-enqueue-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "double-enqueue-texture");
  auto readback = CreateTextureReadback("double-enqueue-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 0, .y = 0, .width = 2, .height = 2 },
    },
    "texture-readback-first-enqueue", QueueRole::kGraphics, false);

  auto recorder = AcquireRecorder(
    "texture-readback-second-enqueue", QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, texture, ResourceStates::kCommon);

  const auto second = readback->EnqueueCopy(*recorder, *texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    });
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyPending);
}

NOLINT_TEST_F(
  TextureReadbackMappingTest, IsReadyBecomesTrueAfterFenceCompletion)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "ready-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "ready-texture");
  auto readback = CreateTextureReadback("ready-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 0, .y = 0, .width = 2, .height = 2 },
    },
    "texture-readback-ready");
  WaitForQueueIdle();

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(TextureReadbackMappingTest, TryMapFailsWhilePending)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "pending-map-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "pending-map-texture");
  auto readback = CreateTextureReadback("pending-map-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 0, .y = 0, .width = 2, .height = 2 },
    },
    "texture-readback-pending-map", QueueRole::kGraphics, false);

  const auto mapped = readback->TryMap();
  ASSERT_FALSE(mapped.has_value());
  EXPECT_EQ(mapped.error(), ReadbackError::kNotReady);
}

NOLINT_TEST_F(TextureReadbackMappingTest, TryMapReturnsExpectedBytesAndLayout)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "mapped-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(10u * y + x), static_cast<uint8_t>(40u + x),
          static_cast<uint8_t>(80u + y), static_cast<uint8_t>(120u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "mapped-texture");
  auto readback = CreateTextureReadback("mapped-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = {
        .x = 1,
        .y = 1,
        .z = 0,
        .width = 2,
        .height = 2,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    },
    "texture-readback-map");
  WaitForQueueIdle();

  const auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());

  const auto& layout = mapped->Layout();
  EXPECT_EQ(layout.format, oxygen::Format::kRGBA8UNorm);
  EXPECT_EQ(layout.texture_type, TextureType::kTexture2D);
  EXPECT_EQ(layout.width, 2U);
  EXPECT_EQ(layout.height, 2U);
  EXPECT_EQ(layout.depth, 1U);
  EXPECT_EQ(layout.aspects, ClearFlags::kColor);
  EXPECT_EQ(layout.mip_level, 0U);
  EXPECT_EQ(layout.array_slice, 0U);
  EXPECT_GT(layout.row_pitch.get(), 0U);
  EXPECT_EQ(layout.row_pitch.get() % 256U, 0U);
  EXPECT_EQ(layout.slice_pitch.get(), layout.row_pitch.get() * 2U);

  ASSERT_NE(mapped->Data(), nullptr);
  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 11U, 41U, 81U, 122U, 12U, 42U, 81U, 123U }));
  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 21U, 41U, 82U, 123U, 22U, 42U, 82U, 124U }));
}

NOLINT_TEST_F(TextureReadbackMappingTest, TryMapRejectsSecondActiveMap)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "active-map-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(7u * y + x), static_cast<uint8_t>(50u + x),
          static_cast<uint8_t>(90u + y), static_cast<uint8_t>(130u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "active-map-texture");
  auto readback = CreateTextureReadback("active-map-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = {
        .x = 1,
        .y = 1,
        .z = 0,
        .width = 2,
        .height = 2,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    },
    "texture-readback-active-map");
  WaitForQueueIdle();

  auto first = readback->TryMap();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);

  const auto second = readback->TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  first = MappedTextureReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);

  const auto third = readback->TryMap();
  ASSERT_TRUE(third.has_value());
}

NOLINT_TEST_F(TextureReadbackMappingTest,
  MappedViewMoveKeepsReadbackMappedUntilFinalOwnerReleases)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "moved-map-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(9u * y + x), static_cast<uint8_t>(55u + x),
          static_cast<uint8_t>(95u + y), static_cast<uint8_t>(135u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "moved-map-texture");
  auto readback = CreateTextureReadback("moved-map-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = {
        .x = 1,
        .y = 1,
        .z = 0,
        .width = 2,
        .height = 2,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    },
    "texture-readback-moved-map");
  WaitForQueueIdle();

  auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());

  auto moved = std::move(*mapped);
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);

  const auto& layout = moved.Layout();
  EXPECT_EQ(ReadRowBytes(moved.Data(), layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 10U, 56U, 96U, 137U, 11U, 57U, 96U, 138U }));
  EXPECT_EQ(ReadRowBytes(moved.Data(), layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 19U, 56U, 97U, 138U, 20U, 57U, 97U, 139U }));

  const auto second = readback->TryMap();
  ASSERT_FALSE(second.has_value());
  EXPECT_EQ(second.error(), ReadbackError::kAlreadyMapped);

  moved = MappedTextureReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(TextureReadbackMappingTest, MapNowReturnsExpectedBytesAndLayout)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "map-now-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(20u * y + x), static_cast<uint8_t>(60u + x),
          static_cast<uint8_t>(100u + y), static_cast<uint8_t>(140u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "map-now-texture");
  auto readback = CreateTextureReadback("map-now-texture-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = {
        .x = 1,
        .y = 1,
        .z = 0,
        .width = 2,
        .height = 2,
        .depth = 1,
        .mip_level = 0,
        .array_slice = 0,
      },
    },
    "texture-readback-map-now");

  const auto mapped = readback->MapNow();
  ASSERT_TRUE(mapped.has_value());

  const auto& layout = mapped->Layout();
  EXPECT_EQ(layout.width, 2U);
  EXPECT_EQ(layout.height, 2U);
  EXPECT_EQ(layout.texture_type, TextureType::kTexture2D);
  EXPECT_EQ(layout.aspects, ClearFlags::kColor);
  EXPECT_EQ(readback->GetState(), ReadbackState::kMapped);
  ASSERT_NE(mapped->Data(), nullptr);

  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 21U, 61U, 101U, 142U, 22U, 62U, 101U, 143U }));
  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 41U, 61U, 102U, 143U, 42U, 62U, 102U, 144U }));
}

NOLINT_TEST_F(TextureReadbackValidationTest, MixedAspectMaskIsRejected)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "mixed-aspect-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "mixed-aspect-texture");
  auto readback = CreateTextureReadback("mixed-aspect-readback");

  auto recorder = AcquireRecorder(
    "texture-readback-mixed-aspect", QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, texture, ResourceStates::kCommon);

  const auto ticket = readback->EnqueueCopy(*recorder, *texture,
    TextureReadbackRequest {
      .aspects = ClearFlags::kColor | ClearFlags::kDepth });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST_F(
  TextureReadbackValidationTest, DepthAspectIsRejectedForColorTexture)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "depth-aspect-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "depth-aspect-texture");
  auto readback = CreateTextureReadback("depth-aspect-readback");

  auto recorder = AcquireRecorder(
    "texture-readback-depth-aspect", QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, texture, ResourceStates::kCommon);

  const auto ticket = readback->EnqueueCopy(*recorder, *texture,
    TextureReadbackRequest { .aspects = ClearFlags::kDepth });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST_F(TextureReadbackValidationTest,
  MultisampledTextureIsRejectedWhenResolveIsDisallowed)
{
  auto texture = CreateClearedMsaaRenderTarget(
    Color { 1.0F, 0.0F, 0.0F, 1.0F }, "msaa-disallow-texture");
  auto readback = CreateTextureReadback("msaa-disallow-readback");

  auto recorder = AcquireRecorder(
    "texture-readback-msaa-disallow", QueueRole::kGraphics, false);
  CHECK_NOTNULL_F(recorder.get());
  EnsureTracked(*recorder, texture, ResourceStates::kCommon);

  const auto ticket = readback->EnqueueCopy(*recorder, *texture,
    TextureReadbackRequest { .msaa_mode = MsaaReadbackMode::kDisallow });
  ASSERT_FALSE(ticket.has_value());
  EXPECT_EQ(ticket.error(), ReadbackError::kUnsupportedResource);
}

NOLINT_TEST_F(
  TextureReadbackLifecycleTest, CancelPendingReadbackTransitionsToCancelled)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "cancel-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "cancel-texture");
  auto readback = CreateTextureReadback("cancel-readback");

  const auto ticket
    = EnqueueReadback(readback, texture, TextureReadbackRequest {},
      "texture-readback-cancel", QueueRole::kGraphics, false);

  const auto cancelled = CancelReadback(ticket);
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_TRUE(*cancelled);
  EXPECT_EQ(readback->GetState(), ReadbackState::kCancelled);
}

NOLINT_TEST_F(TextureReadbackLifecycleTest, CancelCompletedReadbackReturnsFalse)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "cancel-completed-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "cancel-completed-texture");
  auto readback = CreateTextureReadback("cancel-completed-readback");

  EnqueueReadback(readback, texture, {}, "texture-readback-cancel-completed");
  WaitForQueueIdle();

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);

  const auto cancelled = readback->Cancel();
  ASSERT_TRUE(cancelled.has_value());
  EXPECT_FALSE(*cancelled);
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(TextureReadbackLifecycleTest, ResetAfterCompletionReturnsIdle)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "reset-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "reset-texture");
  auto readback = CreateTextureReadback("reset-readback");

  EnqueueReadback(
    readback, texture, {}, "texture-readback-reset", QueueRole::kGraphics);
  WaitForQueueIdle();

  {
    const auto mapped = readback->TryMap();
    ASSERT_TRUE(mapped.has_value());
  }

  readback->Reset();
  EXPECT_EQ(readback->GetState(), ReadbackState::kIdle);
  EXPECT_FALSE(readback->Ticket().has_value());
}

NOLINT_TEST_F(TextureReadbackLifecycleTest,
  ReusableTextureReadbackSupportsSequentialEnqueueMapResetCycles)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "reuse-texture";

  auto readback = CreateTextureReadback("reuse-readback");

  auto MakeTexture
    = [&](const std::array<uint8_t, 4>& texel, std::string_view debug_name) {
        std::vector<std::byte> upload_bytes(
          kUploadRowPitch * texture_desc.height, std::byte { 0 });
        for (uint32_t y = 0; y < texture_desc.height; ++y) {
          for (uint32_t x = 0; x < texture_desc.width; ++x) {
            WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y, texel);
          }
        }
        return CreateInitializedColorTexture(
          texture_desc, upload_bytes, debug_name);
      };

  auto first = MakeTexture({ 7U, 8U, 9U, 10U }, "reuse-first-texture");
  EnqueueReadback(readback, first, {}, "texture-readback-reuse-first");
  {
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(ReadRowBytes(mapped->Data(), mapped->Layout().row_pitch, 0U, 8U),
      (std::vector<uint8_t> { 7U, 8U, 9U, 10U, 7U, 8U, 9U, 10U }));
  }
  readback->Reset();

  auto second = MakeTexture({ 21U, 22U, 23U, 24U }, "reuse-second-texture");
  EnqueueReadback(readback, second, {}, "texture-readback-reuse-second");
  {
    const auto mapped = readback->MapNow();
    ASSERT_TRUE(mapped.has_value());
    EXPECT_EQ(ReadRowBytes(mapped->Data(), mapped->Layout().row_pitch, 0U, 8U),
      (std::vector<uint8_t> { 21U, 22U, 23U, 24U, 21U, 22U, 23U, 24U }));
  }
}

NOLINT_TEST_F(TextureReadbackManagerTest,
  ManagerAwaitCompletesTicketProducedByTextureReadback)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "await-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(10u * y + x), static_cast<uint8_t>(40u + x),
          static_cast<uint8_t>(80u + y), static_cast<uint8_t>(120u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "await-texture");
  auto readback = CreateTextureReadback("await-texture-readback");

  const auto ticket = EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    },
    "texture-readback-await");

  const auto result = AwaitReadback(ticket);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->ticket.id.get(), ticket.id.get());
  EXPECT_EQ(result->ticket.fence.get(), ticket.fence.get());
  EXPECT_FALSE(result->error.has_value());

  const auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  const auto expected_bytes_copied = mapped->Layout().row_pitch.get()
      * static_cast<uint64_t>(mapped->Layout().height - 1U)
    + (static_cast<uint64_t>(mapped->Layout().width) * 4U);
  EXPECT_EQ(result->bytes_copied.get(), expected_bytes_copied);
  EXPECT_EQ(ReadRowBytes(mapped->Data(), mapped->Layout().row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 11U, 41U, 81U, 122U, 12U, 42U, 81U, 123U }));

  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
}

NOLINT_TEST_F(TextureReadbackFrameLifecycleTest,
  OnFrameStartRetiresCompletedTicketWhileMappedBytesStayValid)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "retire-ticket-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(30u * y + x), static_cast<uint8_t>(20u + x),
          static_cast<uint8_t>(90u + y), static_cast<uint8_t>(110u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "retire-ticket-texture");
  auto readback = CreateTextureReadback("retire-ticket-readback");

  const auto ticket = EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    },
    "texture-readback-retire-ticket");
  WaitForQueueIdle();

  auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  EXPECT_EQ(ReadRowBytes(mapped->Data(), mapped->Layout().row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 31U, 21U, 91U, 112U, 32U, 22U, 91U, 113U }));

  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  EXPECT_EQ(ReadRowBytes(mapped->Data(), mapped->Layout().row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 31U, 21U, 91U, 112U, 32U, 22U, 91U, 113U }));

  const auto awaited = AwaitReadback(ticket);
  ASSERT_FALSE(awaited.has_value());
  EXPECT_EQ(awaited.error(), ReadbackError::kTicketNotFound);

  mapped = MappedTextureReadback {};
  EXPECT_EQ(readback->GetState(), ReadbackState::kReady);
}

NOLINT_TEST_F(TextureReadbackCoroutineTest,
  AwaitAsyncCompletesWhenFramePumpMarksTicketComplete)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "await-async-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(15u * y + x), static_cast<uint8_t>(50u + x),
          static_cast<uint8_t>(70u + y), static_cast<uint8_t>(100u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "await-async-texture");
  auto readback = CreateTextureReadback("await-async-readback");

  const auto ticket = EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    },
    "texture-readback-await-async");

  TestEventLoop loop;
  bool resumed = false;
  std::jthread completion_pump([this, readback] {
    WaitForQueueIdle();
    const auto ready = readback->IsReady();
    CHECK_F(ready.has_value() && *ready, "Readback should become ready");
  });

  RunWithTestEventLoop(loop, [&]() -> Co<> {
    co_await GetReadbackManager()->AwaitAsync(ticket);
    resumed = true;
  });

  completion_pump.join();
  EXPECT_TRUE(resumed);
  const auto ready = readback->IsReady();
  ASSERT_TRUE(ready.has_value());
  EXPECT_TRUE(*ready);
}

NOLINT_TEST_F(TextureReadbackCoroutineTest,
  AwaitAsyncReturnsImmediatelyAfterCompletionWasPumped)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "await-async-complete-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(12u * y + x), static_cast<uint8_t>(20u + x),
          static_cast<uint8_t>(40u + y), static_cast<uint8_t>(60u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "await-async-complete-texture");
  auto readback = CreateTextureReadback("await-async-complete-readback");

  const auto ticket = EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 1, .y = 1, .width = 2, .height = 2 },
    },
    "texture-readback-await-async-complete");
  WaitForQueueIdle();
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 1 });

  TestEventLoop loop;
  bool resumed = false;
  RunWithTestEventLoop(loop, [&]() -> Co<> {
    co_await GetReadbackManager()->AwaitAsync(ticket);
    resumed = true;
  });

  EXPECT_TRUE(resumed);
}

NOLINT_TEST_F(TextureReadbackShutdownTest,
  ShutdownReturnsBackendFailureWhileDeferredSubmissionNeverSignals)
{
  GetReadbackManager()->OnFrameStart(oxygen::frame::Slot { 0 });

  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "shutdown-pending-texture";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "shutdown-pending-texture");
  auto readback = CreateTextureReadback("shutdown-pending-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest {
      .src_slice = { .x = 0, .y = 0, .width = 2, .height = 2 },
    },
    "texture-readback-shutdown-pending", QueueRole::kGraphics, false);

  const auto shutdown_result
    = GetReadbackManager()->Shutdown(std::chrono::milliseconds { 0 });
  ASSERT_FALSE(shutdown_result.has_value());
  EXPECT_EQ(shutdown_result.error(), ReadbackError::kBackendFailure);
  EXPECT_EQ(readback->GetState(), ReadbackState::kPending);
}

NOLINT_TEST_F(
  TextureReadbackResolveTest, MultisampledColorTextureResolvesBeforeReadback)
{
  auto texture = CreateClearedMsaaRenderTarget(
    Color { 1.0F, 0.0F, 0.0F, 1.0F }, "msaa-resolve-texture");
  auto readback = CreateTextureReadback("msaa-resolve-readback");

  EnqueueReadback(readback, texture,
    TextureReadbackRequest { .msaa_mode = MsaaReadbackMode::kResolveIfNeeded },
    "texture-readback-msaa-resolve");
  WaitForQueueIdle();

  const auto mapped = readback->TryMap();
  ASSERT_TRUE(mapped.has_value());
  const auto& layout = mapped->Layout();
  EXPECT_EQ(layout.texture_type, TextureType::kTexture2D);
  EXPECT_EQ(layout.width, 2U);
  EXPECT_EQ(layout.height, 2U);
  ASSERT_NE(mapped->Data(), nullptr);

  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 255U, 0U, 0U, 255U, 255U, 0U, 0U, 255U }));
  EXPECT_EQ(ReadRowBytes(mapped->Data(), layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 255U, 0U, 0U, 255U, 255U, 0U, 0U, 255U }));
}

} // namespace
