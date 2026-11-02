//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/Result.h>
#include <Oxygen/Serio/Device.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! A read-only view of a whole file, mapped into the address space.
/*!
 A `MappedFile` serves bytes that are parsed in place or read at random:
 metadata, indexes, directories, inspection tools. It is opened like a
 `NativeFile`, from a logical path, sharing reads and deletion, so a file
 published by atomic replacement is replaced (never modified) while it is
 mapped, and the mapping keeps showing the complete version it opened.

 - `Bytes()` is the view; parsers read it without allocating or copying.
 - `ReadAt` copies from the view inside a guarded copy that turns a failed
   page-in into `kIo`, so every copied read stays a `Result`.
 - A mapping of a large file only reserves address space, so mapping a
   multi-GB file to parse a small part of it is cheap.

 @warning A view cannot be guarded: a device failure while code reads
 `Bytes()` directly is fatal to the process. Parse views only for metadata
 read once, and copy payloads with `ReadAt` or read them from a `NativeFile`.

 A `MappedFile` has no awaitable operations: nothing awaited may fault on a
 loop thread.
*/
class MappedFile {
public:
  //! Maps the whole file at `path`, read-only.
  /*!
   `sharing` is what other handles may do while the file is open; by default
   they read and may replace or delete it. `kReadWriteDelete` maps a file
   that another handle holds open for writing, such as a resource data file
   writers append to; the view shows the bytes the file held when it was
   mapped.

   @return The mapped file, or the OS error (`kNotFound`, `kAccessDenied`,
           `kSharingViolation` compare equal to it). An empty file maps to an
           empty view.
  */
  OXGN_SERIO_NDAPI static auto Open(const std::filesystem::path& path,
    Sharing sharing = Sharing::kReadDelete) noexcept -> Result<MappedFile>;

  //! Maps the whole file that the open, readable `handle` refers to.
  /*!
   The view shows the file version `handle` opened, even after its path was
   atomically replaced, so code that already reads a file through a handle
   maps those same bytes. The caller keeps owning `handle`: the mapping does
   not need it once this returns. `path` is the file's logical path, which
   `Path()` returns.

   @return The mapped file, or the OS error. An empty file maps to an empty
           view.
  */
  OXGN_SERIO_NDAPI static auto Map(NativeHandle handle,
    std::filesystem::path path) noexcept -> Result<MappedFile>;

  OXGN_SERIO_API ~MappedFile() noexcept;

  OXYGEN_MAKE_NON_COPYABLE(MappedFile)

  OXGN_SERIO_API MappedFile(MappedFile&& other) noexcept;
  OXGN_SERIO_API auto operator=(MappedFile&& other) noexcept -> MappedFile&;

  //! The size of the mapped file, as it was when it was opened.
  [[nodiscard]] auto Size() const noexcept -> Result<ByteCount>
  {
    return ::oxygen::Ok(CountOf(size_));
  }

  //! Copies up to `out.size()` bytes at `at`; fewer when the view ends first.
  //! A failed page-in returns `kIo`.
  OXGN_SERIO_NDAPI auto ReadAt(ByteOffset at,
    std::span<std::byte> out) const noexcept -> Result<ByteCount>;

  //! The mapped bytes. See the class warning: a failed page-in under a direct
  //! read of the view is fatal.
  [[nodiscard]] auto Bytes() const noexcept -> std::span<const std::byte>
  {
    return { view_, size_ };
  }

  //! Hints that the bytes of `range` will be read soon, without waiting.
  //! Ranges past the end are clipped; the hint may be ignored.
  OXGN_SERIO_API auto Prefetch(ByteRange range) const noexcept -> void;

  //! The logical path the file was opened from.
  [[nodiscard]] auto Path() const noexcept -> const std::filesystem::path&
  {
    return path_;
  }

private:
  MappedFile(std::byte* view, size_t size, std::filesystem::path path) noexcept;

  auto Close() noexcept -> void;

  //! The mapped pages, for the OS calls that take a mutable address. The
  //! mapping is read-only: nothing writes through it.
  [[nodiscard]] auto Pages() const noexcept -> std::span<std::byte>
  {
    return { view_, size_ };
  }

  //! Mutable only because the OS APIs that unmap or advise take a mutable
  //! address; the pages are mapped read-only and exposed as `const`.
  std::byte* view_ { nullptr };
  size_t size_ { 0 };
  std::filesystem::path path_;
};

static_assert(ContiguousDevice<MappedFile>);
static_assert(!WritableDevice<MappedFile>);

} // namespace oxygen::serio
