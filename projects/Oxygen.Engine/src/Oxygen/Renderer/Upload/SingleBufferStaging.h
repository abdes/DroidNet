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
class SingleBufferStaging final : public StagingProvider {
public:
  enum class MapPolicy : uint8_t { kPinned, kPerOp };

  explicit SingleBufferStaging(std::shared_ptr<oxygen::Graphics> gfx,
    MapPolicy policy = MapPolicy::kPinned, float slack = 0.5f)
    : gfx_(std::move(gfx))
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
  void EnsureCapacity_(uint64_t desired, std::string_view name)
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

    oxygen::graphics::BufferDesc desc;
    desc.size_bytes = size_bytes;
    desc.usage = oxygen::graphics::BufferUsage::kNone;
    desc.memory = oxygen::graphics::BufferMemory::kUpload;
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

  std::byte* Map_()
  {
    if (!buffer_)
      return nullptr;
    if (!buffer_->IsMapped()) {
      mapped_ptr_ = static_cast<std::byte*>(buffer_->Map());
      stats_.map_calls++;
    }
    return mapped_ptr_;
  }

  void Unmap_()
  {
    if (buffer_ && buffer_->IsMapped()) {
      buffer_->UnMap();
      stats_.unmap_calls++;
    }
    mapped_ptr_ = nullptr;
  }

  std::shared_ptr<oxygen::Graphics> gfx_;
  MapPolicy policy_ { MapPolicy::kPinned };
  float slack_ { 0.5f };
  std::shared_ptr<oxygen::graphics::Buffer> buffer_;
  std::byte* mapped_ptr_ { nullptr };
  StagingStats stats_ {};
};

} // namespace oxygen::engine::upload
