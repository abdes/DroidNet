//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/Tools/Inspector/InspectorReport.h>

namespace oxygen::content::inspection {

auto ResolveCookedRoot(const std::string& cooked_root)
  -> std::expected<std::filesystem::path, toolkit::ToolError>
{
  auto error = std::error_code {};
  auto root
    = std::filesystem::canonical(base::ToNativePath(cooked_root), error);
  if (error) {
    return std::unexpected(toolkit::ToolError::Usage(fmt::format(
      "cannot read cooked root '{}': {}", cooked_root, error.message())));
  }
  return root;
}

auto PrepareReport(const ReportRequest& request)
  -> std::expected<ReportTarget, toolkit::ToolError>
{
  auto root = ResolveCookedRoot(request.cooked_root);
  if (!root) {
    return std::unexpected(std::move(root.error()));
  }
  auto target = ReportTarget {
    .root = std::move(*root),
    .output = toolkit::ResolveReportPath(request.output),
  };
  if (auto safe = toolkit::CheckToolReportDestination(target.output,
        {
          .inputs = {},
          .cooked_roots = { target.root },
        });
    !safe) {
    return std::unexpected(std::move(safe.error()));
  }
  return target;
}

auto RowFailure(const std::string_view kind, const cooked::CookedAsset& entry,
  const std::exception& error) -> std::string
{
  return fmt::format("{} '{}' ({}): {}", kind, entry.virtual_path,
    nostd::to_string(entry.key), error.what());
}

auto PublishRowReport(const std::filesystem::path& output,
  const nlohmann::json& report, const std::span<const std::string> failures)
  -> toolkit::ToolResult
{
  if (auto published
    = toolkit::PublishToolReport(output, report.dump(2) + '\n');
    !published) {
    return published;
  }
  if (failures.empty()) {
    return {};
  }
  auto message = std::string {};
  for (const auto& failure : failures) {
    message += message.empty() ? failure : "; " + failure;
  }
  return std::unexpected(toolkit::ToolError::Failed(std::move(message)));
}

} // namespace oxygen::content::inspection
