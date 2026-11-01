//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <bit>
#include <limits>
#include <type_traits>

namespace oxygen::serio {

//! Declares `T` a record: a fixed-layout struct whose bytes are its encoding,
//! read and written as one copy.
/*!
 Records are opt-in, because a trivially copyable type can still hold a
 pointer, or padding whose bytes must never reach a file. Declare a record
 beside its struct:

 ```cpp
 template <> inline constexpr bool serio::kIsRecord<SaveHeader> = true;
 ```

 A record is a raw wire layout. Copying one checks its layout at compile
 time (`RecordLayout`) and never its values: read alone or in a sequence,
 its enumerations and flags hold whatever bytes the file held, and the code
 that uses a field decides whether to check it. A type that must be checked
 on every read validates in its own `Load` and is not declared a record (see
 `LoadRecord`).
*/
template <class T> inline constexpr bool kIsRecord = false;

//! A type declared a record with `kIsRecord`.
template <class T>
concept Record = kIsRecord<std::remove_cv_t<T>>;

//! A layout whose object bytes are its encoding: trivially copyable, without
//! padding, on a little-endian target.
/*!
 A struct packed to one byte has no padding between its members; any other
 struct must have a unique object representation, which rules out padding
 and floating-point members. Neither reaches inside a member: a padded struct
 nested in a packed record passes the check. Each nested struct is therefore
 itself packed, or has a unique object representation; Oxygen's pak records
 declare theirs inside the same packed block.

 A record holds no C++ `bool`, whose invalid byte values are undefined
 behavior to read; a 0/1 flag is an integer.
*/
template <class T>
concept RecordLayout = std::is_trivially_copyable_v<T>
  && (alignof(T) == 1 || std::has_unique_object_representations_v<T>)
  && std::endian::native == std::endian::little;

//! An element a sequence moves with its neighbours as one copy: a record
//! with a valid layout, or an arithmetic value other than `bool` on a
//! little-endian target.
/*!
 A record whose layout fails `RecordLayout` is no bulk element: a sequence of
 it reaches the record's own `Load`/`Store`, whose layout assertion rejects
 it, so sequences never copy a layout that single records refuse.
*/
template <class T>
concept BulkElement = (Record<T> && RecordLayout<T>)
  || (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>
    && std::endian::native == std::endian::little);

// Floating-point members of records are copied as their IEEE-754 bytes.
static_assert(std::numeric_limits<float>::is_iec559
  && std::numeric_limits<double>::is_iec559);

} // namespace oxygen::serio
