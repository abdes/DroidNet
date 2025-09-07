//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/CommandList.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingAllocator.h>
#include <Oxygen/Testing/GTest.h>

#include <cstring>
#include <vector>

namespace {
using oxygen::engine::upload::Bytes;
using oxygen::engine::upload::StagingAllocator;
using oxygen::graphics::Buffer;
using oxygen::graphics::BufferDesc;
using oxygen::graphics::BufferMemory;
using oxygen::graphics::BufferUsage;

// Minimal fake Buffer to satisfy StagingAllocator contract without backends.
class FakeBuffer : public Buffer {
public:
  FakeBuffer(std::string_view name, uint64_t size)
    : Buffer(name)
  {
    desc_.size_bytes = size;
    desc_.usage = BufferUsage::kNone;
    desc_.memory = BufferMemory::kUpload;
  }

  // NOLINTBEGIN(modernize-use-trailing-return-type)
  [[nodiscard]] auto GetDescriptor() const noexcept -> BufferDesc override
  {
    return desc_;
  }
  [[nodiscard]] auto GetNativeResource() const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  auto Map(uint64_t offset = 0, uint64_t size = 0) -> void* override
  {
    (void)offset;
    if (mapped_)
      return mapped_ptr_;
    const auto bytes = size == 0 ? desc_.size_bytes : size;
    storage_.resize(static_cast<size_t>(bytes));
    mapped_ptr_ = storage_.data();
    mapped_ = true;
    return mapped_ptr_;
  }
  auto UnMap() -> void override
  {
    mapped_ = false;
    mapped_ptr_ = nullptr;
    storage_.clear();
  }
  auto Update(const void* data, uint64_t size, uint64_t offset) -> void override
  {
    auto b = static_cast<const std::byte*>(data);
    if (offset + size <= storage_.size()) {
      std::memcpy(storage_.data() + offset, b, static_cast<size_t>(size));
    }
  }
  [[nodiscard]] auto GetSize() const noexcept -> uint64_t override
  {
    return desc_.size_bytes;
  }
  [[nodiscard]] auto GetUsage() const noexcept -> BufferUsage override
  {
    return desc_.usage;
  }
  [[nodiscard]] auto GetMemoryType() const noexcept -> BufferMemory override
  {
    return desc_.memory;
  }
  [[nodiscard]] auto IsMapped() const noexcept -> bool override
  {
    return mapped_;
  }
  [[nodiscard]] auto GetGPUVirtualAddress() const -> uint64_t override
  {
    return 0;
  }

protected:
  [[nodiscard]] auto CreateConstantBufferView(
    const oxygen::graphics::DescriptorHandle&,
    const oxygen::graphics::BufferRange&) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }

  [[nodiscard]] auto CreateShaderResourceView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }

  [[nodiscard]] auto CreateUnorderedAccessView(
    const oxygen::graphics::DescriptorHandle&, oxygen::Format,
    oxygen::graphics::BufferRange, uint32_t) const
    -> oxygen::graphics::NativeObject override
  {
    return {};
  }
  // NOLINTEND

private:
  BufferDesc desc_ {};
  bool mapped_ { false };
  std::byte* mapped_ptr_ { nullptr };
  std::vector<std::byte> storage_ {};
};

class FakeGraphics : public oxygen::Graphics {
public:
  FakeGraphics()
    : oxygen::Graphics("FakeGraphics")
  {
  }

  // Only CreateBuffer is needed for StagingAllocator tests.
  // NOLINTBEGIN(modernize-use-trailing-return-type)
  [[nodiscard]] auto CreateSurface(std::weak_ptr<oxygen::platform::Window>,
    oxygen::observer_ptr<oxygen::graphics::CommandQueue>) const
    -> std::shared_ptr<oxygen::graphics::Surface> override
  {
    return {};
  }
  [[nodiscard]] auto GetShader(std::string_view) const
    -> std::shared_ptr<oxygen::graphics::IShaderByteCode> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTexture(const oxygen::graphics::TextureDesc&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateTextureFromNativeObject(
    const oxygen::graphics::TextureDesc&,
    const oxygen::graphics::NativeObject&) const
    -> std::shared_ptr<oxygen::graphics::Texture> override
  {
    return {};
  }
  [[nodiscard]] auto CreateBuffer(
    const oxygen::graphics::BufferDesc& desc) const
    -> std::shared_ptr<oxygen::graphics::Buffer> override
  {
    return std::make_shared<FakeBuffer>("Staging", desc.size_bytes);
  }

  // Pure virtual required by base Graphics returning bindless facilities.
  auto GetDescriptorAllocator() const
    -> const oxygen::graphics::DescriptorAllocator& override
  {
    static const oxygen::graphics::DescriptorAllocator* dummy = nullptr;
    return *dummy; // not used in this test
  }

protected:
  [[nodiscard]] auto CreateCommandQueue(
    const oxygen::graphics::QueueKey&, oxygen::graphics::QueueRole)
    -> std::shared_ptr<oxygen::graphics::CommandQueue> override
  {
    return {};
  }

  [[nodiscard]] auto CreateCommandListImpl(oxygen::graphics::QueueRole,
    std::string_view) -> std::unique_ptr<oxygen::graphics::CommandList> override
  {
    return {};
  }

  [[nodiscard]] auto CreateCommandRecorder(
    std::shared_ptr<oxygen::graphics::CommandList>,
    oxygen::observer_ptr<oxygen::graphics::CommandQueue>)
    -> std::unique_ptr<oxygen::graphics::CommandRecorder> override
  {
    return {};
  }
  // NOLINTEND
};

//! Verify Allocate maps a persistently mapped upload buffer with correct size.
NOLINT_TEST(UploadStagingAllocator, AllocateMapsAndSizes)
{
  auto gfx = std::make_shared<FakeGraphics>();
  StagingAllocator alloc(gfx);

  const uint64_t size = 1024;
  auto a = alloc.Allocate(Bytes { size }, "alloc1");

  ASSERT_NE(a.buffer, nullptr);
  EXPECT_EQ(a.size, size);
  EXPECT_EQ(a.offset, 0u);
  ASSERT_NE(a.ptr, nullptr);
  EXPECT_TRUE(a.buffer->IsMapped());
}

//! Verify Allocation dtor unmaps the buffer to prevent leaks.
NOLINT_TEST(UploadStagingAllocator, AllocationUnmapsOnDestruct)
{
  auto gfx = std::make_shared<FakeGraphics>();
  StagingAllocator alloc(gfx);
  std::weak_ptr<Buffer> weak;

  {
    auto a = alloc.Allocate(Bytes { 512 }, "alloc2");
    weak = a.buffer;
    ASSERT_FALSE(weak.expired());
    ASSERT_TRUE(a.buffer->IsMapped());
  }

  // After a goes out of scope, buffer should still exist (held by weak?
  // maybe expired) but if present it must be unmapped.
  if (!weak.expired()) {
    auto buf = weak.lock();
    EXPECT_FALSE(buf->IsMapped());
  }
}

//! Verify multiple allocations are independent and correctly sized/mapped.
NOLINT_TEST(UploadStagingAllocator, MultipleAllocationsAreIndependent)
{
  auto gfx = std::make_shared<FakeGraphics>();
  StagingAllocator alloc(gfx);

  const uint64_t s1 = 256;
  const uint64_t s2 = 1024;
  const uint64_t s3 = 4096;

  auto a1 = alloc.Allocate(Bytes { s1 }, "a1");
  auto a2 = alloc.Allocate(Bytes { s2 }, "a2");
  auto a3 = alloc.Allocate(Bytes { s3 }, "a3");

  ASSERT_NE(a1.buffer, nullptr);
  ASSERT_NE(a2.buffer, nullptr);
  ASSERT_NE(a3.buffer, nullptr);

  EXPECT_EQ(a1.size, s1);
  EXPECT_EQ(a2.size, s2);
  EXPECT_EQ(a3.size, s3);

  ASSERT_NE(a1.ptr, nullptr);
  ASSERT_NE(a2.ptr, nullptr);
  ASSERT_NE(a3.ptr, nullptr);

  // Distinct buffers and mappings
  EXPECT_NE(a1.buffer.get(), a2.buffer.get());
  EXPECT_NE(a2.buffer.get(), a3.buffer.get());
  EXPECT_NE(a1.buffer.get(), a3.buffer.get());
  EXPECT_NE(a1.ptr, a2.ptr);
  EXPECT_NE(a2.ptr, a3.ptr);
  EXPECT_NE(a1.ptr, a3.ptr);

  EXPECT_TRUE(a1.buffer->IsMapped());
  EXPECT_TRUE(a2.buffer->IsMapped());
  EXPECT_TRUE(a3.buffer->IsMapped());
}

//! Verify size edge cases: zero-size and a reasonably large allocation.
NOLINT_TEST(UploadStagingAllocator, SizeEdgeCases_ZeroAndLarge)
{
  auto gfx = std::make_shared<FakeGraphics>();
  StagingAllocator alloc(gfx);

  // Zero-size allocation: allow size==0, mapping may be nullptr but IsMapped
  // should be true.
  auto zero = alloc.Allocate(Bytes { 0 }, "zero");
  EXPECT_EQ(zero.size, 0u);
  EXPECT_EQ(zero.offset, 0u);
  EXPECT_TRUE(zero.buffer->IsMapped());
  // ptr can be null in our fake depending on how Map() handles 0; accept
  // either.

  // Large allocation: keep it moderate to avoid test flakiness (8 MiB)
  const uint64_t big = 8ull * 1024 * 1024;
  auto large = alloc.Allocate(Bytes { big }, "large");
  ASSERT_NE(large.buffer, nullptr);
  ASSERT_NE(large.ptr, nullptr);
  EXPECT_EQ(large.size, big);
  EXPECT_TRUE(large.buffer->IsMapped());
}

} // namespace
