//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef> // IWYU pragma: keep
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Tools/Common/ReportFile.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Serio/AtomicFile.h>

namespace oxygen::content::toolkit {

namespace {

  namespace fs = std::filesystem;

  [[nodiscard]] auto IsSeparator(const char character) -> bool
  {
    return character == '/' || character == '\\';
  }

  //! The identity of a path with its links resolved, as far as it exists.
  [[nodiscard]] auto ResolvedIdentity(const fs::path& path) -> std::string
  {
    auto error = std::error_code {};
    const auto resolved = fs::weakly_canonical(base::ToNativePath(path), error);
    return base::PathIdentityKey(error ? path : base::ToLogicalPath(resolved));
  }

  [[nodiscard]] auto Aliases(const fs::path& destination, const fs::path& input)
    -> bool
  {
    if (ResolvedIdentity(destination) == ResolvedIdentity(input)) {
      return true;
    }
    auto error = std::error_code {};
    return fs::equivalent(
      base::ToNativePath(destination), base::ToNativePath(input), error);
  }

  [[nodiscard]] auto IsInside(
    const fs::path& destination, const fs::path& cooked_root) -> bool
  {
    const auto file = ResolvedIdentity(destination);
    const auto root = ResolvedIdentity(cooked_root);
    if (file == root) {
      return true;
    }
    const auto has_root_prefix
      = root.size() < file.size() && file.starts_with(root);
    return has_root_prefix
      && (IsSeparator(root.back()) || IsSeparator(file.at(root.size())));
  }

  [[nodiscard]] auto WriteFailure(const fs::path& destination,
    const std::string_view step, const std::error_code& error)
    -> std::unexpected<ReportFileError>
  {
    return std::unexpected(ReportFileError {
      .code = report_codes::kWriteFailed,
      .message = "failed to " + std::string { step } + " report '"
        + destination.string() + "': " + error.message(),
    });
  }

} // namespace

auto ReportFileError::Text() const -> std::string
{
  return std::string { code } + ": " + message;
}

auto ResolveReportPath(const fs::path& report_path) -> fs::path
{
  return fs::absolute(report_path).lexically_normal();
}

auto CheckReportDestination(const fs::path& destination,
  const ReportGuard& guard) -> std::expected<void, ReportFileError>
{
  for (const auto& input : guard.inputs) {
    if (!input.empty() && Aliases(destination, input)) {
      return std::unexpected(ReportFileError {
        .code = report_codes::kDestinationIsInput,
        .message = "report '" + destination.string()
          + "' would overwrite the input '" + input.string() + "'",
      });
    }
  }
  for (const auto& cooked_root : guard.cooked_roots) {
    if (!cooked_root.empty() && IsInside(destination, cooked_root)) {
      return std::unexpected(ReportFileError {
        .code = report_codes::kDestinationInCookedRoot,
        .message = "report '" + destination.string()
          + "' lies in the cooked source '" + cooked_root.string() + "'",
      });
    }
  }
  return {};
}

auto PublishReport(const fs::path& destination, const std::string_view text)
  -> std::expected<void, ReportFileError>
{
  const auto native = base::ToNativePath(destination);
  if (const auto parent = native.parent_path(); !parent.empty()) {
    auto error = std::error_code {};
    fs::create_directories(parent, error);
    if (error) {
      return WriteFailure(destination, "create the folder of", error);
    }
  }
  const auto written
    = serio::WriteFileAtomically(native, std::as_bytes(std::span(text)));
  if (!written) {
    return WriteFailure(destination, "write", written.error());
  }
  if (written->durability_error) {
    return WriteFailure(destination, "flush", written->durability_error);
  }
  return {};
}

auto CheckToolReportDestination(
  const fs::path& destination, const ReportGuard& guard) -> ToolResult
{
  auto checked = CheckReportDestination(destination, guard);
  if (!checked) {
    return std::unexpected(ToolError::Usage(checked.error().Text()));
  }
  return {};
}

auto PublishToolReport(const fs::path& destination, const std::string_view text)
  -> ToolResult
{
  auto published = PublishReport(destination, text);
  if (!published) {
    return std::unexpected(ToolError::Failed(published.error().Text()));
  }
  return {};
}

} // namespace oxygen::content::toolkit
