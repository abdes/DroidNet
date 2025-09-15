//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Graphics/Common/Buffer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>

namespace oxygen::engine::upload {

// Single buffer provider with configurable mapping policy.
// Must be created via UploadCoordinator::CreateSingleBufferStaging.
class SingleBufferStaging final : public StagingProvider {
  friend class UploadCoordinator;

public:
  explicit SingleBufferStaging(
    UploaderTag tag, observer_ptr<oxygen::Graphics> gfx, float slack = 0.5f)
    : StagingProvider(tag)
    , gfx_(gfx)
    , slack_(slack)
  {
  }

  auto Allocate(Bytes size, std::string_view debug_name) -> Allocation override
  {
    EnsureCapacity_(size.get(), debug_name);
    if (!buffer_) {
      return {};
    }

    Allocation out;
    out.buffer = buffer_;
    out.offset = 0;
    out.size = size.get();
    out.ptr = mapped_ptr_ ? mapped_ptr_ : Map_();

    // Update telemetry
    UpdateAllocationStats_(size.get());

    return out;
  }

  auto RetireCompleted(UploaderTag, FenceValue /*completed*/) -> void override
  {
    // Always using pinned mapping - nothing to do here
  }

private:
  auto UpdateAllocationStats_(std::uint64_t size) -> void
  {
    Stats().total_allocations++;
    Stats().total_bytes_allocated += size;
    Stats().allocations_this_frame++;

    // Update moving average (simple exponential moving average with alpha=0.1)
    constexpr double alpha = 0.1;
    if (Stats().avg_allocation_size == 0) {
      Stats().avg_allocation_size = static_cast<std::uint32_t>(size);
    } else {
      const auto new_avg = alpha * static_cast<double>(size)
        + (1.0 - alpha) * static_cast<double>(Stats().avg_allocation_size);
      Stats().avg_allocation_size = static_cast<std::uint32_t>(new_avg);
    }
  }

  auto EnsureCapacity_(uint64_t desired, std::string_view name) -> void
  {
    if (buffer_ && buffer_->GetSize() >= desired) {
      Stats().current_buffer_size = buffer_->GetSize();
      return;
    }

    const uint64_t current = buffer_ ? buffer_->GetSize() : 0ull;
    const uint64_t grow
      = current > 0 ? static_cast<uint64_t>(current * (1.0 + slack_)) : desired;
    const uint64_t size_bytes = std::max(desired, grow);

    graphics::BufferDesc desc;
    desc.size_bytes = size_bytes;
    desc.usage = graphics::BufferUsage::kNone;
    desc.memory = graphics::BufferMemory::kUpload;
    desc.debug_name = std::string(name);

    Unmap_();
    buffer_ = gfx_->CreateBuffer(desc);
    Stats().buffer_growth_count++;
    Stats().current_buffer_size = buffer_->GetSize();

    // Always use pinned mapping
    mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
    Stats().map_calls++;
  }

  auto Map_() -> std::byte*
  {
    if (!buffer_) {
      return nullptr;
    }
    if (!buffer_->IsMapped()) {
      mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
      Stats().map_calls++;
    }
    return mapped_ptr_;
  }

  auto Unmap_() -> void
  {
    if (buffer_ && buffer_->IsMapped()) {
      buffer_->UnMap();
      Stats().unmap_calls++;
    }
    mapped_ptr_ = nullptr;
  }

  observer_ptr<Graphics> gfx_;
  float slack_ { 0.5f };
  std::shared_ptr<graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
};

} // namespace oxygen::engine::upload
