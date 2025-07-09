//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>
#include <vector>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! In-memory stream for reading and writing binary data, designed for static
//! (compile-time) polymorphism.
/*!
  MemoryStream provides a seekable, resizable stream interface backed by either
  an internal buffer (std::vector<std::byte>) or an external buffer
  (std::span<std::byte>). It supports reading, writing, seeking, and clearing
  operations, making it suitable for serialization, deserialization, and
  temporary data storage in memory.

  - If constructed with an external buffer, MemoryStream operates on that buffer
    without owning it.
  - If constructed with no buffer, it manages its own internal buffer on the
    heap.

  All operations are bounds-checked and return a Result type for error handling.

  This class is intentionally designed without any virtual methods or
  inheritance, and does not use runtime polymorphism. Instead, it is intended to
  be used with the Stream concept for static (compile-time) polymorphism,
  enabling efficient, type-safe generic code without the overhead of vtables.

  For use cases requiring runtime polymorphism, see AnyMemoryStream.

  @see AnyMemoryStream, Stream
*/
class MemoryStream {
public:
  OXGN_SERIO_API explicit MemoryStream(
    std::span<std::byte> buffer = {}) noexcept;

  ~MemoryStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(MemoryStream)
  OXYGEN_DEFAULT_MOVABLE(MemoryStream)

  OXGN_SERIO_NDAPI auto Read(std::byte* data, size_t size) noexcept
    -> Result<void>;

  [[nodiscard]] auto Write(const std::span<const std::byte> data) noexcept
    -> Result<void>
  {
    return Write(data.data(), data.size());
  }

  OXGN_SERIO_NDAPI auto Write(const std::byte* data, size_t size) noexcept
    -> Result<void>;

  OXGN_SERIO_NDAPI auto Flush() noexcept -> Result<void>;

  OXGN_SERIO_NDAPI auto Size() const noexcept -> Result<size_t>;

  OXGN_SERIO_NDAPI auto Position() const noexcept -> Result<size_t>;

  OXGN_SERIO_NDAPI auto Seek(size_t pos) noexcept -> Result<void>;

  OXGN_SERIO_NDAPI auto Backward(size_t offset) noexcept -> Result<void>;

  OXGN_SERIO_NDAPI auto Forward(size_t offset) noexcept -> Result<void>;

  OXGN_SERIO_NDAPI auto SeekEnd() noexcept -> Result<void>;

  OXGN_SERIO_API auto Reset() noexcept -> void;

  OXGN_SERIO_NDAPI auto Data() const noexcept -> std::span<const std::byte>;

  //! Clears the internal buffer or fills the external buffer with zeros.
  OXGN_SERIO_API auto Clear() -> void;

private:
  std::vector<std::byte> internal_buffer_;
  std::span<std::byte> external_buffer_;
  size_t pos_ = 0;

  [[nodiscard]] auto GetBuffer() noexcept -> std::span<std::byte>;
  [[nodiscard]] auto GetBuffer() const noexcept -> std::span<const std::byte>;
};

static_assert(Stream<MemoryStream>);

//! Type-erased, polymorphic wrapper for MemoryStream implementing the AnyStream
//! interface.
/*!
  AnyMemoryStream provides a type-erased, that can be used wherever an AnyStream
  pointer or reference is required. It forwards all stream operations to an
  internal MemoryStream instance, enabling runtime polymorphism and generic
  stream handling without exposing the concrete MemoryStream type.

  This is useful for APIs or containers that operate on heterogeneous stream
  types via the AnyStream interface.

  @see MemoryStream, AnyStream
*/
class AnyMemoryStream : public AnyStream {
public:
  explicit AnyMemoryStream(const std::span<std::byte> buffer = {}) noexcept
    : mem_stream_(buffer)
  {
  }

  ~AnyMemoryStream() override = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyMemoryStream)
  OXYGEN_DEFAULT_MOVABLE(AnyMemoryStream)

  [[nodiscard]] auto Read(std::byte* data, const size_t size) noexcept
    -> Result<void> override
  {
    return mem_stream_.Read(data, size);
  }

  [[nodiscard]] auto Write(const std::byte* data, const size_t size) noexcept
    -> Result<void> override
  {
    return mem_stream_.Write(data, size);
  }

  [[nodiscard]] auto Write(const std::span<const std::byte> data) noexcept
    -> Result<void> override
  {
    return mem_stream_.Write(data);
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void> override
  {
    return mem_stream_.Flush();
  }

  [[nodiscard]] auto Size() const noexcept -> Result<size_t> override
  {
    return mem_stream_.Size();
  }

  [[nodiscard]] auto Position() const noexcept -> Result<size_t> override
  {
    return mem_stream_.Position();
  }

  [[nodiscard]] auto Seek(const size_t pos) noexcept -> Result<void> override
  {
    return mem_stream_.Seek(pos);
  }

  [[nodiscard]] auto Backward(const size_t offset) noexcept
    -> Result<void> override
  {
    return mem_stream_.Backward(offset);
  }

  [[nodiscard]] auto Forward(const size_t offset) noexcept
    -> Result<void> override
  {
    return mem_stream_.Forward(offset);
  }

  [[nodiscard]] auto SeekEnd() noexcept -> Result<void> override
  {
    return mem_stream_.SeekEnd();
  }

  auto Reset() noexcept -> void override { mem_stream_.Reset(); }

  [[nodiscard]] auto Data() const noexcept -> std::span<const std::byte>
  {
    return mem_stream_.Data();
  }

  auto Clear() -> void { mem_stream_.Clear(); }

private:
  MemoryStream mem_stream_;
};

} // namespace oxygen::serio
