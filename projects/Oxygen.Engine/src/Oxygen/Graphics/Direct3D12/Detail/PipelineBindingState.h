//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

struct ID3D12DescriptorHeap;
struct ID3D12RootSignature;

namespace oxygen::graphics::d3d12::detail {

//! Tracks binding commands required by one native command-list recording.
/*!
 Graphics and compute root arguments are independent. A descriptor-heap change
 invalidates both tables and root-signature binding for directly indexed heaps;
 the signature must be set after the new heaps. Descriptor-table addresses are
 tracked independently of heap identity. Reset starts a new native recording.
*/
class PipelineBindingState final {
public:
  using Heaps = std::array<ID3D12DescriptorHeap*, 2>;

  void Reset() noexcept { *this = {}; }

  [[nodiscard]] auto ChangeHeaps(const Heaps& heaps) noexcept -> bool
  {
    if (heaps == heaps_) {
      return false;
    }
    heaps_ = heaps;
    graphics_signature_ = nullptr;
    compute_signature_ = nullptr;
    graphics_tables_.fill(std::nullopt);
    compute_tables_.fill(std::nullopt);
    return true;
  }

  [[nodiscard]] auto ChangeRootSignature(
    ID3D12RootSignature* signature, const bool is_compute) noexcept -> bool
  {
    auto& current = is_compute ? compute_signature_ : graphics_signature_;
    if (current == signature) {
      return false;
    }
    current = signature;
    Tables(is_compute).fill(std::nullopt);
    return true;
  }

  [[nodiscard]] auto ChangeDescriptorTable(const std::size_t index,
    const uint64_t gpu_address, const bool is_compute) -> bool
  {
    auto& current = Tables(is_compute).at(index);
    if (current == gpu_address) {
      return false;
    }
    current = gpu_address;
    return true;
  }

  [[nodiscard]] auto RootSignature(const bool is_compute) const noexcept
    -> ID3D12RootSignature*
  {
    return is_compute ? compute_signature_ : graphics_signature_;
  }

private:
  auto Tables(const bool is_compute) noexcept
    -> std::array<std::optional<uint64_t>, 2>&
  {
    return is_compute ? compute_tables_ : graphics_tables_;
  }

  Heaps heaps_ {};
  ID3D12RootSignature* graphics_signature_ {};
  ID3D12RootSignature* compute_signature_ {};
  std::array<std::optional<uint64_t>, 2> graphics_tables_ {};
  std::array<std::optional<uint64_t>, 2> compute_tables_ {};
};

} // namespace oxygen::graphics::d3d12::detail
