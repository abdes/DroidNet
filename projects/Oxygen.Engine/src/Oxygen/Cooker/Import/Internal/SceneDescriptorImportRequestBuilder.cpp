//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/SceneSource.h>
#include <Oxygen/Cooker/Import/Internal/Utils/DescriptorDocument.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/SceneDescriptorImportSettings.h>

namespace oxygen::content::import::internal {

auto BuildSceneDescriptorRequest(const SceneDescriptorImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  if (settings.descriptor_path.empty()) {
    error_stream << "ERROR: descriptor_path is required\n";
    return std::nullopt;
  }

  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  const auto descriptor_text
    = LoadDescriptorText(descriptor_path, "scene", error_stream);
  if (!descriptor_text.has_value()) {
    return std::nullopt;
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto source = SceneSource::FromDescriptor(
    *descriptor_text, descriptor_path, diagnostics);
  for (const auto& diagnostic : diagnostics) {
    error_stream << "ERROR [" << diagnostic.code << "]: " << diagnostic.message;
    if (!diagnostic.object_path.empty()) {
      error_stream << " (" << diagnostic.object_path << ")";
    }
    error_stream << '\n';
  }
  if (!source.has_value()) {
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = descriptor_path;

  if (settings.cooked_root.empty()) {
    error_stream << "ERROR: --output or --cooked-root is required\n";
    return std::nullopt;
  }

  auto cooked_root = std::filesystem::path(settings.cooked_root);
  if (!cooked_root.is_absolute()) {
    error_stream << "ERROR: cooked root must be an absolute path\n";
    return std::nullopt;
  }
  request.cooked_root = std::move(cooked_root);

  for (const auto& root : settings.cooked_context_roots) {
    const auto path = std::filesystem::path(root);
    if (!path.is_absolute()) {
      error_stream << "ERROR: cooked context roots must be absolute paths\n";
      return std::nullopt;
    }
    request.cooked_context_roots.push_back(path.lexically_normal());
  }

  request.job_name
    = settings.job_name.empty() ? source->name : settings.job_name;
  request.options.with_content_hashing = EffectiveContentHashingEnabled(
    source->content_hashing.value_or(settings.with_content_hashing));

  request.scene_descriptor = ImportRequest::SceneDescriptorPayload {
    .normalized_descriptor_json = *descriptor_text,
  };

  return request;
}

} // namespace oxygen::content::import::internal
