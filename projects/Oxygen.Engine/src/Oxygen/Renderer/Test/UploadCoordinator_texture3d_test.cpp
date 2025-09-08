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
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace {
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

class FakeTexture3D final : public Texture {
  OXYGEN_TYPED(FakeTexture3D)
public:
  FakeTexture3D(std::string_view name, uint32_t w, uint32_t h, uint32_t d,
    oxygen::Format fmt)
    : Texture(name)
  {
    desc_.width = w;
    desc_.height = h;
    desc_.depth = d;
    desc_.format = fmt;
    desc_.mip_levels = 6;
    desc_.texture_type = oxygen::TextureType::kTexture3D;
  }

  [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override
  {
    return desc_;
  }

  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeObject override
  {
    return oxygen::graphics::NativeObject(const_cast<FakeTexture3D*>(this),
      oxygen::graphics::Texture::ClassTypeId());
  }

protected:
  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::TextureType, oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  [[nodiscard]] auto CreateRenderTargetView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  [[nodiscard]] auto CreateDepthStencilView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::TextureSubResourceSet, bool) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }

private:
  TextureDesc desc_ {};
};

//! Full 3D texture upload: verifies one region and correct row/slice pitches,
//! and that the ticket completes with total bytes = slice_pitch * depth.
NOLINT_TEST(UploadCoordinator, Texture3D_FullUpload_RecordsRegionAndCompletes)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  // 32x16x8 RGBA8: row = 32*4=128 -> aligned to 256; slice = 256*16=4096;
  // total = 4096 * 8 = 32768
  auto tex = std::make_shared<FakeTexture3D>(
    "DstTex3D", 32, 16, 8, oxygen::Format::kRGBA8UNorm);
  const uint64_t row_pitch = 256;
  const uint64_t slice_pitch = row_pitch * 16; // 4096
  const uint64_t total = slice_pitch * 8; // 32768
  std::vector<std::byte> data(static_cast<size_t>(total));

  UploadRequest req { .kind = UploadKind::kTexture3D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "Tex3DFull",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 32,
      .height = 16,
      .depth = 8,
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
  EXPECT_EQ(r.buffer_row_pitch, row_pitch);
  EXPECT_EQ(r.buffer_slice_pitch, slice_pitch);
  EXPECT_EQ(r.buffer_offset % 512u, 0u);
  EXPECT_EQ(r.dst_slice.mip_level, 0u);
  EXPECT_EQ(r.dst_slice.array_slice, 0u);

  EXPECT_TRUE(coord.IsComplete(ticket));
  auto res = coord.TryGetResult(ticket);
  ASSERT_TRUE(res.has_value());
  EXPECT_EQ(res->bytes_uploaded, total);

  gfx->Flush();
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional failure-path tests
//===----------------------------------------------------------------------===//

//! Producer returns false for a full 3D texture upload: no copy recorded and
//! an immediate failed ticket with UploadError::kProducerFailed.
NOLINT_TEST(UploadCoordinator, Texture3D_FullUpload_ProducerFails_NoCopy)
{
  auto gfx = std::make_shared<FakeGraphics_Texture>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTexture3D>(
    "DstTex3DProdFail", 16, 8, 4, oxygen::Format::kRGBA8UNorm);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req { .kind = UploadKind::kTexture3D,
    .batch_policy = {},
    .priority = {},
    .debug_name = "Tex3DProdFail",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 16,
      .height = 8,
      .depth = 4,
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
