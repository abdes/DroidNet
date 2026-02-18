//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <fstream>
#include <iterator>
#include <utility>

#include <Oxygen/Scripting/Resolver/ExternalFileResolver.h>

namespace oxygen::scripting {

namespace {

  auto NormalizeExternalPath(std::string_view path_text)
    -> std::filesystem::path
  {
    std::filesystem::path path { path_text };
    if (path.extension().empty()) {
      path.replace_extension(".luau");
    }
    return path.lexically_normal();
  }

  auto HasParentTraversal(const std::filesystem::path& path) -> bool
  {
    return std::ranges::any_of(
      path, [](const auto& segment) -> auto { return segment == ".."; });
  }

} // namespace

ExternalFileResolver::ExternalFileResolver(PathFinder path_finder)
  : path_finder_(std::move(path_finder))
{
}

auto ExternalFileResolver::Resolve(
  const IScriptSourceResolver::ResolveRequest& request) const
  -> IScriptSourceResolver::ResolveResult
{
  const auto external_path = request.asset.get().TryGetExternalSourcePath();
  if (!external_path.has_value()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path is not set",
    };
  }

  auto relative_path = NormalizeExternalPath(*external_path);
  if (relative_path.is_absolute()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path must be relative",
    };
  }
  if (HasParentTraversal(relative_path)) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message = "external source path must not contain parent traversal",
    };
  }

  const auto full_path
    = (path_finder_.ScriptsRootPath() / relative_path).lexically_normal();
  std::ifstream input(full_path, std::ios::in | std::ios::binary);
  if (!input.is_open()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "unable to open external source file: " + full_path.string(),
    };
  }

  std::vector<uint8_t> bytes(
    (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  if (input.bad()) {
    return IScriptSourceResolver::ResolveResult {
      .ok = false,
      .blob = {},
      .error_message
      = "failed while reading external source file: " + full_path.string(),
    };
  }

  ScriptSourceBlob blob {
    .bytes = std::move(bytes),
    .language = data::pak::ScriptLanguage::kLuau,
    .encoding = data::pak::ScriptEncoding::kSource,
    .compression = data::pak::ScriptCompression::kNone,
    .content_hash = 0,
    .origin = ScriptSourceBlob::Origin::kExternalFile,
    .canonical_name
    = ScriptSourceBlob::CanonicalName { full_path.generic_string() },
  };
  return IScriptSourceResolver::ResolveResult {
    .ok = true,
    .blob = std::move(blob),
    .error_message = {},
  };
}

} // namespace oxygen::scripting
