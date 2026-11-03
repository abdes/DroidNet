//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data::loose_cooked {

//! Refuses `relpath` unless a loose cooked index may list it.
/*!
 A listed path is relative to the cooked root: not empty, `/`-separated, with
 no `:`, no leading or trailing `/`, no `//`, and no `.` or `..` segment. It
 names none of the root's reserved paths (`IsReservedRelPath`). Writers check
 what they list, and readers what they load, with this one rule.

 @throw std::runtime_error naming the rule `relpath` breaks.
*/
OXGN_DATA_API auto RequireIndexRelPath(std::string_view relpath) -> void;

//! Whether `relpath` is one of the root's own files, never an inventory path:
//! the index, the writer lease, the generation lease, or the reader leases
//! folder and what it holds.
OXGN_DATA_NDAPI auto IsReservedRelPath(std::string_view relpath) noexcept
  -> bool;

//! The reader lease of `commit`, relative to the cooked root:
//! `.leases/<commit>.lock`, the commit in decimal.
OXGN_DATA_NDAPI auto LeaseRelPath(uint64_t commit) -> std::string;

//! The commit a file name of the leases folder leases, `<commit>.lock`; none
//! for any other name.
OXGN_DATA_NDAPI auto LeasedCommitOf(std::string_view file_name) noexcept
  -> std::optional<uint64_t>;

//! The content name of a file whose layout path is `relpath`:
//! `<stem>.<digest>.<extension>`, the digest being the first 16 lowercase
//! hexadecimal digits of `sha256`.
/*!
 `Materials/Red.omat` with a digest starting `3f9a07c1d2b4e6a8` is
 `Materials/Red.3f9a07c1d2b4e6a8.omat`; a name without an extension ends with
 its digest. Every file of a loose root but its data files and its index has
 a content name, so a commit never changes a file another commit lists.
*/
OXGN_DATA_NDAPI auto ContentRelPath(
  std::string_view relpath, const base::Sha256Digest& sha256) -> std::string;

//! Whether `relpath` has the form `ContentRelPath` gives, whatever its digest.
OXGN_DATA_NDAPI auto IsContentRelPath(std::string_view relpath) noexcept
  -> bool;

//! Whether `relpath` names what only a writer produces: a content-named file,
//! or the sibling an atomic publication streams into.
/*!
 Such a file no commit lists is a running session's, or one a crashed writer
 left: the next writer deletes it, and checks accept it at mount
 (`kUncommitted`). Any other unlisted file is someone else's.
*/
OXGN_DATA_NDAPI auto IsWriterOutputRelPath(std::string_view relpath) noexcept
  -> bool;

} // namespace oxygen::data::loose_cooked
