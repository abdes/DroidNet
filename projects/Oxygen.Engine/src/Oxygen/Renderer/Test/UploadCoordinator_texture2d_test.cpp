//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

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
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>

#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

#include <Oxygen/Renderer/Test/Helpers/UploadTestFakes.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
using oxygen::engine::upload::Bytes;
using oxygen::engine::upload::UploadCoordinator;
using oxygen::engine::upload::UploadDataView;
using oxygen::engine::upload::UploadKind;
using oxygen::engine::upload::UploadRequest;
using oxygen::engine::upload::UploadTextureDesc;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::CommandList;
using oxygen::graphics::CommandQueue;
using oxygen::graphics::CommandRecorder;
using oxygen::graphics::QueueKey;
using oxygen::graphics::QueueRole;
using oxygen::graphics::Texture;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureUploadRegion;
using oxygen::tests::uploadhelpers::FakeGraphics_Texture;

// --- Minimal test fakes --------------------------------------------------//

class FakeTexture final : public Texture {
  OXYGEN_TYPED(FakeTexture)
public:
  FakeTexture(std::string_view name, uint32_t w, uint32_t h, oxygen::Format fmt)
    : Texture(name)
  {
    desc_.width = w;
    desc_.height = h;
    desc_.format = fmt;
    desc_.mip_levels = 8;
    desc_.texture_type = oxygen::TextureType::kTexture2D;
  }

  [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override
  {
    return desc_;
  }

  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeResource override
  {
    return oxygen::graphics::NativeResource(
      const_cast<FakeTexture*>(this), Texture::ClassTypeId());
  }

protected:
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateRenderTargetView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }
  [[nodiscard]] auto CreateDepthStencilView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet, bool) const
    -> oxygen::graphics::NativeView override
  {
    return {};
  }

private:
  TextureDesc desc_ {};
};

//! Single full-texture upload: verifies one region with aligned row/slice
//! pitches.
NOLINT_TEST(UploadCoordinator, Texture2D_FullUpload_RecordsRegionAndCompletes)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTexture>(
    "DstTex", 128, 64, oxygen::Format::kRGBA8UNorm);

  // Provide enough bytes; exact content is irrelevant for this test
  // 128*64*4 = 32768; aligned row pitch will be 512? Actually 128*4=512,
  // alignment keeps 512
  std::vector<std::byte> data(32768);

  UploadRequest req { .kind = UploadKind::kTexture2D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexUploadFull",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 128,
      .height = 64,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {},
    .data = UploadDataView {
      .bytes = std::span<const std::byte>(data.data(), data.size()) } };

  UploadCoordinator coord(*gfx);

  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  const auto& log = gfx->texture_log_;
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

  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->bytes_uploaded, 32768u);

  // Cleanup deferred releases
  gfx->Flush();
}

//! Multi-subresource upload: verifies two regions with proper pitches and
//! placement alignment.
NOLINT_TEST(UploadCoordinator, Texture2D_MipChainTwoRegions_AlignedOffsets)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTexture>(
    "DstTex2", 64, 32, oxygen::Format::kRGBA8UNorm);

  // Expected pitches: mip0 row=64*4=256 (already aligned), slice=256*32=8192
  // mip1 row=32*4=128 -> aligned to 256, slice=256*16=4096; offsets: 0, 8192
  constexpr uint64_t total = 8192 + 4096; // 12288
  std::vector<std::byte> data(total);

  UploadRequest req {
    .kind = UploadKind::kTexture2D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexUploadMips",
    .desc = UploadTextureDesc{ .dst = tex, .width = 64, .height = 32, .depth = 1, .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {
      { .mip = 0, .array_slice = 0, .x = 0, .y = 0, .z = 0, .width = 0, .height = 0, .depth = 0, .row_pitch = 0, .slice_pitch = 0 },
      { .mip = 1, .array_slice = 0, .x = 0, .y = 0, .z = 0, .width = 0, .height = 0, .depth = 0, .row_pitch = 0, .slice_pitch = 0 },
    },
    .data = UploadDataView{ .bytes = std::span<const std::byte>(data.data(), data.size()) }
  };

  UploadCoordinator coord(*gfx);
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  const auto& log = gfx->texture_log_;
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

  // Ticket completion
  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->bytes_uploaded, total);

  // Cleanup deferred releases
  gfx->Flush();
}

//! Full Texture2D upload using a producer callback; verifies region pitches and
//! completion.
NOLINT_TEST(UploadCoordinator, Texture2D_FullUpload_WithProducer_Completes)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTexture>(
    "DstTexProd", 128, 64, oxygen::Format::kRGBA8UNorm);

  constexpr uint32_t w = 128;
  constexpr uint32_t h = 64;
  constexpr uint32_t bpp = 4; // RGBA8
  constexpr uint64_t expected_row = w * bpp; // 512, already aligned
  constexpr uint64_t expected_slice = expected_row * h; // 32768

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte> out) -> bool {
    producer_ran = true;
    std::memset(out.data(), 0x7F, out.size());
    return true;
  };

  UploadRequest req { .kind = UploadKind::kTexture2D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexUploadFullProd",
    .desc = UploadTextureDesc { .dst = tex,
      .width = w,
      .height = h,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {},
    .data = std::move(prod) };

  UploadCoordinator coord(*gfx);
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  EXPECT_TRUE(producer_ran);

  const auto& log = gfx->texture_log_;
  ASSERT_TRUE(log.copy_called);
  ASSERT_EQ(log.regions.size(), 1u);
  const auto& r = log.regions.front();
  EXPECT_EQ(r.buffer_row_pitch, expected_row);
  EXPECT_EQ(r.buffer_slice_pitch, expected_slice);
  EXPECT_EQ(r.buffer_offset % 512u, 0u);

  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->bytes_uploaded, expected_slice);

  gfx->Flush();
}

//! Producer returns false: no CopyBufferToTexture and failed result.
NOLINT_TEST(UploadCoordinator, Texture2D_FullUpload_ProducerFails_NoCopy)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTexture>(
    "DstTexProdFail", 64, 32, oxygen::Format::kRGBA8UNorm);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req { .kind = UploadKind::kTexture2D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexProdFail",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 64,
      .height = 32,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {},
    .data = std::move(prod) };

  UploadCoordinator coord(*gfx);
  auto ticket = coord.Submit(req);
  coord.Flush();
  coord.RetireCompleted();

  EXPECT_TRUE(producer_ran);
  const auto& log = gfx->texture_log_;
  EXPECT_FALSE(log.copy_called);

  ASSERT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_FALSE(res->success);
  EXPECT_EQ(res->error, oxygen::engine::upload::UploadError::kProducerFailed);
  EXPECT_EQ(res->bytes_uploaded, 0u);

  gfx->Flush();
}

} // namespace
