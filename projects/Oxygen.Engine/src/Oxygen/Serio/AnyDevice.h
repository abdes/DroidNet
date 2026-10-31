//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <system_error>
#include <type_traits>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Detail/ErasedObject.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Operation.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::serio {

//! Owns any readable device, with its type erased, for a coarse boundary.
/*!
 One `AnyDevice` is handed over once per file or region (a mounted pak, a
 data file), never per field: each operation costs one indirect call through
 a table generated for the device type, with no virtual base. Small devices
 (a `NativeFile`, a `MappedFile`, a `MemoryFile`) are stored inline.

 `AnyDevice` satisfies `WritableDevice` so generic code takes it unchanged,
 but whether the erased device writes is known only at run time: `Writable()`
 says so, and a write to a device that does not write fails with
 `operation_not_supported`. `Contiguous()` returns the view of a contiguous
 device.

 A moved-from `AnyDevice` may only be assigned to or destroyed. Shared
 ownership, when a consumer needs it, is the consumer's
 `std::shared_ptr<const AnyDevice>`.
*/
class AnyDevice {
public:
  //! Takes ownership of `device`.
  template <ReadableDevice D>
    requires(!std::is_same_v<D, AnyDevice>)
  explicit AnyDevice(D device) noexcept(Storage::kStoredInline<D>)
    : storage_(std::move(device))
    , table_(TableFor<D>())
  {
  }

  ~AnyDevice() = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyDevice)
  OXYGEN_DEFAULT_MOVABLE(AnyDevice)

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return table_->size(storage_.Get());
  }

  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    return table_->read_at(storage_.Get(), at, out);
  }

  //! Whether the erased device writes.
  [[nodiscard]] auto Writable() const noexcept -> bool
  {
    return table_->write_at != nullptr;
  }

  //! The view of a contiguous device; empty for any other.
  [[nodiscard]] auto Contiguous() const noexcept
    -> std::optional<std::span<const std::byte>>
  {
    if (table_->bytes == nullptr) {
      return std::nullopt;
    }
    return table_->bytes(storage_.Get());
  }

  [[nodiscard]] auto WriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
  {
    if (!Writable()) {
      return NotWritable();
    }
    return table_->write_at(storage_.Get(), at, in);
  }

  [[nodiscard]] auto Reserve(const ByteCount size) noexcept -> Result<void>
  {
    if (!Writable()) {
      return NotWritable();
    }
    return table_->reserve(storage_.Get(), size);
  }

  [[nodiscard]] auto Flush(const Durability durability) noexcept -> Result<void>
  {
    if (!Writable()) {
      return NotWritable();
    }
    return table_->flush(storage_.Get(), durability);
  }

private:
  friend class AnyAsyncDevice;

  //! Inline room for a `NativeFile`, a `MappedFile` or an `AsyncFile` on every
  //! platform.
  using Storage = detail::ErasedObject<64>;

  struct Table {
    Result<ByteCount> (*size)(const void* device) noexcept;
    Result<ByteCount> (*read_at)(
      const void* device, ByteOffset at, std::span<std::byte> out) noexcept;
    Result<void> (*write_at)(
      void* device, ByteOffset at, std::span<const std::byte> in) noexcept;
    Result<void> (*reserve)(void* device, ByteCount size) noexcept;
    Result<void> (*flush)(void* device, Durability durability) noexcept;
    std::span<const std::byte> (*bytes)(const void* device) noexcept;
    ReadOperation (*async_read_at)(
      const void* device, ByteOffset at, std::span<std::byte> out) noexcept;
    WriteOperation (*async_write_at)(
      void* device, ByteOffset at, std::span<const std::byte> in) noexcept;
  };

  template <ReadableDevice D>
  static constexpr auto MakeTable() noexcept -> Table
  {
    auto table = Table {
      .size = [](const void* device) noexcept -> Result<ByteCount> {
        return static_cast<const D*>(device)->Size();
      },
      .read_at
      = [](const void* device, const ByteOffset at,
          const std::span<std::byte> out) noexcept -> Result<ByteCount> {
        return static_cast<const D*>(device)->ReadAt(at, out);
      },
      .write_at = nullptr,
      .reserve = nullptr,
      .flush = nullptr,
      .bytes = nullptr,
      .async_read_at = nullptr,
      .async_write_at = nullptr,
    };
    if constexpr (WritableDevice<D>) {
      table.write_at
        = [](void* device, const ByteOffset at,
            const std::span<const std::byte> in) noexcept -> Result<void> {
        return static_cast<D*>(device)->WriteAt(at, in);
      };
      table.reserve
        = [](void* device, const ByteCount size) noexcept -> Result<void> {
        return static_cast<D*>(device)->Reserve(size);
      };
      table.flush = [](void* device,
                      const Durability durability) noexcept -> Result<void> {
        return static_cast<D*>(device)->Flush(durability);
      };
    }
    if constexpr (ContiguousDevice<D>) {
      table.bytes
        = [](const void* device) noexcept -> std::span<const std::byte> {
        return static_cast<const D*>(device)->Bytes();
      };
    }
    if constexpr (AsyncReadableDevice<D>) {
      table.async_read_at
        = [](const void* device, const ByteOffset at,
            const std::span<std::byte> out) noexcept -> ReadOperation {
        return static_cast<const D*>(device)->AsyncReadAt(at, out);
      };
    }
    if constexpr (AsyncWritableDevice<D>) {
      table.async_write_at
        = [](void* device, const ByteOffset at,
            const std::span<const std::byte> in) noexcept -> WriteOperation {
        return static_cast<D*>(device)->AsyncWriteAt(at, in);
      };
    }
    return table;
  }

  template <ReadableDevice D> static auto TableFor() noexcept -> const Table*
  {
    static constexpr Table kTable = MakeTable<D>();
    return &kTable;
  }

  [[nodiscard]] static auto NotWritable() noexcept -> Result<void>
  {
    return ::oxygen::Err(
      std::make_error_code(std::errc::operation_not_supported));
  }

  Storage storage_;
  const Table* table_ { nullptr };
};

static_assert(WritableDevice<AnyDevice>);

//! Owns any asynchronous device, with its type erased, for a coarse boundary.
/*!
 Everything an `AnyDevice` is, plus the awaitable operations: a loader awaits
 payload reads of a file opened through an `IoDriver` and of embedded memory
 through the same type. Every device returns the same operation types, so an
 awaited read through the erased device allocates no coroutine frame.

 An awaited write to a device that does not write, or does not write
 asynchronously, fails with `operation_not_supported`, as `WriteAt` does.
*/
class AnyAsyncDevice {
public:
  //! Takes ownership of `device`.
  template <AsyncReadableDevice D>
    requires(!std::is_same_v<D, AnyAsyncDevice>)
  explicit AnyAsyncDevice(D device) noexcept(
    AnyDevice::Storage::kStoredInline<D>)
    : device_(std::move(device))
  {
  }

  ~AnyAsyncDevice() = default;

  OXYGEN_MAKE_NON_COPYABLE(AnyAsyncDevice)
  OXYGEN_DEFAULT_MOVABLE(AnyAsyncDevice)

  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return device_.Size();
  }

  [[nodiscard]] auto ReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> Result<ByteCount>
  {
    return device_.ReadAt(at, out);
  }

  [[nodiscard]] auto AsyncReadAt(const ByteOffset at,
    const std::span<std::byte> out) const noexcept -> ReadOperation
  {
    return device_.table_->async_read_at(device_.storage_.Get(), at, out);
  }

  //! Whether the erased device writes.
  [[nodiscard]] auto Writable() const noexcept -> bool
  {
    return device_.Writable();
  }

  //! The view of a contiguous device; empty for any other.
  [[nodiscard]] auto Contiguous() const noexcept
    -> std::optional<std::span<const std::byte>>
  {
    return device_.Contiguous();
  }

  [[nodiscard]] auto WriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> Result<void>
  {
    return device_.WriteAt(at, in);
  }

  [[nodiscard]] auto AsyncWriteAt(const ByteOffset at,
    const std::span<const std::byte> in) noexcept -> WriteOperation
  {
    if (device_.table_->async_write_at == nullptr) {
      return WriteOperation::Ready(AnyDevice::NotWritable());
    }
    return device_.table_->async_write_at(device_.storage_.Get(), at, in);
  }

  [[nodiscard]] auto Reserve(const ByteCount size) noexcept -> Result<void>
  {
    return device_.Reserve(size);
  }

  [[nodiscard]] auto Flush(const Durability durability) noexcept -> Result<void>
  {
    return device_.Flush(durability);
  }

private:
  AnyDevice device_;
};

static_assert(AsyncWritableDevice<AnyAsyncDevice>);

} // namespace oxygen::serio
