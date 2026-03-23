//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/ResourceAccessMode.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Direct3D12/Test/Fixtures/ReadbackTestFixture.h>

namespace {

using oxygen::SizeBytes;
using oxygen::TextureType;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;
using oxygen::graphics::ResourceAccessMode;
using oxygen::graphics::ResourceStates;
using oxygen::graphics::TextureBufferCopyRegion;
using oxygen::graphics::TextureDesc;
using oxygen::graphics::TextureSlice;
using oxygen::graphics::TextureUploadRegion;
using oxygen::graphics::d3d12::testing::ReadbackTestFixture;

constexpr auto kD3D12RowPitch = 256u;

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

auto ReadBytes(const std::byte* data, const size_t size) -> std::vector<uint8_t>
{
  std::vector<uint8_t> bytes(size);
  for (size_t index = 0; index < size; ++index) {
    bytes[index] = static_cast<uint8_t>(data[index]);
  }
  return bytes;
}

auto WriteFloatTexel(std::vector<std::byte>& buffer, const uint32_t row_pitch,
  const uint32_t x, const uint32_t y, const float value) -> void
{
  const auto offset = static_cast<size_t>(y) * row_pitch + (x * sizeof(float));
  std::memcpy(buffer.data() + offset, &value, sizeof(value));
}

auto ReadFloats(const std::byte* data, const size_t count) -> std::vector<float>
{
  std::vector<float> values(count);
  std::memcpy(values.data(), data, count * sizeof(float));
  return values;
}

class D3D12TextureReadbackIntegrationTest : public ReadbackTestFixture { };

NOLINT_TEST_F(D3D12TextureReadbackIntegrationTest,
  CpuReadbackTextureCreatesBufferBackedReadbackSurface)
{
  TextureDesc desc {};
  desc.width = 7;
  desc.height = 5;
  desc.array_size = 2;
  desc.mip_levels = 2;
  desc.format = oxygen::Format::kRGBA8UNorm;
  desc.texture_type = TextureType::kTexture2DArray;
  desc.cpu_access = ResourceAccessMode::kReadBack;
  desc.debug_name = "readback-surface";

  auto texture = CreateD3D12Texture(desc);
  ASSERT_NE(texture, nullptr);
  EXPECT_TRUE(texture->IsReadbackSurface());
  EXPECT_EQ(texture->GetDescriptor().cpu_access, ResourceAccessMode::kReadBack);

  const auto& layout = texture->GetReadbackSurfaceLayout();
  EXPECT_EQ(layout.subresource_count, desc.array_size * desc.mip_levels);
  EXPECT_EQ(layout.subresources.size(), desc.array_size * desc.mip_levels);
  EXPECT_GT(layout.total_bytes, 0u);
  EXPECT_GT(
    layout.subresources.front().placed_footprint.Footprint.RowPitch, 0u);

  auto* native = texture->GetNativeResource()->AsPointer<ID3D12Resource>();
  ASSERT_NE(native, nullptr);
  const auto native_desc = native->GetDesc();
  EXPECT_EQ(native_desc.Dimension, D3D12_RESOURCE_DIMENSION_BUFFER);
  EXPECT_EQ(native_desc.Format, DXGI_FORMAT_UNKNOWN);
  EXPECT_EQ(native_desc.Width, layout.total_bytes);
}

NOLINT_TEST_F(D3D12TextureReadbackIntegrationTest,
  CopyTextureToBufferCopiesTextureSubregionIntoMappedReadbackBuffer)
{
  TextureDesc texture_desc {};
  texture_desc.width = 4;
  texture_desc.height = 4;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "copy-source-texture";

  auto texture = CreateRegisteredTexture(texture_desc);
  auto upload = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = kD3D12RowPitch * texture_desc.height,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "copy-source-upload",
  });
  auto readback = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = 1024,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = "copy-readback-buffer",
  });

  std::vector<std::byte> upload_bytes(
    kD3D12RowPitch * texture_desc.height, std::byte { 0 });
  for (uint32_t y = 0; y < texture_desc.height; ++y) {
    for (uint32_t x = 0; x < texture_desc.width; ++x) {
      WriteRgbaTexel(upload_bytes, kD3D12RowPitch, x, y,
        { static_cast<uint8_t>(10u * y + x), static_cast<uint8_t>(100u + x),
          static_cast<uint8_t>(150u + y), static_cast<uint8_t>(200u + x + y) });
    }
  }
  upload->Update(upload_bytes.data(), upload_bytes.size(), 0);

  const TextureUploadRegion upload_region {
    .buffer_offset = 0,
    .buffer_row_pitch = kD3D12RowPitch,
    .buffer_slice_pitch = kD3D12RowPitch * texture_desc.height,
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
  const TextureBufferCopyRegion readback_region {
    .buffer_offset = oxygen::OffsetBytes { 512 },
    .buffer_row_pitch = SizeBytes { kD3D12RowPitch },
    .texture_slice = {
      .x = 1,
      .y = 1,
      .z = 0,
      .width = 2,
      .height = 2,
      .depth = 1,
      .mip_level = 0,
      .array_slice = 0,
    },
  };

  {
    auto recorder = AcquireRecorder("texture-readback-copy");
    ASSERT_NE(recorder, nullptr);

    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);

    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload, upload_region, *texture);

    recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
    recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyTextureToBuffer(*readback, *texture, readback_region);
  }

  WaitForQueueIdle();

  auto* mapped = static_cast<const std::byte*>(
    readback->Map(readback_region.buffer_offset.get(), kD3D12RowPitch * 2u));
  ASSERT_NE(mapped, nullptr);

  const auto first_row = ReadBytes(mapped, 8);
  const auto second_row = ReadBytes(mapped + kD3D12RowPitch, 8);
  readback->UnMap();

  EXPECT_EQ(first_row,
    (std::vector<uint8_t> { 11u, 101u, 151u, 202u, 12u, 102u, 151u, 203u }));
  EXPECT_EQ(second_row,
    (std::vector<uint8_t> { 21u, 101u, 152u, 203u, 22u, 102u, 152u, 204u }));
}

NOLINT_TEST_F(D3D12TextureReadbackIntegrationTest,
  CopyTextureToBufferCopiesArraySliceIntoMappedReadbackBuffer)
{
  TextureDesc texture_desc {};
  texture_desc.width = 2;
  texture_desc.height = 2;
  texture_desc.array_size = 2;
  texture_desc.texture_type = TextureType::kTexture2DArray;
  texture_desc.format = oxygen::Format::kRGBA8UNorm;
  texture_desc.debug_name = "copy-source-array-texture";

  auto texture = CreateRegisteredTexture(texture_desc);
  auto upload = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = kD3D12RowPitch * texture_desc.height,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "copy-source-array-upload",
  });
  auto readback = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = 1024,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = "copy-array-readback-buffer",
  });

  std::vector<std::byte> upload_bytes(
    kD3D12RowPitch * texture_desc.height, std::byte { 0 });
  WriteRgbaTexel(upload_bytes, kD3D12RowPitch, 0, 0, { 1u, 2u, 3u, 4u });
  WriteRgbaTexel(upload_bytes, kD3D12RowPitch, 1, 0, { 5u, 6u, 7u, 8u });
  WriteRgbaTexel(upload_bytes, kD3D12RowPitch, 0, 1, { 9u, 10u, 11u, 12u });
  WriteRgbaTexel(upload_bytes, kD3D12RowPitch, 1, 1, { 13u, 14u, 15u, 16u });
  upload->Update(upload_bytes.data(), upload_bytes.size(), 0);

  const TextureUploadRegion upload_region {
    .buffer_offset = 0,
    .buffer_row_pitch = kD3D12RowPitch,
    .buffer_slice_pitch = kD3D12RowPitch * texture_desc.height,
    .dst_slice = {
      .x = 0,
      .y = 0,
      .z = 0,
      .width = texture_desc.width,
      .height = texture_desc.height,
      .depth = 1,
      .mip_level = 0,
      .array_slice = 1,
    },
    .dst_subresources = {
      .base_mip_level = 0,
      .num_mip_levels = 1,
      .base_array_slice = 1,
      .num_array_slices = 1,
    },
  };
  const TextureBufferCopyRegion readback_region {
    .buffer_offset = oxygen::OffsetBytes { 512 },
    .buffer_row_pitch = SizeBytes { kD3D12RowPitch },
    .texture_slice = {
      .x = 0,
      .y = 0,
      .z = 0,
      .width = texture_desc.width,
      .height = texture_desc.height,
      .depth = 1,
      .mip_level = 0,
      .array_slice = 1,
    },
  };

  {
    auto recorder = AcquireRecorder("texture-readback-array-slice");
    ASSERT_NE(recorder, nullptr);

    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);

    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload, upload_region, *texture);

    recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
    recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyTextureToBuffer(*readback, *texture, readback_region);
  }

  WaitForQueueIdle();

  auto* mapped = static_cast<const std::byte*>(
    readback->Map(readback_region.buffer_offset.get(), kD3D12RowPitch * 2u));
  ASSERT_NE(mapped, nullptr);
  const auto first_row = ReadBytes(mapped, 8);
  const auto second_row = ReadBytes(mapped + kD3D12RowPitch, 8);
  readback->UnMap();

  EXPECT_EQ(
    first_row, (std::vector<uint8_t> { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u }));
  EXPECT_EQ(second_row,
    (std::vector<uint8_t> { 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u }));
}

NOLINT_TEST_F(D3D12TextureReadbackIntegrationTest,
  CopyTextureToBufferCopiesR32FloatSubregionIntoMappedReadbackBuffer)
{
  TextureDesc texture_desc {};
  texture_desc.width = 3;
  texture_desc.height = 2;
  texture_desc.format = oxygen::Format::kR32Float;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "copy-source-r32f-texture";

  auto texture = CreateRegisteredTexture(texture_desc);
  auto upload = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = kD3D12RowPitch * texture_desc.height,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "copy-source-r32f-upload",
  });
  auto readback = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = 1024,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = "copy-r32f-readback-buffer",
  });

  std::vector<std::byte> upload_bytes(
    kD3D12RowPitch * texture_desc.height, std::byte { 0 });
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 0, 0, 1.0F);
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 1, 0, 2.0F);
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 2, 0, 3.0F);
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 0, 1, 4.0F);
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 1, 1, 5.0F);
  WriteFloatTexel(upload_bytes, kD3D12RowPitch, 2, 1, 6.0F);
  upload->Update(upload_bytes.data(), upload_bytes.size(), 0);

  const TextureUploadRegion upload_region {
    .buffer_offset = 0,
    .buffer_row_pitch = kD3D12RowPitch,
    .buffer_slice_pitch = kD3D12RowPitch * texture_desc.height,
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
  const TextureBufferCopyRegion readback_region {
    .buffer_offset = oxygen::OffsetBytes { 512 },
    .buffer_row_pitch = SizeBytes { kD3D12RowPitch },
    .texture_slice = {
      .x = 1,
      .y = 0,
      .z = 0,
      .width = 2,
      .height = 2,
      .depth = 1,
      .mip_level = 0,
      .array_slice = 0,
    },
  };

  {
    auto recorder = AcquireRecorder("texture-readback-r32f");
    ASSERT_NE(recorder, nullptr);

    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);

    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload, upload_region, *texture);

    recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
    recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyTextureToBuffer(*readback, *texture, readback_region);
  }

  WaitForQueueIdle();

  auto* mapped = static_cast<const std::byte*>(
    readback->Map(readback_region.buffer_offset.get(), kD3D12RowPitch * 2u));
  ASSERT_NE(mapped, nullptr);
  const auto first_row = ReadFloats(mapped, 2);
  const auto second_row = ReadFloats(mapped + kD3D12RowPitch, 2);
  readback->UnMap();

  EXPECT_EQ(first_row, (std::vector<float> { 2.0F, 3.0F }));
  EXPECT_EQ(second_row, (std::vector<float> { 5.0F, 6.0F }));
}

NOLINT_TEST_F(D3D12TextureReadbackIntegrationTest,
  CopyTextureToBufferCopiesBlockCompressedRegionIntoMappedReadbackBuffer)
{
  TextureDesc texture_desc {};
  texture_desc.width = 8;
  texture_desc.height = 8;
  texture_desc.format = oxygen::Format::kBC1UNorm;
  texture_desc.texture_type = TextureType::kTexture2D;
  texture_desc.debug_name = "copy-source-bc1-texture";

  auto texture = CreateRegisteredTexture(texture_desc);
  auto upload = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = kD3D12RowPitch * 2u,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kUpload,
    .debug_name = "copy-source-bc1-upload",
  });
  auto readback = CreateRegisteredBuffer(BufferDesc {
    .size_bytes = 1024,
    .usage = BufferUsage::kNone,
    .memory = BufferMemory::kReadBack,
    .debug_name = "copy-bc1-readback-buffer",
  });

  std::vector<std::byte> upload_bytes(kD3D12RowPitch * 2u, std::byte { 0 });
  for (uint32_t index = 0; index < 16u; ++index) {
    upload_bytes[index] = static_cast<std::byte>(0x40u + index);
    upload_bytes[kD3D12RowPitch + index]
      = static_cast<std::byte>(0x50u + index);
  }
  upload->Update(upload_bytes.data(), upload_bytes.size(), 0);

  const TextureUploadRegion upload_region {
    .buffer_offset = 0,
    .buffer_row_pitch = kD3D12RowPitch,
    .buffer_slice_pitch = kD3D12RowPitch * 2u,
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
  const TextureBufferCopyRegion readback_region {
    .buffer_offset = oxygen::OffsetBytes { 512 },
    .buffer_row_pitch = SizeBytes { kD3D12RowPitch },
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

  {
    auto recorder = AcquireRecorder("texture-readback-bc1");
    ASSERT_NE(recorder, nullptr);

    EnsureTracked(*recorder, upload, ResourceStates::kGenericRead);
    EnsureTracked(*recorder, texture, ResourceStates::kCommon);
    EnsureTracked(*recorder, readback, ResourceStates::kCopyDest);

    recorder->RequireResourceState(*upload, ResourceStates::kCopySource);
    recorder->RequireResourceState(*texture, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyBufferToTexture(*upload, upload_region, *texture);

    recorder->RequireResourceState(*texture, ResourceStates::kCopySource);
    recorder->RequireResourceState(*readback, ResourceStates::kCopyDest);
    recorder->FlushBarriers();
    recorder->CopyTextureToBuffer(*readback, *texture, readback_region);
  }

  WaitForQueueIdle();

  auto* mapped = static_cast<const std::byte*>(
    readback->Map(readback_region.buffer_offset.get(), kD3D12RowPitch * 2u));
  ASSERT_NE(mapped, nullptr);
  const auto first_row = ReadBytes(mapped, 8);
  const auto second_row = ReadBytes(mapped + kD3D12RowPitch, 8);
  readback->UnMap();

  EXPECT_EQ(first_row,
    (std::vector<uint8_t> {
      0x48u, 0x49u, 0x4Au, 0x4Bu, 0x4Cu, 0x4Du, 0x4Eu, 0x4Fu }));
  EXPECT_EQ(second_row,
    (std::vector<uint8_t> {
      0x58u, 0x59u, 0x5Au, 0x5Bu, 0x5Cu, 0x5Du, 0x5Eu, 0x5Fu }));
}

} // namespace
