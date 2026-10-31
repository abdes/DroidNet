//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/AlignmentGuard.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/RegionStream.h>

namespace oxygen::content::internal {

//! Owns a cooked file reader and bounds reads before allocating result buffers.
//! Positions remain file-relative for the existing packed-format loaders.
class ContentFileReader final : public serio::AnyReader {
public:
  explicit ContentFileReader(const std::filesystem::path& path,
    const size_t offset = 0U, const std::optional<size_t> size = {})
    : file_(OpenFile(path))
    , stream_(OpenStream(file_))
    , reader_(stream_)
    , begin_(offset)
    , end_(RangeEnd(stream_, offset, size))
  {
    if (const auto seek = reader_.Seek(offset); !seek) {
      throw std::system_error(seek.error(), "Could not seek content file");
    }
  }

  ~ContentFileReader() override = default;
  OXYGEN_MAKE_NON_COPYABLE(ContentFileReader)
  OXYGEN_MAKE_NON_MOVABLE(ContentFileReader)

  [[nodiscard]] auto ReadBlob(const size_t size) noexcept
    -> Result<std::vector<std::byte>> override
  {
    if (const auto bounds = CheckRemaining(size); !bounds) {
      return Err(bounds.error());
    }
    return reader_.ReadBlob(size);
  }

  [[nodiscard]] auto ReadBlobInto(const std::span<std::byte> buffer) noexcept
    -> Result<void> override
  {
    if (const auto bounds = CheckRemaining(buffer.size()); !bounds) {
      return Err(bounds.error());
    }
    return reader_.ReadBlobInto(buffer);
  }

  [[nodiscard]] auto Position() noexcept -> Result<size_t> override
  {
    return reader_.Position();
  }

  [[nodiscard]] auto Remaining() noexcept -> serio::ByteCount override
  {
    const auto position = reader_.Position();
    if (!position || *position < begin_ || *position > end_) {
      return serio::ByteCount { 0 };
    }
    return serio::CountOf(end_ - *position);
  }

  [[nodiscard]] auto AlignTo(const size_t alignment) noexcept
    -> Result<void> override
  {
    if (const auto aligned = reader_.AlignTo(alignment); !aligned) {
      return Err(aligned.error());
    }
    return CheckRemaining(0U);
  }

  [[nodiscard]] auto ScopedAlignment(const uint16_t alignment)
    -> serio::AlignmentGuard override
  {
    return reader_.ScopedAlignment(alignment);
  }

  [[nodiscard]] auto Forward(const size_t count) noexcept
    -> Result<void> override
  {
    if (const auto bounds = CheckRemaining(count); !bounds) {
      return Err(bounds.error());
    }
    return reader_.Forward(count);
  }

  [[nodiscard]] auto Seek(const size_t position) noexcept
    -> Result<void> override
  {
    if (position < begin_ || position > end_) {
      return Err(std::errc::result_out_of_range);
    }
    return reader_.Seek(position);
  }

private:
  using Stream = serio::RegionReadStream<serio::NativeFile>;

  static auto OpenFile(const std::filesystem::path& path) -> serio::NativeFile
  {
    auto file = serio::NativeFile::Open(path, {});
    if (!file) {
      throw std::system_error(
        file.error(), "Could not open content file '" + path.string() + "'");
    }
    return std::move(file).value();
  }

  static auto OpenStream(const serio::NativeFile& file) -> Stream
  {
    auto stream = serio::OpenReadStream(file);
    if (!stream) {
      throw std::system_error(
        stream.error(), "Could not open content file stream");
    }
    return std::move(stream).value();
  }

  static auto RangeEnd(const Stream& stream, const size_t offset,
    const std::optional<size_t> length) -> size_t
  {
    const auto size = static_cast<size_t>(stream.Size().get());
    if (offset > size || (length && *length > size - offset)) {
      throw std::out_of_range("Content descriptor range exceeds its file");
    }
    return length ? offset + *length : size;
  }

  auto CheckRemaining(const size_t count) noexcept -> Result<void>
  {
    const auto position = reader_.Position();
    if (!position) {
      return Err(position.error());
    }
    if (*position < begin_ || *position > end_ || count > end_ - *position) {
      return Err(std::errc::result_out_of_range);
    }
    return {};
  }

  serio::NativeFile file_;
  Stream stream_;
  serio::Reader<Stream> reader_;
  size_t begin_;
  size_t end_;
};

} // namespace oxygen::content::internal
