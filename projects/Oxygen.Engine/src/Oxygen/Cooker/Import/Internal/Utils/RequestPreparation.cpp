//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/Internal/Utils/RequestPreparation.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>

namespace oxygen::content::import::internal {

auto ApplyCookedRoot(const std::string_view cooked_root, ImportRequest& request,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  if (cooked_root.empty()) {
    return true;
  }
  auto root = std::filesystem::path(cooked_root);
  if (!root.is_absolute()) {
    RefuseSetting(diagnostics, request_codes::kCookedRootNotAbsolute,
      fmt::format("cooked root must be an absolute path: '{}'", cooked_root),
      "/output");
    return false;
  }
  request.cooked_root = std::move(root);
  return true;
}

auto RequireCookedRoot(const std::string_view cooked_root,
  ImportRequest& request, std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  if (cooked_root.empty()) {
    RefuseSetting(diagnostics, request_codes::kCookedRootMissing,
      "--output or --cooked-root is required", "/output");
    return false;
  }
  return ApplyCookedRoot(cooked_root, request, diagnostics);
}

auto ApplyCookedContextRoots(const std::span<const std::string> roots,
  ImportRequest& request, std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  for (const auto& root : roots) {
    const auto path = std::filesystem::path(root);
    if (!path.is_absolute()) {
      RefuseSetting(diagnostics, request_codes::kContextRootNotAbsolute,
        fmt::format("cooked context roots must be absolute paths: '{}'", root),
        "/cooked_context_roots");
      return false;
    }
    request.cooked_context_roots.push_back(path.lexically_normal());
  }
  return true;
}

auto ApplyJobName(const std::string_view name, ImportRequest& request) -> void
{
  if (!name.empty()) {
    request.job_name = std::string { name };
  } else if (auto stem = request.source_path.stem().string(); !stem.empty()) {
    request.job_name = std::move(stem);
  }
}

auto CheckSidecarSource(const bool has_source, const bool has_inline,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  if (has_source == has_inline) {
    RefuseSetting(diagnostics, request_codes::kSourceConflict,
      "exactly one of a source file and inline bindings must be provided",
      "/source");
    return false;
  }
  return true;
}

auto CheckTargetScene(const std::string_view scene,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  if (scene.empty()) {
    RefuseSetting(diagnostics, request_codes::kTargetSceneMissing,
      "target_scene_virtual_path is required", "/target_scene_virtual_path");
    return false;
  }
  if (!IsCanonicalVirtualPath(scene)) {
    RefuseSetting(diagnostics, request_codes::kTargetSceneNotCanonical,
      fmt::format("target_scene_virtual_path must be a canonical virtual "
                  "path: '{}'",
        scene),
      "/target_scene_virtual_path");
    return false;
  }
  return true;
}

auto CheckInputSourceName(const std::filesystem::path& source,
  std::vector<ImportDiagnostic>& diagnostics) -> bool
{
  if (source.extension() != ".json"
    || !source.stem().string().ends_with(".input")) {
    RefuseSetting(diagnostics, request_codes::kInputSourceNameInvalid,
      fmt::format("input source must be '*.input.json': '{}'", source.string()),
      "/source");
    return false;
  }
  return true;
}

auto StartDescriptorRequest(DescriptorRequestInputs inputs,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>
{
  auto request = ImportRequest {};
  request.source_path = *inputs.descriptor_path;
  request.captured_inputs = std::move(inputs.captured_inputs);
  request.preparation_inputs.push_back(inputs.document->Observation());
  if (!RequireCookedRoot(inputs.cooked_root, request, diagnostics)
    || !ApplyCookedContextRoots(
      inputs.cooked_context_roots, request, diagnostics)) {
    return std::nullopt;
  }
  return request;
}

} // namespace oxygen::content::import::internal
