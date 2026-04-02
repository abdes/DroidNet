//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Graphics/Common/CommandQueue.h>
#include <Oxygen/Graphics/Common/DescriptorAllocationHandle.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Nexus/Types/Domain.h>

namespace oxygen::nexus::testing {

namespace b = oxygen::bindless;

inline constexpr b::HeapIndex kDefaultBaseIndex { 10U };
inline constexpr b::Count kDefaultRemainingCount { 5U };
inline constexpr uint32_t kDomainHashShift = 16U;

//! Mock descriptor allocator for testing purposes.
class FakeAllocator : public oxygen::graphics::DescriptorAllocator {
public:
  FakeAllocator()
  {
    // provide small default domain capacities via internal map
    bases_[oxygen::bindless::generated::kTexturesDomain.get()]
      = b::ShaderVisibleIndex { kDefaultBaseIndex.get() };
  }

  auto SetBase(oxygen::bindless::DomainToken domain, b::ShaderVisibleIndex base)
    -> void
  {
    bases_[domain.get()] = base;
  }

  auto AllocateRaw(oxygen::graphics::ResourceViewType vt,
    oxygen::graphics::DescriptorVisibility vis)
    -> oxygen::graphics::DescriptorAllocationHandle override
  {
    return CreateRawDescriptorHandle(b::HeapIndex { 0U }, vt, vis);
  }

  auto AllocateBindless(
    oxygen::bindless::DomainToken domain, oxygen::graphics::ResourceViewType vt)
    -> oxygen::graphics::DescriptorAllocationHandle override
  {
    return CreateBindlessHandle(
      oxygen::graphics::DescriptorAllocationHandle::PackBindlessSlot(
        domain, 0U),
      domain, vt);
  }

  auto Release(oxygen::graphics::DescriptorAllocationHandle& /*handle*/)
    -> void override
  {
  }

  auto CopyDescriptor(
    const oxygen::graphics::DescriptorAllocationHandle& /*source*/,
    const oxygen::graphics::DescriptorAllocationHandle& /*destination*/)
    -> void override
  {
  }

  [[nodiscard]] auto GetRemainingDescriptorsCount(
    oxygen::graphics::ResourceViewType /*view_type*/,
    oxygen::graphics::DescriptorVisibility /*vis*/) const -> b::Count override
  {
    return kDefaultRemainingCount;
  }

  [[nodiscard]] auto GetDomainBaseIndex(
    oxygen::bindless::DomainToken domain) const
    -> b::ShaderVisibleIndex override
  {
    auto it = bases_.find(domain.get());
    if (it != bases_.end()) {
      return it->second;
    }
    return b::ShaderVisibleIndex { 0U };
  }

  [[nodiscard]] auto ReserveRaw(
    oxygen::graphics::ResourceViewType /*view_type*/,
    oxygen::graphics::DescriptorVisibility /*vis*/, b::Count /*count*/)
    -> std::optional<b::HeapIndex> override
  {
    return std::nullopt;
  }

  [[nodiscard]] auto Contains(
    const oxygen::graphics::DescriptorAllocationHandle& /*handle*/) const
    -> bool override
  {
    return false;
  }

  [[nodiscard]] auto GetAllocatedDescriptorsCount(
    oxygen::graphics::ResourceViewType /*view_type*/,
    oxygen::graphics::DescriptorVisibility /*visibility*/) const
    -> b::Count override
  {
    return b::Count { 0U };
  }

  [[nodiscard]] auto GetShaderVisibleIndex(
    const oxygen::graphics::DescriptorAllocationHandle& /*handle*/)
    const noexcept -> b::ShaderVisibleIndex override
  {
    return oxygen::kInvalidShaderVisibleIndex;
  }

private:
  std::unordered_map<uint16_t, b::ShaderVisibleIndex> bases_;
};

//! Backend allocator mock that tracks allocations and supports free list reuse.
struct AllocateBackend {
  std::vector<uint32_t> free_list;
  std::atomic<uint32_t> next { 0U };
  std::atomic<int> alloc_count { 0 };

  auto operator()(DomainKey /*domain*/) -> b::HeapIndex
  {
    ++alloc_count;
    if (!free_list.empty()) {
      auto idx = free_list.back();
      free_list.pop_back();
      return b::HeapIndex { idx };
    }
    return b::HeapIndex { next.fetch_add(1U) };
  }
};

//! Backend free function mock that records freed handle indices.
struct FreeBackend {
  std::vector<uint32_t> freed;
  auto operator()(DomainKey /*domain*/, b::HeapIndex h) -> void
  {
    freed.push_back(h.get());
  }
};

//! Test-only CommandQueue implementation for testing timeline-gated
//! reclamation.
struct FakeCommandQueue : oxygen::graphics::CommandQueue {
  mutable std::atomic<uint64_t> completed { 0U };
  mutable std::atomic<uint64_t> current { 0U };

  FakeCommandQueue()
    : CommandQueue("FakeCommandQueue")
  {
  }

  auto Signal(uint64_t value) const -> void override
  {
    completed.store(value);
    current.store(value);
  }

  auto Signal() const -> uint64_t override
  {
    const auto v = current.fetch_add(1U) + 1U;
    current.store(v);
    completed.store(v);
    return v;
  }

  auto Wait(uint64_t /*value*/, std::chrono::milliseconds /*timeout*/) const
    -> void override
  {
  }
  auto Wait(uint64_t /*value*/) const -> void override { }

  auto GetCompletedValue() const -> uint64_t override
  {
    return completed.load();
  }
  auto GetCurrentValue() const -> uint64_t override { return current.load(); }

  auto Submit(std::shared_ptr<oxygen::graphics::CommandList> /*command_list*/)
    -> void override
  {
  }
  auto Submit(
    std::span<std::shared_ptr<oxygen::graphics::CommandList>> /*command_lists*/)
    -> void override
  {
  }

  auto GetQueueRole() const -> oxygen::graphics::QueueRole override
  {
    return oxygen::graphics::QueueRole::kGraphics;
  }

private:
  auto SignalImmediate(uint64_t value) const -> void override
  {
    completed.store(value);
    current.store(value);
  }
};

} // namespace oxygen::nexus::testing
