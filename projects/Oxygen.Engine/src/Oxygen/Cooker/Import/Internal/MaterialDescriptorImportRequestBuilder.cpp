//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/ImportSourceDocument.h>
#include <Oxygen/Cooker/Import/Internal/MaterialSource.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/MaterialDescriptorImportSettings.h>

namespace oxygen::content::import::internal {

auto BuildMaterialDescriptorRequest(
  const MaterialDescriptorImportSettings& settings, std::ostream& error_stream,
  std::shared_ptr<const CapturedInputSet> captured_inputs)
  -> std::optional<ImportRequest>
{
  if (settings.descriptor_path.empty()) {
    error_stream << "ERROR: descriptor_path is required\n";
    return std::nullopt;
  }

  const auto descriptor_path
    = std::filesystem::path(settings.descriptor_path).lexically_normal();
  auto document = ImportSourceDocument::Load(
    descriptor_path, "material", error_stream, captured_inputs.get());
  if (!document.has_value()) {
    return std::nullopt;
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto prepared = MaterialSource::FromDescriptor(
    document->text, descriptor_path, settings.job_name, diagnostics);
  for (const auto& diagnostic : diagnostics) {
    error_stream << "ERROR [" << diagnostic.code << "]: " << diagnostic.message;
    if (!diagnostic.object_path.empty()) {
      error_stream << " (" << diagnostic.object_path << ")";
    }
    error_stream << '\n';
  }
  if (!prepared) {
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = descriptor_path;
  request.captured_inputs = std::move(captured_inputs);
  request.preparation_inputs.push_back(document->Observation());

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

  request.job_name = prepared->name;
  request.options.with_content_hashing = EffectiveContentHashingEnabled(
    prepared->content_hashing.value_or(settings.with_content_hashing));

  request.material_descriptor = ImportRequest::MaterialDescriptorPayload {
    .normalized_descriptor_json = std::move(document->text),
  };

  return request;
}

} // namespace oxygen::content::import::internal
