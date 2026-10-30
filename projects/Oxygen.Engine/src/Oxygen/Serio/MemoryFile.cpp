//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace {
  //! Memory devices align for SIMD copies, not for unbuffered IO.
  constexpr size_t kMemoryAlignment = 64;
} // namespace

MemoryFile::MemoryFile(ByteBuffer bytes) noexcept
  : storage_(std::move(bytes))
  , size_(static_cast<size_t>(storage_.Size().get()))
{
}

auto MemoryFile::FromBytes(const std::span<const std::byte> bytes) noexcept
  -> Result<MemoryFile>
{
  auto buffer = ByteBuffer::Allocate(CountOf(bytes.size()), kMemoryAlignment);
  if (const auto* error = buffer.error_if()) {
    return ::oxygen::Err(*error);
  }
  if (!bytes.empty()) {
    std::memcpy(buffer.value_if()->Bytes().data(), bytes.data(), bytes.size());
  }
  return ::oxygen::Ok(MemoryFile(std::move(*buffer.value_if())));
}

auto MemoryFile::ReadAt(const ByteOffset at,
  const std::span<std::byte> out) const noexcept -> Result<ByteCount>
{
  if (at.get() >= size_) {
    return ::oxygen::Ok(ByteCount { 0 });
  }
  const auto offset = static_cast<size_t>(at.get());
  const auto count = (std::min)(out.size(), size_ - offset);
  if (count != 0) {
    std::memcpy(
      out.data(), storage_.Bytes().subspan(offset, count).data(), count);
  }
  return ::oxygen::Ok(CountOf(count));
}

auto MemoryFile::WriteAt(const ByteOffset at,
  const std::span<const std::byte> in) noexcept -> Result<void>
{
  if (in.empty()) {
    return {};
  }
  if (at.get() > std::numeric_limits<size_t>::max() - in.size()) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  const auto offset = static_cast<size_t>(at.get());
  const auto end = offset + in.size();
  if (end > static_cast<size_t>(storage_.Size().get())) {
    CHECK_RESULT(Grow((std::max)(end, 2 * size_)));
  }
  auto bytes = storage_.Bytes();
  if (offset > size_) {
    std::memset(bytes.subspan(size_).data(), 0, offset - size_);
  }
  std::memcpy(bytes.subspan(offset).data(), in.data(), in.size());
  size_ = (std::max)(size_, end);
  return {};
}

auto MemoryFile::Reserve(const ByteCount size) noexcept -> Result<void>
{
  if (size.get() > std::numeric_limits<size_t>::max()) {
    return ::oxygen::Err(std::make_error_code(std::errc::not_enough_memory));
  }
  if (size.get() <= storage_.Size().get()) {
    return {};
  }
  return Grow(static_cast<size_t>(size.get()));
}

auto MemoryFile::Take() noexcept -> ByteBuffer
{
  storage_.Truncate(CountOf(size_));
  size_ = 0;
  return std::move(storage_);
}

auto MemoryFile::Grow(const size_t capacity) noexcept -> Result<void>
{
  auto grown = ByteBuffer::Allocate(CountOf(capacity), kMemoryAlignment);
  if (const auto* error = grown.error_if()) {
    return ::oxygen::Err(*error);
  }
  if (size_ != 0) {
    std::memcpy(
      grown.value_if()->Bytes().data(), storage_.Bytes().data(), size_);
  }
  storage_ = std::move(*grown.value_if());
  return {};
}

} // namespace oxygen::serio
