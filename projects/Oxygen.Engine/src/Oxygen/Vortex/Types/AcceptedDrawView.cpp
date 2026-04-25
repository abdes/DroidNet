//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>

#include <Oxygen/Vortex/Types/AcceptedDrawView.h>

namespace oxygen::vortex {

namespace {

  [[nodiscard]] auto Matches(
    const PassMask mask, const PassMask accept) noexcept -> bool
  {
    return (mask & accept).get() != 0U;
  }

  [[nodiscard]] auto ClampDrawIndex(const std::uint32_t index,
    const std::size_t metadata_count) noexcept -> std::uint32_t
  {
    return (std::min)(index, static_cast<std::uint32_t>(metadata_count));
  }

} // namespace

AcceptedDrawView::AcceptedDrawView(
  const PreparedSceneFrame& frame, const PassMask accept_mask) noexcept
  : metadata_(frame.GetDrawMetadata())
  , partitions_(frame.partitions)
  , accept_mask_(accept_mask)
{
}

auto AcceptedDrawView::begin() const noexcept -> Iterator
{
  return {
    metadata_,
    partitions_,
    accept_mask_,
    !partitions_.empty(),
    metadata_.empty() || accept_mask_.IsEmpty(),
  };
}

auto AcceptedDrawView::end() const noexcept -> Iterator
{
  return {
    metadata_,
    partitions_,
    accept_mask_,
    !partitions_.empty(),
    true,
  };
}

auto AcceptedDrawView::empty() const noexcept -> bool
{
  return begin() == end();
}

AcceptedDrawView::Iterator::Iterator(
  const std::span<const DrawMetadata> metadata,
  const std::span<const PreparedSceneFrame::PartitionRange> partitions,
  const PassMask accept_mask, const bool use_partitions,
  const bool is_end) noexcept
  : metadata_(metadata)
  , partitions_(partitions)
  , accept_mask_(accept_mask)
  , use_partitions_(use_partitions)
  , is_end_(is_end)
{
  if (!is_end_) {
    AdvanceToNextAccepted();
  } else {
    MarkEnd();
  }
}

auto AcceptedDrawView::Iterator::operator*() const noexcept -> value_type
{
  return { observer_ptr<const DrawMetadata> { &metadata_[current_index_] },
    current_index_ };
}

auto AcceptedDrawView::Iterator::operator++() noexcept -> Iterator&
{
  if (!is_end_) {
    ++current_index_;
    AdvanceToNextAccepted();
  }
  return *this;
}

auto AcceptedDrawView::Iterator::operator++(int) noexcept -> Iterator
{
  auto copy = *this;
  ++(*this);
  return copy;
}

auto AcceptedDrawView::Iterator::operator==(
  const Iterator& other) const noexcept -> bool
{
  return metadata_.data() == other.metadata_.data()
    && metadata_.size() == other.metadata_.size()
    && partitions_.data() == other.partitions_.data()
    && partitions_.size() == other.partitions_.size()
    && accept_mask_ == other.accept_mask_
    && partition_index_ == other.partition_index_
    && current_index_ == other.current_index_
    && use_partitions_ == other.use_partitions_ && is_end_ == other.is_end_;
}

void AcceptedDrawView::Iterator::AdvanceToNextAccepted() noexcept
{
  if (metadata_.empty() || accept_mask_.IsEmpty()) {
    MarkEnd();
    return;
  }

  if (!use_partitions_) {
    while (current_index_ < metadata_.size()
      && !Matches(metadata_[current_index_].flags, accept_mask_)) {
      ++current_index_;
    }
    if (current_index_ >= metadata_.size()) {
      MarkEnd();
    }
    return;
  }

  while (partition_index_ < partitions_.size()) {
    const auto& partition = partitions_[partition_index_];
    if (!Matches(partition.pass_mask, accept_mask_)) {
      ++partition_index_;
      continue;
    }

    const auto begin = ClampDrawIndex(partition.begin, metadata_.size());
    const auto end = ClampDrawIndex(partition.end, metadata_.size());
    current_index_ = std::max(current_index_, begin);
    if (current_index_ < end) {
      return;
    }

    ++partition_index_;
  }

  MarkEnd();
}

void AcceptedDrawView::Iterator::MarkEnd() noexcept
{
  partition_index_ = partitions_.size();
  current_index_ = static_cast<std::uint32_t>(metadata_.size());
  is_end_ = true;
}

} // namespace oxygen::vortex
