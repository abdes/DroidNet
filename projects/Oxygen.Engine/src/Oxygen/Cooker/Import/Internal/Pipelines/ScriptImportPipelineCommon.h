//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>

namespace oxygen::content::import::detail::script_import {

inline auto DeriveScriptName(const std::filesystem::path& source_path)
  -> std::string
{
  auto stem = source_path.stem().string();
  if (!stem.empty()) {
    return stem;
  }
  return "Script";
}

//! The external source path a script asset stores: its source's mount path,
//! `/<Mount>/<path in mount>`. Nothing when the source lies in no mount.
inline auto BuildExternalSourcePath(const ImportRequest& request)
  -> std::optional<std::string>
{
  if (!request.source_mount_path.starts_with('/')) {
    return std::nullopt;
  }
  return request.source_mount_path;
}

inline auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
  const ImportSeverity severity, std::string code, std::string message) -> void
{
  session.AddDiagnostic({
    .severity = severity,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = request.source_path.string(),
    .object_path = {},
  });
}

inline auto AddDiagnosticAtPath(ImportSession& session,
  const ImportRequest& request, const ImportSeverity severity, std::string code,
  std::string message, std::string object_path) -> void
{
  session.AddDiagnostic({
    .severity = severity,
    .code = std::move(code),
    .message = std::move(message),
    .source_path = request.source_path.string(),
    .object_path = std::move(object_path),
  });
}

} // namespace oxygen::content::import::detail::script_import
