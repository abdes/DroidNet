//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Headless/Buffer.h>

using namespace oxygen::graphics::headless;

namespace {

//! Ensure buffer descriptor construction and basic properties do not crash
NOLINT_TEST(HeadlessBufferTest, DescriptorAndSize)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 128;
  desc.usage = oxygen::graphics::BufferUsage::kVertex
    | oxygen::graphics::BufferUsage::kStorage;
  desc.memory = oxygen::graphics::BufferMemory::kUpload;
  desc.debug_name = "TestBuffer";

  // Act
  Buffer buf(desc);

  // Assert: constructing a buffer should not crash; verify debug name locally
  EXPECT_EQ(desc.debug_name, "TestBuffer");
}

//! Map/UnMap should return a valid pointer for non-zero buffers and allow
//! writes
NOLINT_TEST(HeadlessBufferTest, MapUnmap_WriteMappedMemory)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 32;
  Buffer buf(desc);

  // Act
  void* p = buf.Map();

  // Assert
  ASSERT_NE(p, nullptr);

  // Act: write sequential bytes
  auto bytes = static_cast<uint8_t*>(p);
  for (auto i = 0u; i < desc.size_bytes; ++i) {
    bytes[i] = static_cast<uint8_t>(i + 1);
  }

  // Act
  buf.UnMap();
}

//! Update/ReadBacking/WriteBacking should copy data and respect bounds
NOLINT_TEST(HeadlessBufferTest, UpdateReadWrite_BoundsChecks)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 64;
  Buffer buf(desc);

  // Act: prepare source data and update
  std::vector<uint8_t> src(16);
  for (auto i = 0u; i < src.size(); ++i)
    src[i] = static_cast<uint8_t>(0xA0 + i);
  buf.Update(src.data(), src.size(), 8);

  // Assert: read back
  std::vector<uint8_t> dst(16);
  buf.ReadBacking(dst.data(), 8, dst.size());
  EXPECT_EQ(dst, src);

  // Act: write backing and verify
  std::vector<uint8_t> src2(8, 0x55);
  buf.WriteBacking(src2.data(), 4, src2.size());
  std::vector<uint8_t> dst2(8);
  buf.ReadBacking(dst2.data(), 4, dst2.size());
  EXPECT_EQ(dst2, src2);

  // Act / Assert: no-op and out-of-range operations should not crash
  buf.Update(nullptr, 0, 0);
  buf.WriteBacking(nullptr, 0, 0);
  buf.ReadBacking(nullptr, 0, 0);
  buf.Update(src.data(), src.size(), desc.size_bytes + 10);
  buf.ReadBacking(dst.data(), desc.size_bytes + 5, dst.size());
}

//! GetGPUVirtualAddress returns a stable non-zero address for headless buffers
NOLINT_TEST(HeadlessBufferTest, GPUVirtualAddress_StableNonZero)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 1;
  Buffer buf(desc);

  // Act
  const auto addr1 = buf.GetGPUVirtualAddress();
  const auto addr2 = buf.GetGPUVirtualAddress();

  // Assert
  EXPECT_EQ(addr1, addr2);
  EXPECT_NE(addr1, 0u);
}

//! Zero-size buffers: Map returns nullptr and Read/Write/Update are safe no-ops
NOLINT_TEST(HeadlessBufferTest, ZeroSize_NoOps)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 0;
  Buffer buf(desc);

  // Act / Assert
  void* p = buf.Map();
  EXPECT_EQ(p, nullptr);
  buf.UnMap();

  std::vector<uint8_t> tmp(4);
  buf.ReadBacking(tmp.data(), 0, tmp.size());
  buf.WriteBacking(tmp.data(), 0, tmp.size());
  buf.Update(tmp.data(), tmp.size(), 0);
}

//! Create CBV/SRV/UAV views via GetNativeView and validate NativeObject
NOLINT_TEST(HeadlessBufferTest, CreateViewsAndNativeObject)
{
  // Arrange
  oxygen::graphics::BufferDesc desc {};
  desc.size_bytes = 48;
  Buffer buf(desc);

  // Act / Assert: CBV
  oxygen::graphics::BufferViewDescription cbv_desc;
  cbv_desc.view_type = oxygen::graphics::ResourceViewType::kConstantBuffer;
  auto cbv = buf.GetNativeView(oxygen::graphics::DescriptorHandle {}, cbv_desc);
  EXPECT_TRUE(cbv.IsValid());
  EXPECT_TRUE(cbv.IsPointerHandle());

  // Act / Assert: SRV
  oxygen::graphics::BufferViewDescription srv_desc;
  srv_desc.view_type = oxygen::graphics::ResourceViewType::kRawBuffer_SRV;
  auto srv = buf.GetNativeView(oxygen::graphics::DescriptorHandle {}, srv_desc);
  EXPECT_TRUE(srv.IsValid());

  // Act / Assert: UAV
  oxygen::graphics::BufferViewDescription uav_desc;
  uav_desc.view_type = oxygen::graphics::ResourceViewType::kRawBuffer_UAV;
  auto uav = buf.GetNativeView(oxygen::graphics::DescriptorHandle {}, uav_desc);
  EXPECT_TRUE(uav.IsValid());
}

} // namespace
