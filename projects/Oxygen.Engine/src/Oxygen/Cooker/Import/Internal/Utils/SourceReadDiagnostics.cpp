//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import::detail {

auto MakeSourceTooLargeDiagnostic(const std::filesystem::path& path,
  std::optional<uint64_t> size, const serio::ByteCount limit,
  std::string object_path) -> ImportDiagnostic
{
  if (!size) {
    auto error = std::error_code {};
    const auto measured
      = std::filesystem::file_size(base::ToNativePath(path), error);
    if (!error) {
      size = measured;
    }
  }
  const auto held = size ? std::to_string(*size) + " bytes" : "more bytes";
  return ImportDiagnostic {
    .severity = ImportSeverity::kError,
    .code = std::string(kSourceTooLargeCode),
    .message = "Source '" + path.string() + "' holds " + held
      + ", more than the import reads from one source ("
      + std::to_string(limit.get()) + " bytes, max_source_bytes)",
    .source_path = path.string(),
    .object_path = std::move(object_path),
    .object_name = {},
  };
}

auto SourceTooLargeDiagnostic(const std::error_code& error,
  const std::filesystem::path& path, const serio::ByteCount limit)
  -> std::optional<ImportDiagnostic>
{
  if (error != serio::Error::kTooLarge) {
    return std::nullopt;
  }
  return MakeSourceTooLargeDiagnostic(path, std::nullopt, limit);
}

} // namespace oxygen::content::import::detail
