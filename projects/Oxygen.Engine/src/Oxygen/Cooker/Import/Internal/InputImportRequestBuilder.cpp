//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>
#include <Oxygen/Cooker/Import/InputImportSettings.h>

namespace oxygen::content::import::internal {

namespace {

  auto TrimInPlace(std::string& value) -> void
  {
    const auto not_space = [](const unsigned char ch) -> bool {
      return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
    };

    const auto first
      = std::find_if(value.begin(), value.end(), [&](const char ch) -> bool {
          return not_space(static_cast<unsigned char>(ch));
        });
    const auto last
      = std::find_if(value.rbegin(), value.rend(), [&](char ch) -> bool {
          return not_space(static_cast<unsigned char>(ch));
        });

    if (first == value.end()) {
      value.clear();
      return;
    }
    value = std::string(first, last.base());
  }

  auto IsLikelyInputSourcePath(const std::filesystem::path& path) -> bool
  {
    const auto ext = path.extension().string();
    if (ext != ".json") {
      return false;
    }
    return path.stem().string().ends_with(".input");
  }

} // namespace

auto BuildInputImportRequest(const InputImportSettings& settings,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  return BuildInputImportRequest(settings, {}, {}, error_stream);
}

auto BuildInputImportRequest(const InputImportSettings& settings,
  std::string job_id, std::vector<std::string> depends_on,
  std::ostream& error_stream) -> std::optional<ImportRequest>
{
  if (settings.source_path.empty()) {
    error_stream << "ERROR: source_path is required\n";
    return std::nullopt;
  }

  auto request = ImportRequest {};
  request.source_path = settings.source_path;
  request.asset_virtual_path = settings.virtual_path;
  request.input = ImportRequest::InputPayload {};

  if (!settings.cooked_root.empty()) {
    auto cooked_root = std::filesystem::path(settings.cooked_root);
    if (!cooked_root.is_absolute()) {
      error_stream << "ERROR: cooked root must be an absolute path\n";
      return std::nullopt;
    }
    request.cooked_root = std::move(cooked_root);
  }

  for (const auto& root : settings.cooked_context_roots) {
    const auto path = std::filesystem::path(root);
    if (!path.is_absolute()) {
      error_stream << "ERROR: cooked context roots must be absolute paths\n";
      return std::nullopt;
    }
    request.cooked_context_roots.push_back(path.lexically_normal());
  }

  // Keep the currently requested hashing default for input jobs.
  request.options.with_content_hashing
    = EffectiveContentHashingEnabled(request.options.with_content_hashing);

  TrimInPlace(job_id);
  for (auto& dep : depends_on) {
    TrimInPlace(dep);
  }
  depends_on.erase(
    std::remove_if(depends_on.begin(), depends_on.end(),
      [](const std::string& dep) -> bool { return dep.empty(); }),
    depends_on.end());

  if (!job_id.empty()) {
    request.orchestration = ImportRequest::OrchestrationMetadata {
      .job_id = std::move(job_id),
      .depends_on = std::move(depends_on),
    };
  } else if (!depends_on.empty()) {
    error_stream << "ERROR: depends_on requires a non-empty job_id\n";
    return std::nullopt;
  }

  if (!IsLikelyInputSourcePath(request.source_path)) {
    error_stream << "ERROR: input source must be '*.input.json'\n";
    return std::nullopt;
  }

  return request;
}

} // namespace oxygen::content::import::internal
