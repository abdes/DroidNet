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
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Renderer/Test/Fakes/Graphics.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

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
using oxygen::renderer::testing::FakeGraphics;
using oxygen::renderer::testing::TextureCommandLog;

class FakeTextureCube final : public Texture {
  OXYGEN_TYPED(FakeTextureCube)
public:
  FakeTextureCube(
    std::string_view name, uint32_t w, uint32_t h, oxygen::Format fmt)
    : Texture(name)
  {
    desc_.width = w;
    desc_.height = h;
    desc_.depth = 1;
    desc_.array_size = 6;
    desc_.format = fmt;
    desc_.mip_levels = 8;
    desc_.texture_type = oxygen::TextureType::kTextureCube;
  }

  [[nodiscard]] auto GetDescriptor() const -> const TextureDesc& override
  {
    return desc_;
  }
  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeResource override
  {
    return oxygen::graphics::NativeResource(
      const_cast<FakeTextureCube*>(this), Texture::ClassTypeId());
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

//! Full cube face upload (single face selected by array_slice=0): verifies one
//! region and correct row/slice pitches and ticket completion.
NOLINT_TEST(UploadCoordinator, TextureCube_FullUpload_RecordsRegionAndCompletes)
{
  auto gfx = std::make_shared<FakeGraphics>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  // 64x64 RGBA8: row = 64*4 = 256 (aligned); slice = 256*64 = 16384
  auto tex = std::make_shared<FakeTextureCube>(
    "DstTexCube", 64, 64, oxygen::Format::kRGBA8UNorm);
  constexpr uint64_t row_pitch = 256;
  constexpr uint64_t slice_pitch = row_pitch * 64; // 16384
  std::vector<std::byte> data(slice_pitch);

  UploadRequest req { .kind = UploadKind::kTextureCube,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexCubeFull",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 64,
      .height = 64,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {
      { .mip = 0,
        .array_slice = 0, // single face
        .x = 0,
        .y = 0,
        .z = 0,
        .width = 0,
        .height = 0,
        .depth = 0,
        .row_pitch = 0,
        .slice_pitch = 0 },
    },
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
  EXPECT_EQ(res->bytes_uploaded, slice_pitch);

  gfx->Flush();
}

} // namespace

//===----------------------------------------------------------------------===//
// Additional failure-path tests
//===----------------------------------------------------------------------===//

//! Producer returns false for a cube face upload: no copy recorded and an
//! immediate failed ticket with UploadError::kProducerFailed.
NOLINT_TEST(UploadCoordinator, TextureCube_FullUpload_ProducerFails_NoCopy)
{
  auto gfx = std::make_shared<FakeGraphics>();
  gfx->CreateCommandQueues(oxygen::graphics::SingleQueueStrategy());

  auto tex = std::make_shared<FakeTextureCube>(
    "DstTexCubeProdFail", 32, 32, oxygen::Format::kRGBA8UNorm);

  bool producer_ran = false;
  std::move_only_function<bool(std::span<std::byte>)> prod
    = [&producer_ran](std::span<std::byte>) -> bool {
    producer_ran = true;
    return false;
  };

  UploadRequest req { .kind = UploadKind::kTextureCube,
    .batch_policy = {},
    .priority = {},
    .debug_name = "TexCubeProdFail",
    .desc = UploadTextureDesc { .dst = tex,
      .width = 32,
      .height = 32,
      .depth = 1,
      .format = oxygen::Format::kRGBA8UNorm },
    .subresources = {
      { .mip = 0, .array_slice = 0, .x = 0, .y = 0, .z = 0, .width = 0, .height = 0, .depth = 0, .row_pitch = 0, .slice_pitch = 0 },
    },
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
