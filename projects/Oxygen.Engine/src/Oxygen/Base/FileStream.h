//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Stream.h>

#include <filesystem>
#include <fstream>
#include <memory>

namespace oxygen::serio {

template <typename T>
concept BackingStream = requires(T stream, const T constStream, const std::filesystem::path& path,
    std::ios::openmode mode, char* data, std::streamsize size, std::streamoff off, std::ios_base::seekdir way) {
    { stream.open(path, mode) } -> std::same_as<void>;
    { constStream.is_open() } -> std::same_as<bool>;
    { stream.write(data, size) } -> std::convertible_to<std::basic_ostream<char>&>;
    { stream.read(data, size) } -> std::convertible_to<std::basic_istream<char>&>;
    { stream.flush() } -> std::convertible_to<std::basic_ostream<char>&>;
    { stream.tellg() }; // We cannot specify the return type here with MSVC
    { stream.seekg(off, way) } -> std::convertible_to<std::basic_istream<char>&>;
    { constStream.fail() } -> std::same_as<bool>;
    { constStream.eof() } -> std::same_as<bool>;
};

static_assert(BackingStream<std::fstream>);

template <BackingStream StreamType = std::fstream>
class FileStream {
public:
    explicit FileStream(
        const std::filesystem::path& path,
        std::ios::openmode mode = std::ios::in | std::ios::out,
        std::unique_ptr<StreamType> stream = nullptr);

    virtual ~FileStream() = default;

    OXYGEN_MAKE_NON_COPYABLE(FileStream);
    OXYGEN_DEFAULT_MOVABLE(FileStream);

    [[nodiscard]] auto write(const char* data, size_t size) const noexcept -> Result<void>;
    [[nodiscard]] auto read(char* data, size_t size) const noexcept -> Result<void>;
    [[nodiscard]] auto flush() const noexcept -> Result<void>;
    [[nodiscard]] auto position() const noexcept -> Result<size_t>;

    //! Sets the position of the next character to be extracted from the input stream.
    /*!
      \param pos The new absolute position within the stream.
      \return A Result<void> indicating success or failure.
      \retval std::errc::invalid_seek if the seek operation fails.
    */
    [[nodiscard]] auto seek(size_t pos) const noexcept -> Result<void>;
    [[nodiscard]] auto size() const noexcept -> Result<size_t>;

private:
    std::unique_ptr<StreamType> stream_;
};

static_assert(Stream<FileStream<>>);

template <BackingStream StreamType>
FileStream<StreamType>::FileStream(const std::filesystem::path& path, const std::ios::openmode mode, std::unique_ptr<StreamType> stream)
    : stream_(stream ? std::move(stream) : std::make_unique<StreamType>())
{
    stream_->open(path, mode | std::ios::binary);
    if (!stream_->is_open()) {
        throw std::system_error(std::make_error_code(std::errc::no_such_file_or_directory));
    }
}

template <BackingStream StreamType>
auto FileStream<StreamType>::write(const char* data, const size_t size) const noexcept -> Result<void>
{
    if (data == nullptr && size > 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    stream_->write(data, static_cast<std::streamsize>(size));
    if (stream_->fail()) {
        stream_->clear();
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

template <BackingStream StreamType>
auto FileStream<StreamType>::read(char* data, const size_t size) const noexcept -> Result<void>
{
    if (data == nullptr && size > 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }
    if (size > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
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
}

template <BackingStream StreamType>
auto FileStream<StreamType>::flush() const noexcept -> Result<void>
{
    stream_->flush();
    if (stream_->fail()) {
        stream_->clear();
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

template <BackingStream StreamType>
auto FileStream<StreamType>::position() const noexcept -> Result<size_t>
{
    const auto pos = stream_->tellg();
    if (pos < 0) {
        stream_->clear();
        return std::make_error_code(std::errc::io_error);
    }
    return static_cast<size_t>(pos);
}

template <BackingStream StreamType>
auto FileStream<StreamType>::seek(const size_t pos) const noexcept -> Result<void>
{
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
}

template <BackingStream StreamType>
auto FileStream<StreamType>::size() const noexcept -> Result<size_t>
{
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
}

// Explicit template instantiation
template class FileStream<std::fstream>;

} // namespace oxygen::serio
