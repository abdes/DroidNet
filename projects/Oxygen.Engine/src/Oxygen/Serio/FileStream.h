//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <fstream>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Stream.h>

namespace oxygen::serio {

//! Concept to specify the implementation backend of a FileStream.
/*!
  Allows for seamlessly switching between different file stream implementations,
  such as std::fstream or any other custom stream implementation that adheres to
  the same interface.

  Particularly useful for adapting to different element types (such as
  std::byte) or different storage mechanisms (like in-memory streams), while
  maintaining the same interface.
*/
template <typename T>
concept BackingStream = requires(T stream, const T c_stream,
  const std::filesystem::path& path, std::ios::openmode mode,
  const std::byte* cdata, std::byte* data, std::span<const std::byte> sdata,
  size_t size, std::streamoff off, std::ios_base::seekdir way) {
  { stream.open(path, mode) } -> std::same_as<void>;
  { stream.close() } -> std::same_as<void>;
  { c_stream.is_open() } -> std::same_as<bool>;
  { stream.write(cdata, size) } -> std::same_as<void>;
  { stream.read(data, size) } -> std::same_as<void>;
  { stream.clear() } -> std::same_as<void>;
  { stream.flush() } -> std::same_as<void>;
  { stream.tellg() } -> std::same_as<std::streampos>;
  { stream.seekg(off, way) } -> std::same_as<void>;
  { c_stream.fail() } -> std::same_as<bool>;
  { c_stream.eof() } -> std::same_as<bool>;
};

//! A file stream implementation backend that adapts to std::fstream to use
//! std::byte instead of char.
class ByteFileStream {
public:
  ByteFileStream() = default;
  ~ByteFileStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(ByteFileStream)
  OXYGEN_DEFAULT_MOVABLE(ByteFileStream)

  auto open(const std::filesystem::path& path, const std::ios::openmode mode)
    -> void
  {
    file_.open(path, mode | std::ios::binary);
  }

  auto close() -> void { file_.close(); }

  [[nodiscard]] auto is_open() const noexcept -> bool
  {
    return file_.is_open();
  }

  auto write(const std::byte* data, const size_t size) -> void
  {
    if (!file_.is_open() || !file_.good()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.write(
      reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
    if (!file_) {
      file_.setstate(std::ios::failbit);
    }
  }

  auto read(std::byte* data, const size_t size) -> void
  {
    if (!file_.is_open() || !file_.good()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.read(
      reinterpret_cast<char*>(data), static_cast<std::streamsize>(size));
    if (!file_ && !file_.eof()) {
      file_.setstate(std::ios::failbit);
    }
  }

  auto flush() -> void
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.flush();
    if (!file_) {
      file_.setstate(std::ios::failbit);
    }
  }

  auto clear(const std::ios::iostate state = std::ios::goodbit) -> void
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.clear(state);
  }

  [[nodiscard]] auto tellg() -> std::streampos
  {
    if (!file_.is_open()) {
      return -1;
    }
    return file_.tellg();
  }

  auto seekg(const std::streamoff off, const std::ios_base::seekdir way) -> void
  {
    if (!file_.is_open()) {
      file_.setstate(std::ios::failbit);
      return;
    }
    file_.seekg(off, way);
    if (!file_) {
      file_.setstate(std::ios::failbit);
    }
  }

  [[nodiscard]] auto fail() const noexcept -> bool { return !file_.good(); }
  [[nodiscard]] auto eof() const noexcept -> bool { return file_.eof(); }

private:
  std::fstream file_;
};
static_assert(BackingStream<ByteFileStream>);

//! A stream that provides file I/O operations using a specified backing
//! implementation.
/*!
  This class allows for reading and writing to "files" using a specified
  BackingStream implementation, such as std::fstream or any custom stream
  implementation that adheres to the BackingStream concept. This is particularly
  useful for adapting to different element types (like std::byte) or different
  IO optimizations (such as buffered IO, memory-mapped files, etc.), while
  maintaining the same interface.

  The default BackingStream is ByteFileStream, which uses std::fstream with
  std::byte as the element type.

  All operations are exception free, and the ones that may fail return a Result
  type for error handling.

  This class is intentionally designed without any virtual methods or
  inheritance, and does not use runtime polymorphism. Instead, it is intended to
  be used with the Stream concept for static (compile-time) polymorphism,
  enabling efficient, type-safe generic code without the overhead of vtables.

  For use cases requiring runtime polymorphism, see AnyFileStream.

  @see AnyFileStream, Stream
*/
template <BackingStream StreamType = ByteFileStream> class FileStream {
public:
  explicit FileStream(const std::filesystem::path& path,
    std::ios::openmode mode = std::ios::in | std::ios::out,
    std::unique_ptr<StreamType> stream = nullptr);

  ~FileStream()
  {
    if (stream_ && stream_->is_open()) {
      stream_->flush();
      stream_->close();
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(FileStream)
  OXYGEN_DEFAULT_MOVABLE(FileStream)

  [[nodiscard]] auto Write(const std::span<const std::byte> data) noexcept
    -> Result<void>
  {
    return Write(data.data(), data.size());
  }

  [[nodiscard]] auto Write(const std::byte* data, size_t size) noexcept
    -> Result<void>;
  [[nodiscard]] auto Read(std::byte* data, size_t size) noexcept
    -> Result<void>;
  [[nodiscard]] auto Flush() noexcept -> Result<void>;

  [[nodiscard]] auto Position() const noexcept -> Result<size_t>;
  [[nodiscard]] auto Seek(size_t pos) noexcept -> Result<void>;
  [[nodiscard]] auto Size() const noexcept -> Result<size_t>;
  [[nodiscard]] auto Backward(size_t offset) noexcept -> Result<void>;
  [[nodiscard]] auto Forward(size_t offset) noexcept -> Result<void>;
  [[nodiscard]] auto SeekEnd() noexcept -> Result<void>;

  auto Reset() noexcept -> void
  {
    stream_->clear();
    stream_->seekg(0, std::ios::beg);
  }

private:
  std::unique_ptr<StreamType> stream_;
};
static_assert(Stream<FileStream<>>);

//! Type-erased, polymorphic wrapper for FileStream implementing the AnyStream
//! interface.
/*!
  AnyFileStream provides a type-erased file stream that can be used wherever an
  AnyStream pointer or reference is required. It forwards all stream operations
  to an internal FileStream instance (using the default ByteFileStream backend),
  enabling runtime polymorphism and generic stream handling without exposing the
  concrete FileStream type.

  This is useful for APIs or containers that operate on heterogeneous stream
  types via the AnyStream interface.

  @see FileStream, AnyStream
*/
template <BackingStream StreamType = ByteFileStream>
class AnyFileStream : public AnyStream {
public:
  explicit AnyFileStream(const std::filesystem::path& path,
    std::ios::openmode mode = std::ios::in | std::ios::out)
    : file_stream_(path, mode)
  {
  }

  ~AnyFileStream() override = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyFileStream)
  OXYGEN_DEFAULT_MOVABLE(AnyFileStream)

  [[nodiscard]] auto Read(std::byte* data, const size_t size) noexcept
    -> Result<void> override
  {
    return file_stream_.Read(data, size);
  }

  [[nodiscard]] auto Write(const std::byte* data, const size_t size) noexcept
    -> Result<void> override
  {
    return file_stream_.Write(data, size);
  }

  [[nodiscard]] auto Write(const std::span<const std::byte> data) noexcept
    -> Result<void> override
  {
    return file_stream_.Write(data);
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void> override
  {
    return file_stream_.Flush();
  }

  [[nodiscard]] auto Size() const noexcept -> Result<size_t> override
  {
    return file_stream_.Size();
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t> override
  {
    return file_stream_.Position();
  }

  [[nodiscard]] auto Seek(const size_t pos) noexcept -> Result<void> override
  {
    return file_stream_.Seek(pos);
  }

  [[nodiscard]] auto Backward(const size_t offset) noexcept
    -> Result<void> override
  {
    return file_stream_.Backward(offset);
  }

  [[nodiscard]] auto Forward(const size_t offset) noexcept
    -> Result<void> override
  {
    return file_stream_.Forward(offset);
  }

  [[nodiscard]] auto SeekEnd() noexcept -> Result<void> override
  {
    return file_stream_.SeekEnd();
  }

  auto Reset() noexcept -> void override { file_stream_.Reset(); }

private:
  FileStream<StreamType> file_stream_;
};

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
auto FileStream<StreamType>::Write(
  const std::byte* data, const size_t size) noexcept -> Result<void>
{
  if (size == 0) {
    return {};
  }
  if (data == nullptr && size > 0) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  if (size
    > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)())) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  try {
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
auto FileStream<StreamType>::Read(std::byte* data, const size_t size) noexcept
  -> Result<void>
{
  if (size == 0) {
    return {};
  }
  if (data == nullptr && size > 0) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  if (size
    > static_cast<size_t>((std::numeric_limits<std::streamsize>::max)())) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  try {
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
auto FileStream<StreamType>::Flush() noexcept -> Result<void>
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
auto FileStream<StreamType>::Position() const noexcept -> Result<size_t>
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
auto FileStream<StreamType>::Seek(const size_t pos) noexcept -> Result<void>
{
  try {
    if (pos
      > static_cast<size_t>((std::numeric_limits<std::streamoff>::max)())) {
      return std::make_error_code(std::errc::invalid_argument);
    }

    const auto size_result = this->Size();
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
auto FileStream<StreamType>::Size() const noexcept -> Result<size_t>
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
auto FileStream<StreamType>::Backward(const size_t offset) noexcept
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
auto FileStream<StreamType>::Forward(const size_t offset) noexcept
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
auto FileStream<StreamType>::SeekEnd() noexcept -> Result<void>
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
