//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ModelDependency.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import::internal {

namespace {

  //! True when `path` names a network server or device: `\\server`, `//server`,
  //! `\\?\`, `\\.\`. A root name of two separators is never a local drive.
  [[nodiscard]] auto HasNetworkRoot(const std::filesystem::path& path) -> bool
  {
    const auto root = path.root_name().generic_wstring();
    return root.starts_with(L"//");
  }

} // namespace

auto RefusedModelDependency(const ImportRequest& request,
  const std::filesystem::path& file) -> std::optional<std::string>
{
  if (file.empty()) {
    return std::nullopt;
  }
  if (HasNetworkRoot(file)
    && file.root_name() != request.source_path.root_name()) {
    return "which is on a network server the model is not on";
  }
  if (request.source_mounts == nullptr) {
    return std::nullopt;
  }
  auto findings = std::vector<ImportDiagnostic> {};
  if (!request.source_mounts->MountPathOf(file, findings).has_value()) {
    return "which lies in no source mount";
  }
  return std::nullopt;
}

auto ModelDependencyReportPath(const std::filesystem::path& file,
  const std::filesystem::path& model) -> std::string
{
  const auto relative = file.lexically_relative(model.parent_path());
  return (relative.empty() ? file : relative).generic_string();
}

} // namespace oxygen::content::import::internal
