//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/Frame.h>
#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Detail/Barriers.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Queues.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Test/Upload/UploadCoordinatorTest.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace {

using oxygen::engine::upload::SizeBytes;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::UploadTextureDesc;
using oxygen::engine::upload::testing::UploadCoordinatorTest;
using oxygen::graphics::QueueKey;
using oxygen::graphics::TextureDesc;
namespace frame = oxygen::frame;

//! Single full-texture upload: verifies one region with aligned row/slice
//! pitches.
NOLINT_TEST_F(
  UploadCoordinatorTest, Texture2D_FullUpload_RecordsRegionAndCompletes)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_size = 1,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture2D,
    .clear_value = oxygen::graphics::Color { 0, 0, 0, 0 },
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  // Provide enough bytes; exact content is irrelevant for this test
  // 128*64*4 = 32768; aligned row pitch will be 512? Actually 128*4=512,
  // alignment keeps 512
  std::vector<std::byte> data(32768);

  UploadRequest req {
    .kind = UploadKind::kTexture2D,
    .priority = {},
    .debug_name = "TexUploadFull",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 128,
      .height = 64,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data.data(), data.size()),
    },
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert
  const auto& log = GfxPtr()->texture_log_;
  ASSERT_TRUE(log.copy_called);
  ASSERT_NE(log.dst, nullptr);
  EXPECT_EQ(log.dst, tex.get());
  ASSERT_EQ(log.regions.size(), 1u);

  const auto& r = log.regions.front();
  // Validate row/slice pitches: RGBA8 => bytes/row = 128*4 = 512, aligned to
  // 256 stays 512; slice = 512*64 = 32768
  EXPECT_EQ(r.buffer_row_pitch, 512u);
  EXPECT_EQ(r.buffer_slice_pitch, 32768u);
  // Placement alignment: 512B; expect offset multiple of 512 (likely 0)
  EXPECT_EQ(r.buffer_offset % 512u, 0u);
  // Destination slice covers full subresource at mip0/array0
  EXPECT_EQ(r.dst_slice.mip_level, 0u);
  EXPECT_EQ(r.dst_slice.array_slice, 0u);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_EQ(res->bytes_uploaded, 32768u);
}

//! Multi-subresource upload: verifies two regions with proper pitches and
//! placement alignment.
NOLINT_TEST_F(
  UploadCoordinatorTest, Texture2D_MipChainTwoRegions_AlignedOffsets)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 64,
    .height = 32,
    .depth = 1,
    .array_size = 1,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture2D,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  // Expected pitches: mip0 row=64*4=256 (already aligned), slice=256*32=8192
  // mip1 row=32*4=128 -> aligned to 256, slice=256*16=4096; offsets: 0, 8192
  constexpr uint64_t total = 8192 + 4096; // 12288
  std::vector<std::byte> data(total);

  UploadRequest req {
    .kind = UploadKind::kTexture2D,
    .priority = {},
    .debug_name = "TexUploadMips",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 64,
      .height = 32,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {
      { .mip = 0, .array_slice = 0, .x = 0, .y = 0, .z = 0, .width = 0, .height = 0, .depth = 0, .row_pitch = 0, .slice_pitch = 0, },
      { .mip = 1, .array_slice = 0, .x = 0, .y = 0, .z = 0, .width = 0, .height = 0, .depth = 0, .row_pitch = 0, .slice_pitch = 0, },
    },
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data.data(), data.size()),
    },
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert
  const auto& log = GfxPtr()->texture_log_;
  ASSERT_TRUE(log.copy_called);
  ASSERT_EQ(log.regions.size(), 2u);
  const auto& r0 = log.regions[0];
  const auto& r1 = log.regions[1];

  EXPECT_EQ(r0.buffer_row_pitch, 256u);
  EXPECT_EQ(r0.buffer_slice_pitch, 8192u);
  EXPECT_EQ(r0.buffer_offset, 0u);
  EXPECT_EQ(r0.dst_slice.mip_level, 0u);

  EXPECT_EQ(r1.buffer_row_pitch, 256u);
  EXPECT_EQ(r1.buffer_slice_pitch, 4096u);
  EXPECT_EQ(r1.buffer_offset, 8192u);
  EXPECT_EQ(r1.dst_slice.mip_level, 1u);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  // Ticket completion
  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_EQ(res->bytes_uploaded, total);
}

//! Full Texture2D upload using a producer callback; verifies region pitches and
//! completion.
NOLINT_TEST_F(
  UploadCoordinatorTest, Texture2D_FullUpload_WithProducer_Completes)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 128,
    .height = 64,
    .depth = 1,
    .array_size = 1,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture2D,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  constexpr uint32_t w = 128;
  constexpr uint32_t h = 64;
  constexpr uint32_t bpp = 4; // RGBA8
  constexpr uint64_t expected_row
    = static_cast<uint64_t>(w) * bpp; // 512, already aligned
  constexpr uint64_t expected_slice = expected_row * h; // 32768

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte> out) -> bool {
    producer_ran = true;
    std::memset(out.data(), 0x7F, out.size());
    return true;
  };

  UploadRequest req {
    .kind = UploadKind::kTexture2D,
    .priority = {},
    .debug_name = "TexUploadFullProd",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = w,
      .height = h,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {},
    .data = std::move(prod),
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert
  EXPECT_TRUE(producer_ran);

  const auto& log = GfxPtr()->texture_log_;
  ASSERT_TRUE(log.copy_called);
  ASSERT_EQ(log.regions.size(), 1u);
  const auto& r = log.regions.front();
  EXPECT_EQ(r.buffer_row_pitch, expected_row);
  EXPECT_EQ(r.buffer_slice_pitch, expected_slice);
  EXPECT_EQ(r.buffer_offset % 512u, 0u);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  EXPECT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_EQ(res->bytes_uploaded, expected_slice);
}

//! Producer returns false: no CopyBufferToTexture and failed result.
NOLINT_TEST_F(UploadCoordinatorTest, Texture2D_FullUpload_ProducerFails_NoCopy)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 64,
    .height = 32,
    .depth = 1,
    .array_size = 1,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture2D,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req {
    .kind = UploadKind::kTexture2D,
    .priority = {},
    .debug_name = "TexProdFail",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 64,
      .height = 32,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {},
    .data = std::move(prod),
  };

  auto& uploader = Uploader();

  // Act
  auto ticket_result = uploader.Submit(req, Staging());
  ASSERT_TRUE(ticket_result.has_value()) << "Submit failed";
  const auto ticket = ticket_result.value();

  // Assert
  EXPECT_TRUE(producer_ran);
  const auto& log = GfxPtr()->texture_log_;
  EXPECT_FALSE(log.copy_called);

  // Simulate frame advance to complete fences
  SimulateFrameStart(frame::Slot { 1 });

  auto complete_result = uploader.IsComplete(ticket);
  ASSERT_TRUE(complete_result.has_value()) << "IsComplete failed";
  ASSERT_TRUE(complete_result.value());
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_FALSE(res->success);
  EXPECT_EQ(res->error, oxygen::engine::upload::UploadError::kProducerFailed);
  EXPECT_EQ(res->bytes_uploaded, 0u);
}

} // namespace
