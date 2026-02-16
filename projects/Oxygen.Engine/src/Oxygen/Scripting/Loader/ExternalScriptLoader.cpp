//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>

#include <Oxygen/Scripting/Loader/ExternalScriptLoader.h>

namespace oxygen::scripting {

namespace {

  auto NormalizeScriptPath(std::string_view script_id) -> std::filesystem::path
  {
    std::filesystem::path path { script_id };
    if (path.extension().empty()) {
      path.replace_extension(".luau");
    }
    return path.lexically_normal();
  }

  auto HasParentTraversal(const std::filesystem::path& path) -> bool
  {
    return std::ranges::any_of(
      path, [](const auto& segment) { return segment == ".."; });
  }

} // namespace

ExternalScriptLoader::ExternalScriptLoader(
  std::shared_ptr<const PathFinderConfig> config,
  std::filesystem::path working_directory)
  : path_finder_(std::move(config), std::move(working_directory))
{
}

auto ExternalScriptLoader::LoadScript(const std::string_view script_id) const
  -> ScriptLoadResult
{
  if (script_id.empty()) {
    return ScriptLoadResult {
      .ok = false,
      .source_text = {},
      .chunk_name = {},
      .error_message = "script id is empty",
    };
  }

  const auto relative_script_path = NormalizeScriptPath(script_id);
  if (relative_script_path.is_absolute()) {
    return ScriptLoadResult {
      .ok = false,
      .source_text = {},
      .chunk_name = {},
      .error_message = "script id must be a relative path under scripts root",
    };
  }
  if (HasParentTraversal(relative_script_path)) {
    return ScriptLoadResult {
      .ok = false,
      .source_text = {},
      .chunk_name = {},
      .error_message = "script id must not contain parent path traversal",
    };
  }

  const auto script_path
    = (path_finder_.ScriptsRootPath() / relative_script_path)
        .lexically_normal();

  std::ifstream input(script_path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    return ScriptLoadResult {
      .ok = false,
      .source_text = {},
      .chunk_name = {},
      .error_message = std::string("unable to open script file: ")
        .append(script_path.string()),
    };
  }

  const std::string source_text(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return ScriptLoadResult {
      .ok = false,
      .source_text = {},
      .chunk_name = {},
      .error_message = std::string("failed while reading script file: ")
        .append(script_path.string()),
    };
  }

  return ScriptLoadResult {
    .ok = true,
    .source_text = source_text,
    .chunk_name = script_path.generic_string(),
    .error_message = {},
  };
}

} // namespace oxygen::scripting
