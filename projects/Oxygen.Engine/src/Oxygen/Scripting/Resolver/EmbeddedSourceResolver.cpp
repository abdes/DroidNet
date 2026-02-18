//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <string>

#include <Oxygen/Scripting/Resolver/EmbeddedSourceResolver.h>

namespace oxygen::scripting {

namespace {

  auto LoadEmbeddedBlob(const IScriptSourceResolver::ResolveRequest& request,
    const uint32_t index) -> IScriptSourceResolver::ResolveResult
  {
    if (!request.load_script_resource) {
      return IScriptSourceResolver::ResolveResult {
        .ok = false,
        .blob = {},
        .error_message = "script resource loader callback is not set",
      };
    }

    auto resource = request.load_script_resource(index);
    if (!resource) {
      return IScriptSourceResolver::ResolveResult {
        .ok = false,
        .blob = {},
        .error_message = "embedded script resource not found for index "
          + std::to_string(index),
      };
    }
    if (resource->GetData().empty()) {
      return IScriptSourceResolver::ResolveResult {
        .ok = false,
        .blob = {},
        .error_message = "embedded script resource is empty for index "
          + std::to_string(index),
      };
    }

    auto origin = ScriptSourceBlob::Origin::kEmbeddedResource;
    if (request.map_resource_origin) {
      if (const auto mapped = request.map_resource_origin(index)) {
        origin = *mapped;
      }
    }

    ScriptSourceBlob blob {
      .bytes = std::vector<uint8_t>(
        resource->GetData().begin(), resource->GetData().end()),
      .language = resource->GetLanguage(),
      .encoding = resource->GetEncoding(),
      .compression = resource->GetCompression(),
      .content_hash = resource->GetContentHash(),
      .origin = origin,
      .canonical_name = ScriptSourceBlob::CanonicalName { "embedded-resource:"
        + std::to_string(index) },
    };
    return IScriptSourceResolver::ResolveResult {
      .ok = true,
      .blob = std::move(blob),
      .error_message = {},
    };
  }

} // namespace

auto EmbeddedSourceResolver::Resolve(
  const IScriptSourceResolver::ResolveRequest& request)
  -> IScriptSourceResolver::ResolveResult
{
  const auto& asset = request.asset.get();
  const std::array<uint32_t, 2> preferred_indices {
    asset.GetBytecodeResourceIndex(),
    asset.GetSourceResourceIndex(),
  };

  for (size_t i = 0; i < preferred_indices.size(); ++i) {
    const auto index = preferred_indices.at(i);
    if (index == data::pak::kNoResourceIndex) {
      continue;
    }
    if (i == 1 && index == preferred_indices[0]) {
      continue;
    }

    return LoadEmbeddedBlob(request, index);
  }

  return IScriptSourceResolver::ResolveResult {
    .ok = false,
    .blob = {},
    .error_message = "no embedded script resource is assigned",
  };
}

} // namespace oxygen::scripting
