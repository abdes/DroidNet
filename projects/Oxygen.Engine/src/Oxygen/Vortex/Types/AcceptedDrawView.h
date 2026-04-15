//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Vortex/PreparedSceneFrame.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct AcceptedDraw {
  observer_ptr<const DrawMetadata> metadata;
  std::uint32_t draw_index;
};

class AcceptedDrawView {
public:
  class Iterator {
  public:
    using iterator_category = std::forward_iterator_tag;
    using value_type = AcceptedDraw;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;

    Iterator() = default;

    [[nodiscard]] OXGN_VRTX_API auto operator*() const noexcept -> value_type;
    OXGN_VRTX_API auto operator++() noexcept -> Iterator&;
    OXGN_VRTX_API auto operator++(int) noexcept -> Iterator;
    [[nodiscard]] OXGN_VRTX_API auto operator==(
      const Iterator& other) const noexcept -> bool;

  private:
    friend class AcceptedDrawView;

    OXGN_VRTX_API Iterator(std::span<const DrawMetadata> metadata,
      std::span<const PreparedSceneFrame::PartitionRange> partitions,
      PassMask accept_mask, bool use_partitions, bool is_end) noexcept;

    OXGN_VRTX_API void AdvanceToNextAccepted() noexcept;
    OXGN_VRTX_API void MarkEnd() noexcept;

    std::span<const DrawMetadata> metadata_;
    std::span<const PreparedSceneFrame::PartitionRange> partitions_;
    PassMask accept_mask_;
    std::size_t partition_index_ { 0U };
    std::uint32_t current_index_ { 0U };
    bool use_partitions_ { false };
    bool is_end_ { true };
  };

  OXGN_VRTX_API AcceptedDrawView(
    const PreparedSceneFrame& frame, PassMask accept_mask) noexcept;

  [[nodiscard]] OXGN_VRTX_API auto begin() const noexcept -> Iterator;
  [[nodiscard]] OXGN_VRTX_API auto end() const noexcept -> Iterator;
  [[nodiscard]] OXGN_VRTX_API auto empty() const noexcept -> bool;

private:
  std::span<const DrawMetadata> metadata_;
  std::span<const PreparedSceneFrame::PartitionRange> partitions_;
  PassMask accept_mask_;
};

} // namespace oxygen::vortex
