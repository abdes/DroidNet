//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::toolkit {

//! The refusal codes of a report file, printed by the tool that refuses it.
namespace report_codes {
  //! The destination is a file the run reads, under any spelling or hard link.
  inline constexpr std::string_view kDestinationIsInput
    = "report.destination_is_input";
  //! The destination is a cooked root, or lies inside one.
  inline constexpr std::string_view kDestinationInCookedRoot
    = "report.destination_in_cooked_root";
  //! The report could not be written; the destination keeps what it held.
  inline constexpr std::string_view kWriteFailed = "report.write_failed";
} // namespace report_codes

//! Why a report file was refused or not written.
struct ReportFileError {
  std::string_view code;
  std::string message;

  //! The refusal as a tool prints it: `<code>: <message>`.
  [[nodiscard]] auto Text() const -> std::string;
};

//! What a run's report must not replace.
struct ReportGuard {
  //! The files the run reads. An empty path is skipped.
  std::vector<std::filesystem::path> inputs;
  //! The cooked sources the run reads or writes: loose roots or paks. An empty
  //! path is skipped.
  std::vector<std::filesystem::path> cooked_roots;
};

//! The file a report option names: absolute and lexically normal. A relative
//! path is relative to the working directory, as every other path option.
[[nodiscard]] auto ResolveReportPath(const std::filesystem::path& report_path)
  -> std::filesystem::path;

//! Refuses a destination that aliases an input or lies in a cooked root.
/*!
 Spellings are compared after resolving links and, on Windows, case. A file with
 another name for the same data, such as a hard link, also aliases it.
*/
[[nodiscard]] auto CheckReportDestination(
  const std::filesystem::path& destination, const ReportGuard& guard)
  -> std::expected<void, ReportFileError>;

//! Publishes a whole report atomically, creating its folder: the destination
//! holds the old file or the complete new one, and a hard link to the old file
//! keeps its bytes.
[[nodiscard]] auto PublishReport(const std::filesystem::path& destination,
  std::string_view text) -> std::expected<void, ReportFileError>;

//! `CheckReportDestination` for a command: a refusal is `ExitCode::kUsage`.
[[nodiscard]] auto CheckToolReportDestination(
  const std::filesystem::path& destination, const ReportGuard& guard)
  -> ToolResult;

//! `PublishReport` for a command: a failure is `ExitCode::kFailed`.
[[nodiscard]] auto PublishToolReport(const std::filesystem::path& destination,
  std::string_view text) -> ToolResult;

} // namespace oxygen::content::toolkit
