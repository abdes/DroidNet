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

//! Full 3D texture upload: verifies one region and correct row/slice pitches,
//! and that the ticket completes with total bytes = slice_pitch * depth.
NOLINT_TEST_F(
  UploadCoordinatorTest, Texture3D_FullUpload_RecordsRegionAndCompletes)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 32,
    .height = 16,
    .depth = 8,
    .array_size = 1,
    .mip_levels = 6,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture3D,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  // 32x16x8 RGBA8: row = 32*4=128 -> aligned to 256; slice = 256*16=4096;
  // total = 4096 * 8 = 32768
  constexpr uint64_t row_pitch = 256;
  constexpr uint64_t slice_pitch = row_pitch * 16; // 4096
  constexpr uint64_t total = slice_pitch * 8; // 32768
  std::vector<std::byte> data(total);

  UploadRequest req {
    .kind = UploadKind::kTexture3D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "Tex3DFull",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 32,
      .height = 16,
      .depth = 8,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {},
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
  EXPECT_EQ(res->bytes_uploaded, total);
}

//! Producer returns false for a full 3D texture upload: no copy recorded and
//! an immediate failed ticket with UploadError::kProducerFailed.
NOLINT_TEST_F(UploadCoordinatorTest, Texture3D_FullUpload_ProducerFails_NoCopy)
{
  // Arrange
  TextureDesc tex_desc {
    .width = 16,
    .height = 8,
    .depth = 4,
    .array_size = 1,
    .mip_levels = 6,
    .sample_count = 1,
    .sample_quality = 0,
    .format = oxygen::Format::kRGBA8UNorm,
    .texture_type = oxygen::TextureType::kTexture3D,
  };
  auto tex = GfxPtr()->CreateTexture(tex_desc);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req {
    .kind = UploadKind::kTexture3D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "Tex3DProdFail",
    .desc = UploadTextureDesc {
      .dst = tex,
      .width = 16,
      .height = 8,
      .depth = 4,
      .format = oxygen::Format::kRGBA8UNorm,
    },
    .subresources = {},
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
