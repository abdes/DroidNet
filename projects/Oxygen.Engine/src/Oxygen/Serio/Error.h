//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <system_error>
#include <type_traits>

#include <Oxygen/Serio/api_export.h>

namespace oxygen::serio {

//! Serio's error codes and the portable conditions OS errors compare to.
/*!
 Two kinds of value share this enumeration:

 - **Serio's own codes**, returned by memory devices, regions, streams and the
   serializer: `kEndOfData`, `kOutOfBounds`, `kTooLarge`, `kDigestMismatch`,
   and `kIo` for a failed page-in under a mapped file's copy.
 - **Conditions** that OS errors keep their own code for and compare equal to:
   `kNotFound`, `kAccessDenied`, `kSharingViolation`, `kNoSpace` and `kIo`.

 `Error` is an error condition enumeration, so a caller writes
 `error == serio::Error::kNotFound` once and it holds for every device. It is
 deliberately not also an error code enumeration: one enumeration that is both
 makes that comparison ambiguous. Serio makes its own codes explicitly, with
 `make_error_code(Error::kEndOfData)`.

 ```cpp
 auto file = NativeFile::Open(path, {});
 if (!file && file.error() == serio::Error::kNotFound) { ... }
 ```

 @see make_error_code, make_error_condition
*/
enum class Error : uint8_t {
  kEndOfData = 1, //!< Fewer bytes remain than an exact read needs.
  kOutOfBounds, //!< An offset or a range lies outside its region or stream.
  kTooLarge, //!< A length exceeds what the reader allows or what remains.
  kDigestMismatch, //!< Bytes read do not match their expected digest.
  kNotFound, //!< The file or a directory of its path does not exist.
  kAccessDenied, //!< The caller may not open or modify the file.
  kSharingViolation, //!< Another handle's sharing mode refuses the open.
  kNoSpace, //!< The volume or the caller's quota is full.
  kIo, //!< The device failed to read or write.
};

//! The category of `serio::Error` codes and conditions.
OXGN_SERIO_NDAPI auto ErrorCategory() noexcept -> const std::error_category&;

//! Makes a `std::error_code` in Serio's category.
[[nodiscard]] inline auto make_error_code(const Error error) noexcept
  -> std::error_code
{
  return { static_cast<int>(error), ErrorCategory() };
}

//! Makes a `std::error_condition` in Serio's category.
[[nodiscard]] inline auto make_error_condition(const Error error) noexcept
  -> std::error_condition
{
  return { static_cast<int>(error), ErrorCategory() };
}

} // namespace oxygen::serio

template <>
struct std::is_error_condition_enum<oxygen::serio::Error> : std::true_type { };
