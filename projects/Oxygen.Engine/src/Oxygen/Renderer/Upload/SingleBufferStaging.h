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
  explicit SingleBufferStaging(UploaderTag tag,
    observer_ptr<oxygen::Graphics> gfx, MapPolicy policy = MapPolicy::kPinned,
    float slack = 0.5f)
    : StagingProvider(tag)
    , gfx_(gfx)
    , policy_(policy)
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
    stats_.allocations++;
    stats_.bytes_requested += out.size;
    return out;
  }

  auto RetireCompleted(FenceValue /*completed*/) -> void override
  {
    if (policy_ == MapPolicy::kPerOp) {
      Unmap_();
    }
  }

  auto GetStats() const -> StagingStats override { return stats_; }

private:
  auto EnsureCapacity_(uint64_t desired, std::string_view name) -> void
  {
    stats_.ensure_capacity_calls++;
    if (buffer_ && buffer_->GetSize() >= desired) {
      stats_.current_buffer_size = buffer_->GetSize();
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
    stats_.buffers_created++;
    stats_.current_buffer_size = buffer_->GetSize();
    stats_.peak_buffer_size
      = std::max(stats_.peak_buffer_size, stats_.current_buffer_size);
    if (policy_ == MapPolicy::kPinned) {
      mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
      stats_.map_calls++;
    } else {
      mapped_ptr_ = nullptr;
    }
  }

  auto Map_() -> std::byte*
  {
    if (!buffer_) {
      return nullptr;
    }
    if (!buffer_->IsMapped()) {
      mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
      stats_.map_calls++;
    }
    return mapped_ptr_;
  }

  auto Unmap_() -> void
  {
    if (buffer_ && buffer_->IsMapped()) {
      buffer_->UnMap();
      stats_.unmap_calls++;
    }
    mapped_ptr_ = nullptr;
  }

  observer_ptr<Graphics> gfx_;
  MapPolicy policy_ { MapPolicy::kPinned };
  float slack_ { 0.5f };
  std::shared_ptr<graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
  StagingStats stats_ {};
};

} // namespace oxygen::engine::upload
