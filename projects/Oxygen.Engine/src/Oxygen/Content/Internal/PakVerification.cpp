//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <span>
#include <string>
#include <system_error>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Internal/PakVerification.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/Serio/AsyncDevice.h>
#include <Oxygen/Serio/AsyncFile.h>
#include <Oxygen/Serio/Integrity.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::internal {

auto VerifyPakAsync(
  const serio::AsyncFile& file, const std::filesystem::path& path) -> co::Co<>
{
  using data::CookedOpenError;
  using data::CookedOpenFailure;
  using data::pak::core::PakFooter;

  const auto size = file.Size();
  if (!size) {
    throw std::system_error(size.error(), "size of '" + path.string() + "'");
  }
  if (size->get() < sizeof(PakFooter)) {
    throw CookedOpenError(
      CookedOpenFailure::kCorrupt, "Pak is smaller than its footer");
  }
  auto footer = PakFooter {};
  if (const auto read = co_await serio::AsyncReadExactlyAt(file,
        serio::ByteOffset { size->get() - sizeof(PakFooter) },
        std::as_writable_bytes(std::span { &footer, 1 }));
    !read) {
    throw std::system_error(
      read.error(), "read the footer of '" + path.string() + "'");
  }
  PakFile::CheckFooter(footer, size->get());
  const auto check = PakFile::CrcCheckOf(footer, size->get());
  if (!check) {
    LOG_F(ERROR, "Verified open of a pak without CRC32 path={}", path.string());
    throw CookedOpenError(
      CookedOpenFailure::kUnverifiable, "Pak holds no CRC32 to verify");
  }
  const auto crc = co_await serio::AsyncComputeCrc32(
    file, std::vector<serio::ByteRange> { check->crc_field });
  if (!crc) {
    throw std::system_error(
      crc.error(), "read '" + path.string() + "' for CRC32");
  }
  PakFile::CheckCrc32(*check, *crc, path);
}

} // namespace oxygen::content::internal
