//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <span>
#include <utility>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Detail/GuardedCopy.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/MappedFile.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

MappedFile::MappedFile(
  std::byte* view, const size_t size, std::filesystem::path path) noexcept
  : view_(view)
  , size_(size)
  , path_(std::move(path))
{
}

MappedFile::~MappedFile() noexcept { Close(); }

MappedFile::MappedFile(MappedFile&& other) noexcept
  : view_(std::exchange(other.view_, nullptr))
  , size_(std::exchange(other.size_, 0))
  , path_(std::move(other.path_))
{
}

auto MappedFile::operator=(MappedFile&& other) noexcept -> MappedFile&
{
  if (this != &other) {
    Close();
    view_ = std::exchange(other.view_, nullptr);
    size_ = std::exchange(other.size_, 0);
    path_ = std::move(other.path_);
  }
  return *this;
}

auto MappedFile::ReadAt(const ByteOffset at,
  const std::span<std::byte> out) const noexcept -> Result<ByteCount>
{
  if (at.get() >= size_ || out.empty()) {
    return ::oxygen::Ok(ByteCount { 0 });
  }
  const auto offset = static_cast<size_t>(at.get());
  const auto count = (std::min)(out.size(), size_ - offset);
  const auto source = this->Bytes().subspan(offset, count);
  if (!detail::GuardedCopy(out.data(), source.data(), count)) {
    return ::oxygen::Err(make_error_code(Error::kIo));
  }
  return ::oxygen::Ok(CountOf(count));
}

} // namespace oxygen::serio
