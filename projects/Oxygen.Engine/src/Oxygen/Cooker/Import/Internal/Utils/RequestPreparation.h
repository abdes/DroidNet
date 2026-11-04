//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportRequestCodes.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>

namespace oxygen::content::import::internal {

//! Adds a refusal of the setting at `json_pointer`, the manifest key that
//! states it; see `request_codes`.
inline auto RefuseSetting(std::vector<ImportDiagnostic>& diagnostics,
  const std::string_view code, std::string message, std::string json_pointer)
  -> void
{
  diagnostics.push_back(ImportDiagnostic {
    .severity = ImportSeverity::kError,
    .code = std::string { code },
    .message = std::move(message),
    .json_pointer = std::move(json_pointer),
  });
}

//! Parses the option `key` with `parser`, refusing a value it does not take.
template <typename Parser>
[[nodiscard]] auto ParseOption(const std::string_view key,
  const std::string_view value, const Parser& parser,
  std::vector<ImportDiagnostic>& diagnostics) -> decltype(parser(value))
{
  auto parsed = parser(value);
  if (!parsed.has_value()) {
    RefuseSetting(diagnostics, request_codes::kOptionInvalid,
      fmt::format("invalid {}: '{}'", key, value), fmt::format("/{}", key));
  }
  return parsed;
}

//! Sets the cooked root, which may be empty: the request then resolves its
//! default one.
[[nodiscard]] auto ApplyCookedRoot(std::string_view cooked_root,
  ImportRequest& request, std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Sets the cooked root of a job that cannot resolve a default one.
[[nodiscard]] auto RequireCookedRoot(std::string_view cooked_root,
  ImportRequest& request, std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Adds the job's cooked context roots.
[[nodiscard]] auto ApplyCookedContextRoots(std::span<const std::string> roots,
  ImportRequest& request, std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Names the job `name`, or after `request.source_path` when `name` is empty.
auto ApplyJobName(std::string_view name, ImportRequest& request) -> void;

//! Refuses a sidecar with both or neither of a source and inline bindings.
[[nodiscard]] auto CheckSidecarSource(bool has_source, bool has_inline,
  std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Refuses a sidecar's missing or non-canonical target scene.
[[nodiscard]] auto CheckTargetScene(
  std::string_view scene, std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! Refuses an input source not named `*.input.json`.
[[nodiscard]] auto CheckInputSourceName(const std::filesystem::path& source,
  std::vector<ImportDiagnostic>& diagnostics) -> bool;

//! What a prepared descriptor's request starts from.
struct DescriptorRequestInputs final {
  observer_ptr<const std::filesystem::path> descriptor_path;
  observer_ptr<const ImportSourceDocument> document;
  std::string_view cooked_root;
  std::span<const std::string> cooked_context_roots;
  std::shared_ptr<const CapturedInputSet> captured_inputs;
};

//! Starts the request of a descriptor that its preparation accepted.
[[nodiscard]] auto StartDescriptorRequest(DescriptorRequestInputs inputs,
  std::vector<ImportDiagnostic>& diagnostics) -> std::optional<ImportRequest>;

} // namespace oxygen::content::import::internal
