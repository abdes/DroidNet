//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AsyncFile.h>

namespace oxygen::content::internal {

//! Verifies the CRC32 of the pak `file` opened through the loader's driver,
//! awaiting its reads on the loop.
/*!
 Reads the footer, checks it (`PakFile::CheckFooter`), and hashes the whole
 file except the footer's CRC field with `serio::AsyncComputeCrc32`, so no
 thread blocks to read the pak. A verified mount refuses a pak without a
 CRC32.

 @throw data::CookedOpenError `kUnverifiable` for a pak without a CRC32,
   `kCorrupt` for a malformed footer or a CRC32 mismatch.
 @throw std::system_error When the file cannot be read.
*/
auto VerifyPakAsync(
  const serio::AsyncFile& file, const std::filesystem::path& path) -> co::Co<>;

} // namespace oxygen::content::internal
