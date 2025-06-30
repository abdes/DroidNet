//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Stream.h>

namespace oxygen::serio {

//=== BackingStream concept (pluggable file io for FileStream) ===------------//
template <typename T>
concept BackingStream
  = requires(T stream, const T constStream, const std::filesystem::path& path,
    std::ios::openmode mode, const std::byte* cdata, std::byte* data,
    size_t size, std::streamoff off, std::ios_base::seekdir way) {
      { stream.open(path, mode) } -> std::same_as<void>;
      { constStream.is_open() } -> std::same_as<bool>;
      { stream.write(cdata, size) } -> std::same_as<void>;
      { stream.read(data, size) } -> std::same_as<void>;
      { stream.clear() } -> std::same_as<void>;
      { stream.flush() } -> std::same_as<void>;
      { stream.tellg() } -> std::same_as<std::streampos>;
      { stream.seekg(off, way) } -> std::same_as<void>;
      { constStream.fail() } -> std::same_as<bool>;
      { constStream.eof() } -> std::same_as<bool>;
    };

//=== ByteFileStream: std::byte wrapper for std::fstream ===------------------//

class ByteFileStream {
public:
  ByteFileStream() = default;
  ~ByteFileStream() = default;

  void open(const std::filesystem::path& path, std::ios::openmode mode)
  {
    file_.open(path, mode | std::ios::binary);
  }

  [[nodiscard]] bool is_open() const noexcept { return file_.is_open(); }

  void write(const std::byte* data, size_t size)
  {
    if (!file_.is_open() || !file_.good()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.write(
      reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file_)
      file_.setstate(std::ios::failbit);
  }

  void read(std::byte* data, size_t size)
  {
    if (!file_.is_open() || !file_.good()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.read(
      reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!file_ && !file_.eof())
      file_.setstate(std::ios::failbit);
  }

  void flush()
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.flush();
    if (!file_)
      file_.setstate(std::ios::failbit);
  }

  void clear(std::ios::iostate state = std::ios::goodbit)
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.clear(state);
  }

  [[nodiscard]] auto tellg() -> std::streampos
  {
    if (!file_.is_open())
      return -1;
    return file_.tellg();
  }

  void seekg(std::streamoff off, std::ios_base::seekdir way)
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.seekg(off, way);
    if (!file_)
      file_.setstate(std::ios::failbit);
  }

  [[nodiscard]] bool fail() const noexcept { return !file_.good(); }
  [[nodiscard]] bool eof() const noexcept { return file_.eof(); }

private:
  std::fstream file_;
};

static_assert(BackingStream<ByteFileStream>);

template <BackingStream StreamType = ByteFileStream> class FileStream {
public:
  explicit FileStream(const std::filesystem::path& path,
    std::ios::openmode mode = std::ios::in | std::ios::out,
    std::unique_ptr<StreamType> stream = nullptr);

  virtual ~FileStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(FileStream);
  OXYGEN_DEFAULT_MOVABLE(FileStream);

  [[nodiscard]] auto write(std::span<const std::byte> data) noexcept
    -> Result<void>
  {
    return write(data.data(), data.size());
  }

  [[nodiscard]] auto write(const std::byte* data, size_t size) const noexcept
    -> Result<void>;
  [[nodiscard]] auto read(std::byte* data, size_t size) const noexcept
    -> Result<void>;
  [[nodiscard]] auto flush() const noexcept -> Result<void>;
  [[nodiscard]] auto position() const noexcept -> Result<size_t>;

  //! Sets the position of the next character to be extracted from the input
  //! stream.
  /*!
    \param pos The new absolute position within the stream.
    \return A Result<void> indicating success or failure.
    \retval std::errc::invalid_seek if the seek operation fails.
  */
  [[nodiscard]] auto seek(size_t pos) const noexcept -> Result<void>;
  [[nodiscard]] auto size() const noexcept -> Result<size_t>;
  [[nodiscard]] auto backward(size_t offset) const noexcept -> Result<void>;
  [[nodiscard]] auto forward(size_t offset) const noexcept -> Result<void>;
  [[nodiscard]] auto seek_end() const noexcept -> Result<void>;

private:
  std::unique_ptr<StreamType> stream_;
};

static_assert(Stream<FileStream<>>);

template <BackingStream StreamType>
FileStream<StreamType>::FileStream(const std::filesystem::path& path,
  const std::ios::openmode mode, std::unique_ptr<StreamType> stream)
  : stream_(stream ? std::move(stream) : std::make_unique<StreamType>())
{
  stream_->open(path, mode | std::ios::binary);
  if (!stream_->is_open()) {
    throw std::system_error(
      std::make_error_code(std::errc::no_such_file_or_directory));
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::write(
  const std::byte* data, const size_t size) const noexcept -> Result<void>
{
  try {
    if (data == nullptr && size > 0) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    if (size
      > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    stream_->write(data, static_cast<std::streamsize>(size));
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::read(
  std::byte* data, const size_t size) const noexcept -> Result<void>
{
  try {
    if (data == nullptr && size > 0) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    if (size
      > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
      return std::make_error_code(std::errc::invalid_argument);
    }
    stream_->read(data, static_cast<std::streamsize>(size));
    if (stream_->fail() && !stream_->eof()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    if (stream_->eof()) {
      stream_->clear();
      return std::make_error_code(std::errc::no_buffer_space);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::flush() const noexcept -> Result<void>
{
  try {
    stream_->flush();
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::position() const noexcept -> Result<size_t>
{
  try {
    const auto pos = stream_->tellg();
    if (pos < 0) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return static_cast<size_t>(pos);
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::seek(const size_t pos) const noexcept
  -> Result<void>
{
  try {
    if (pos > static_cast<size_t>(std::numeric_limits<std::streamoff>::max())) {
      return std::make_error_code(std::errc::invalid_argument);
    }

    const auto size_result = this->size();
    if (!size_result) {
      return size_result.error();
    }
    if (pos > size_result.value()) {
      return std::make_error_code(std::errc::invalid_argument);
    }

    stream_->seekg(static_cast<std::streamoff>(pos), std::ios::beg);
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }

    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::size() const noexcept -> Result<size_t>
{
  try {
    const auto current = stream_->tellg();
    if (current < 0) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }

    stream_->seekg(0, std::ios::end);
    if (stream_->fail()) {
      stream_->clear();
      stream_->seekg(current, std::ios::beg);
      if (stream_->fail()) {
        stream_->clear();
      }
      return std::make_error_code(std::errc::io_error);
    }

    const auto size = stream_->tellg();
    if (size < 0) {
      stream_->clear();
      stream_->seekg(current, std::ios::beg);
      if (stream_->fail()) {
        stream_->clear();
      }
      return std::make_error_code(std::errc::value_too_large);
    }

    stream_->seekg(current, std::ios::beg);
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }

    return static_cast<size_t>(size);
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::backward(size_t offset) const noexcept
  -> Result<void>
{
  try {
    stream_->seekg(-static_cast<std::streamoff>(offset), std::ios::cur);
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::forward(size_t offset) const noexcept
  -> Result<void>
{
  try {
    stream_->seekg(static_cast<std::streamoff>(offset), std::ios::cur);
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::seek_end() const noexcept -> Result<void>
{
  try {
    stream_->seekg(0, std::ios::end);
    if (stream_->fail()) {
      stream_->clear();
      return std::make_error_code(std::errc::io_error);
    }
    return {};
  } catch (const std::exception& /*ex*/) {
    return std::make_error_code(std::errc::io_error);
  }
}

} // namespace oxygen::serio
