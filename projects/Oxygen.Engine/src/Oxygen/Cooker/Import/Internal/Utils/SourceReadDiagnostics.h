//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/api_export.h>
#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import::detail {

//! The error diagnostic of `path`, which holds `size` bytes, refused because
//! the import reads at most `limit` bytes from one source. An unknown `size`
//! is read from the file system; the message names both sizes.
OXGN_COOK_NDAPI auto MakeSourceTooLargeDiagnostic(
  const std::filesystem::path& path, std::optional<uint64_t> size,
  serio::ByteCount limit, std::string object_path = {}) -> ImportDiagnostic;

//! The source-too-large diagnostic of a read of `path` that failed with
//! `error`, when the source limit `limit` refused it; empty for any other
//! failure, which keeps its own diagnostic.
OXGN_COOK_NDAPI auto SourceTooLargeDiagnostic(const std::error_code& error,
  const std::filesystem::path& path, serio::ByteCount limit)
  -> std::optional<ImportDiagnostic>;

} // namespace oxygen::content::import::detail
