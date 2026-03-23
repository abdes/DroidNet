//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Framebuffer.h>
#include <Oxygen/Graphics/Common/ReadbackManager.h>
#include <Oxygen/Graphics/Common/ReadbackTypes.h>
#include <Oxygen/Graphics/Common/Types/Color.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>

namespace {

using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferRange;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::Color;
using oxygen::graphics::FramebufferDesc;
using oxygen::graphics::MsaaReadbackMode;
using oxygen::graphics::OwnedTextureReadbackData;
using oxygen::graphics::ReadbackError;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureReadbackRequest;
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
  std::vector<uint8_t> bytes(byte_count);
  const auto* row = data + (row_pitch.get() * static_cast<size_t>(row_index));
  for (uint32_t index = 0; index < byte_count; ++index) {
    bytes[index] = static_cast<uint8_t>(row[index]);
  }
  return bytes;
}

class BlockingReadbackTestBase : public ReadbackTestFixture {
protected:
  auto CreateInitializedDeviceBuffer(const std::vector<std::byte>& bytes,
    std::string_view debug_name) -> std::shared_ptr<Buffer>
  {
    auto upload = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kUpload,
      .debug_name = std::string(debug_name) + "Upload",
    });
    auto device = CreateRegisteredBuffer(BufferDesc {
      .size_bytes = bytes.size(),
      .usage = BufferUsage::kNone,
      .memory = BufferMemory::kDeviceLocal,
      .debug_name = std::string(debug_name),
    });

    upload->Update(bytes.data(), bytes.size(), 0);

    auto recorder = AcquireRecorder(std::string(debug_name) + "Init");
    CHECK_NOTNULL_F(recorder.get());
    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, device, ResourceStates::kCommon);
    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*device, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBuffer(*device, 0, *upload, 0, bytes.size());
    recorder->RequireResourceStateFinal(*device, ResourceStates::kCommon);

    WaitForQueueIdle();
    return device;
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
};

class BlockingBufferReadbackTest : public BlockingReadbackTestBase { };
class BlockingTextureReadbackTest : public BlockingReadbackTestBase { };

NOLINT_TEST_F(
  BlockingBufferReadbackTest, ReadBufferNowReturnsRequestedRangeBytes)
{
  std::vector<std::byte> source_bytes(48);
  for (size_t index = 0; index < source_bytes.size(); ++index) {
    source_bytes[index] = static_cast<std::byte>(0x20 + index);
  }

  auto source = CreateInitializedDeviceBuffer(source_bytes, "blocking-buffer");
  const auto bytes
    = GetReadbackManager()->ReadBufferNow(*source, BufferRange { 7, 19 });
  ASSERT_TRUE(bytes.has_value());

  const auto expected = std::vector<std::byte>(
    source_bytes.begin() + 7, source_bytes.begin() + 26);
  EXPECT_EQ(*bytes, expected);
}

NOLINT_TEST_F(BlockingBufferReadbackTest, ReadBufferNowRejectsInvalidRange)
{
  std::vector<std::byte> source_bytes(16, std::byte { 0x44 });
  auto source
    = CreateInitializedDeviceBuffer(source_bytes, "blocking-buffer-invalid");

  const auto bytes
    = GetReadbackManager()->ReadBufferNow(*source, BufferRange { 16, 4 });
  ASSERT_FALSE(bytes.has_value());
  EXPECT_EQ(bytes.error(), ReadbackError::kInvalidArgument);
}

NOLINT_TEST_F(
  BlockingTextureReadbackTest, ReadTextureNowReturnsTightlyPackedBytesAndLayout)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "blocking-texture-tight";

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
    texture_desc, upload_bytes, "blocking-texture-tight");
  const auto readback = GetReadbackManager()->ReadTextureNow(*texture,
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
    });
  ASSERT_TRUE(readback.has_value());

  EXPECT_TRUE(readback->tightly_packed);
  EXPECT_EQ(readback->layout.width, 2U);
  EXPECT_EQ(readback->layout.height, 2U);
  EXPECT_EQ(readback->layout.row_pitch.get(), 8U);
  EXPECT_EQ(readback->layout.slice_pitch.get(), 16U);
  ASSERT_EQ(readback->bytes.size(), 16U);

  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 11U, 41U, 81U, 122U, 12U, 42U, 81U, 123U }));
  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 21U, 41U, 82U, 123U, 22U, 42U, 82U, 124U }));
}

NOLINT_TEST_F(BlockingTextureReadbackTest,
  ReadTextureNowCanPreserveNativePitchWhenTightPackingIsDisabled)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "blocking-texture-native";

  std::vector<std::byte> upload_bytes(
    kUploadRowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kUploadRowPitch, x, y,
        { static_cast<uint8_t>(5u * y + x), static_cast<uint8_t>(30u + x),
          static_cast<uint8_t>(60u + y), static_cast<uint8_t>(90u + x + y) });
    }
  }

  auto texture = CreateInitializedColorTexture(
    texture_desc, upload_bytes, "blocking-texture-native");
  const auto readback = GetReadbackManager()->ReadTextureNow(*texture,
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
    false);
  ASSERT_TRUE(readback.has_value());

  EXPECT_FALSE(readback->tightly_packed);
  EXPECT_EQ(readback->layout.width, 2U);
  EXPECT_EQ(readback->layout.height, 2U);
  EXPECT_GT(readback->layout.row_pitch.get(), 8U);
  EXPECT_EQ(readback->layout.row_pitch.get() % 256U, 0U);
  EXPECT_EQ(readback->bytes.size(), readback->layout.slice_pitch.get());

  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 6U, 31U, 61U, 92U, 7U, 32U, 61U, 93U }));
  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 11U, 31U, 62U, 93U, 12U, 32U, 62U, 94U }));
}

NOLINT_TEST_F(
  BlockingTextureReadbackTest, ReadTextureNowResolvesMsaaColorTextureWhenNeeded)
{
  auto texture = CreateClearedMsaaRenderTarget(
    Color { 1.0F, 0.0F, 0.0F, 1.0F }, "blocking-msaa-texture");
  const auto readback = GetReadbackManager()->ReadTextureNow(*texture,
    TextureReadbackRequest { .msaa_mode = MsaaReadbackMode::kResolveIfNeeded });
  ASSERT_TRUE(readback.has_value());

  EXPECT_TRUE(readback->tightly_packed);
  EXPECT_EQ(readback->layout.texture_type, TextureType::kTexture2D);
  EXPECT_EQ(readback->layout.width, 2U);
  EXPECT_EQ(readback->layout.height, 2U);
  EXPECT_EQ(readback->layout.row_pitch.get(), 8U);
  EXPECT_EQ(readback->layout.slice_pitch.get(), 16U);
  ASSERT_EQ(readback->bytes.size(), 16U);

  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 0U, 8U),
    (std::vector<uint8_t> { 255U, 0U, 0U, 255U, 255U, 0U, 0U, 255U }));
  EXPECT_EQ(
    ReadRowBytes(readback->bytes.data(), readback->layout.row_pitch, 1U, 8U),
    (std::vector<uint8_t> { 255U, 0U, 0U, 255U, 255U, 0U, 0U, 255U }));
}

} // namespace
