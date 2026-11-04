//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>

namespace oxygen::content::import {
class AsyncImportService; // forward-declared
class ModelImportRecord;
struct SceneImportSettings;
} // namespace oxygen::content::import

namespace oxygen::content::import::tool {

class IMessageWriter;
struct GlobalOptions;

[[nodiscard]] auto RunImportJob(const ImportRequest& request,
  oxygen::observer_ptr<IMessageWriter> writer, std::string_view report_path,
  std::string_view command_line, bool enable_tui,
  oxygen::observer_ptr<oxygen::content::import::AsyncImportService> service,
  std::shared_ptr<ModelImportRecord> publication = {})
  -> std::expected<void, std::error_code>;

//! A single-job command that stopped before it could submit its job.
struct ImportRefusal {
  std::string_view report_path;
  std::string_view command_line;
  //! The job type the report names, as for a job that ran.
  std::string_view job_type;
  std::filesystem::path source_path;
  std::filesystem::path cooked_root;
  std::span<const ImportDiagnostic> diagnostics;
};

//! Writes the requested report of a refused job, a failed entry holding the
//! typed refusal, and returns the refusal to return from the command.
/*!
 The command has already printed the diagnostics. A destination that aliases
 `source_path` or lies in `cooked_root` gets no report and its own printed
 refusal, as does a report that cannot be written.
*/
[[nodiscard]] auto RefuseImportJob(
  const ImportRefusal& refusal, oxygen::observer_ptr<IMessageWriter> writer)
  -> std::unexpected<std::error_code>;

struct SourceMountOptions;

[[nodiscard]] auto RunSceneImportJob(const SceneImportSettings& settings,
  ImportFormat format, const SourceMountOptions& mount_options,
  const GlobalOptions& globals) -> std::expected<void, std::error_code>;

} // namespace oxygen::content::import::tool
