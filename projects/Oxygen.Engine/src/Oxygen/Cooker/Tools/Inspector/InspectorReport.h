//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <exception>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

//! What a report command names on its command line.
struct ReportRequest final {
  std::string cooked_root;
  std::string output;
};

//! Where one report reads and writes.
struct ReportTarget final {
  std::filesystem::path root;
  std::filesystem::path output;
};

//! The cooked root `cooked_root` names, resolved. One that does not exist is a
//! refused input, `ExitCode::kUsage`; one that exists and does not read is a
//! failed run, which the command's reader reports.
[[nodiscard]] auto ResolveCookedRoot(const std::string& cooked_root)
  -> std::expected<std::filesystem::path, toolkit::ToolError>;

//! The root a report reads and the file it writes, after refusing a missing
//! root and a destination that lies in that root.
[[nodiscard]] auto PrepareReport(const ReportRequest& request)
  -> std::expected<ReportTarget, toolkit::ToolError>;

//! Why the row of `entry` could not be read, naming `kind` and the asset.
[[nodiscard]] auto RowFailure(std::string_view kind,
  const cooked::CookedAsset& entry, const std::exception& error) -> std::string;

//! Publishes a report that has a row per asset. A row that could not be read is
//! in the report, so the others stay usable; each of `failures` then fails the
//! run with `ExitCode::kFailed`.
[[nodiscard]] auto PublishRowReport(const std::filesystem::path& output,
  const nlohmann::json& report, std::span<const std::string> failures)
  -> toolkit::ToolResult;

} // namespace oxygen::content::inspection
