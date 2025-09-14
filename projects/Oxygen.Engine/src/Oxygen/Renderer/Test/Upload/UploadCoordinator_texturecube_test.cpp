//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstring>
#include <map>
#include <memory>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Types/Format.h>
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

using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::UploadTextureDesc;
using oxygen::engine::upload::testing::UploadCoordinatorTest;
using oxygen::graphics::QueueKey;
using oxygen::graphics::TextureDesc;

//! Full cube face upload (single face selected by array_slice=0): verifies one
//! region and correct row/slice pitches and ticket completion.
NOLINT_TEST_F(
  UploadCoordinatorTest, TextureCube_FullUpload_RecordsRegionAndCompletes)
{
  // Arrange
  // 64x64 RGBA8: row = 64*4 = 256 (aligned); slice = 256*64 = 16384
  TextureDesc tex_desc {
    .width = 64,
    .height = 64,
    .depth = 1,
    .array_size = 6,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTextureCube,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  constexpr uint64_t row_pitch = 256;
  constexpr uint64_t slice_pitch = row_pitch * 64; // 16384
  std::vector<std::byte> data(slice_pitch);

  UploadRequest req {
    .kind = UploadKind::kTextureCube,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexCubeFull",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 64,
      .height = 64,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {
      {
        .mip = 0,
        .array_slice = 0, // single face
        .x = 0,
        .y = 0,
        .z = 0,
        .width = 0,
        .height = 0,
        .depth = 0,
        .row_pitch = 0,
        .slice_pitch = 0,
      },
    },
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data.data(), data.size()),
    },
  };

  auto& uploader = Uploader();

  // Act
  auto ticket = uploader.Submit(req, Staging());
  uploader.Flush();
  uploader.RetireCompleted();

  // Assert
  const auto& log = GfxPtr()->texture_log_;
  ASSERT_TRUE(log.copy_called);
  ASSERT_NE(log.dst, nullptr);
  EXPECT_EQ(log.dst, tex.get());
  ASSERT_EQ(log.regions.size(), 1u);

  const auto& r = log.regions.front();
  EXPECT_EQ(r.buffer_row_pitch, row_pitch);
  EXPECT_EQ(r.buffer_slice_pitch, slice_pitch);
  EXPECT_EQ(r.buffer_offset % 512u, 0u);
  EXPECT_EQ(r.dst_slice.mip_level, 0u);
  EXPECT_EQ(r.dst_slice.array_slice, 0u);

  EXPECT_TRUE(uploader.IsComplete(ticket));
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_EQ(res->bytes_uploaded, slice_pitch);
}

//! Producer returns false for a cube face upload: no copy recorded and an
//! immediate failed ticket with UploadError::kProducerFailed.
NOLINT_TEST_F(
  UploadCoordinatorTest, TextureCube_FullUpload_ProducerFails_NoCopy)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 32,
    .height = 32,
    .depth = 1,
    .array_size = 6,
    .mip_levels = 8,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTextureCube,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req {
    .kind = UploadKind::kTextureCube,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexCubeProdFail",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 32,
      .height = 32,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {
      {
        .mip = 0,
        .array_slice = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .width = 0,
        .height = 0,
        .depth = 0,
        .row_pitch = 0,
        .slice_pitch = 0,
      },
    },
    .data = std::move(prod),
  };

  auto& uploader = Uploader();

  // Act
  auto ticket = uploader.Submit(req, Staging());
  uploader.Flush();
  uploader.RetireCompleted();

  // Assert
  EXPECT_TRUE(producer_ran);
  const auto& log = GfxPtr()->texture_log_;
  EXPECT_FALSE(log.copy_called);

  ASSERT_TRUE(uploader.IsComplete(ticket));
  auto res = uploader.TryGetResult(ticket);
  if (!res.has_value()) {
    FAIL() << "expected a value";
  }
  EXPECT_FALSE(res->success);
  EXPECT_EQ(res->error, oxygen::engine::upload::UploadError::kProducerFailed);
  EXPECT_EQ(res->bytes_uploaded, 0u);
}

} // namespace
