//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <filesystem>
#include <string_view>

#include <Oxygen/Cooker/Import/InputImportRequestBuilder.h>

namespace oxygen::content::import::internal {

namespace {

  auto TrimInPlace(std::string& value) -> void
  {
    const auto not_space = [](const unsigned char ch) {
      return ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r';
    };

    const auto first = std::find_if(value.begin(), value.end(),
      [&](const char ch) { return not_space(static_cast<unsigned char>(ch)); });
    const auto last = std::find_if(value.rbegin(), value.rend(),
      [&](char ch) { return not_space(static_cast<unsigned char>(ch)); });

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
    const auto stem = path.stem().string();
    return stem.ends_with(".input") || stem.ends_with(".input-action");
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
  request.options.input = ImportOptions::InputTuning {};

  // Keep the currently requested hashing default for input jobs.
  request.options.with_content_hashing
    = EffectiveContentHashingEnabled(request.options.with_content_hashing);

  TrimInPlace(job_id);
  for (auto& dep : depends_on) {
    TrimInPlace(dep);
  }
  depends_on.erase(std::remove_if(depends_on.begin(), depends_on.end(),
                     [](const std::string& dep) { return dep.empty(); }),
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
    error_stream << "ERROR: input source must be '*.input.json' or "
                    "'*.input-action.json'\n";
    return std::nullopt;
  }

  return request;
}

} // namespace oxygen::content::import::internal
