//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <concepts>
#include <cstddef>
#include <span>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Detail/ErasedObject.h>
#include <Oxygen/Serio/Stream.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

namespace detail {
  //! Inline room for a region stream over any Serio device.
  using ErasedStream = ErasedObject<160>;
} // namespace detail

//! Owns any input stream, with its type erased, for a coarse boundary.
/*!
 Each operation costs one indirect call through a table generated for the
 stream type, with no virtual base. The stream's `Position()` and `Size()` must
 be `const`. A moved-from `AnyInputStream` may only be
 assigned to or destroyed.
*/
class AnyInputStream {
public:
  //! Takes ownership of `stream`.
  template <InputStream S>
    requires(!std::is_same_v<S, AnyInputStream>) && requires(const S& s) {
      { s.Position() } noexcept -> std::same_as<ByteOffset>;
      { s.Size() } noexcept -> std::same_as<ByteCount>;
    }
  explicit AnyInputStream(S stream) noexcept(
    detail::ErasedStream::kStoredInline<S>)
    : storage_(std::move(stream))
    , table_(TableFor<S>())
  {
  }

  ~AnyInputStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyInputStream)
  OXYGEN_DEFAULT_MOVABLE(AnyInputStream)

  [[nodiscard]] auto ReadSome(const std::span<std::byte> out) noexcept
    -> Result<ByteCount>
  {
    return table_->read_some(storage_.Get(), out);
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return table_->position(storage_.Get());
  }

  [[nodiscard]] auto Size() const noexcept -> ByteCount
  {
    return table_->size(storage_.Get());
  }

  [[nodiscard]] auto Seek(const ByteOffset at) noexcept -> Result<void>
  {
    return table_->seek(storage_.Get(), at);
  }

private:
  struct Table {
    Result<ByteCount> (*read_some)(
      void* stream, std::span<std::byte> out) noexcept;
    ByteOffset (*position)(const void* stream) noexcept;
    ByteCount (*size)(const void* stream) noexcept;
    Result<void> (*seek)(void* stream, ByteOffset at) noexcept;
  };

  template <InputStream S> static auto TableFor() noexcept -> const Table*
  {
    static constexpr Table kTable {
      .read_some = [](void* stream, const std::span<std::byte> out) noexcept
        -> Result<ByteCount> { return static_cast<S*>(stream)->ReadSome(out); },
      .position = [](const void* stream) noexcept -> ByteOffset {
        return static_cast<const S*>(stream)->Position();
      },
      .size = [](const void* stream) noexcept -> ByteCount {
        return static_cast<const S*>(stream)->Size();
      },
      .seek = [](void* stream, const ByteOffset at) noexcept -> Result<void> {
        return static_cast<S*>(stream)->Seek(at);
      },
    };
    return &kTable;
  }

  detail::ErasedStream storage_;
  const Table* table_ { nullptr };
};

static_assert(InputStream<AnyInputStream>);

//! Owns any output stream, with its type erased, for a coarse boundary.
/*!
 Each operation costs one indirect call through a table generated for the
 stream type, with no virtual base. The stream's `Position()` must be `const`.
 A moved-from `AnyOutputStream` may only be
 assigned to or destroyed.
*/
class AnyOutputStream {
public:
  //! Takes ownership of `stream`.
  template <OutputStream S>
    requires(!std::is_same_v<S, AnyOutputStream>) && requires(const S& s) {
      { s.Position() } noexcept -> std::same_as<ByteOffset>;
    }
  explicit AnyOutputStream(S stream) noexcept(
    detail::ErasedStream::kStoredInline<S>)
    : storage_(std::move(stream))
    , table_(TableFor<S>())
  {
  }

  ~AnyOutputStream() = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyOutputStream)
  OXYGEN_DEFAULT_MOVABLE(AnyOutputStream)

  [[nodiscard]] auto Write(const std::span<const std::byte> in) noexcept
    -> Result<void>
  {
    return table_->write(storage_.Get(), in);
  }

  [[nodiscard]] auto Flush() noexcept -> Result<void>
  {
    return table_->flush(storage_.Get());
  }

  [[nodiscard]] auto Position() const noexcept -> ByteOffset
  {
    return table_->position(storage_.Get());
  }

private:
  struct Table {
    Result<void> (*write)(void* stream, std::span<const std::byte> in) noexcept;
    Result<void> (*flush)(void* stream) noexcept;
    ByteOffset (*position)(const void* stream) noexcept;
  };

  template <OutputStream S> static auto TableFor() noexcept -> const Table*
  {
    static constexpr Table kTable {
      .write = [](void* stream, const std::span<const std::byte> in) noexcept
        -> Result<void> { return static_cast<S*>(stream)->Write(in); },
      .flush = [](void* stream) noexcept -> Result<void> {
        return static_cast<S*>(stream)->Flush();
      },
      .position = [](const void* stream) noexcept -> ByteOffset {
        return static_cast<const S*>(stream)->Position();
      },
    };
    return &kTable;
  }

  detail::ErasedStream storage_;
  const Table* table_ { nullptr };
};

static_assert(OutputStream<AnyOutputStream>);

} // namespace oxygen::serio
