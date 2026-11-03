//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <system_error>
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/TextureReferenceResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::internal {
auto ResolveTextureReference(observer_ptr<ImportSession> session,
  observer_ptr<const ImportRequest> request,
  const std::span<const MountedCookedSource> mounts,
  TextureReferenceRequest reference)
  -> co::Co<std::optional<ResolvedTextureReference>>
{
  const auto report_error = [&](const char* code, std::string message) -> void {
    session->AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = std::string(reference.diagnostic_prefix) + code,
      .message = std::move(message),
      .source_path = request->source_path.string(),
      .json_pointer = reference.json_pointer,
    });
  };
  const auto resolved = ResolveJobReference(*request, reference.text);
  if (!resolved.has_value()) {
    session->AddDiagnostic(RefuseReference(
      *request, reference.text, resolved.error(), reference.json_pointer));
    co_return std::nullopt;
  }
  const auto& virtual_path = resolved->resolved;

  // The job's own output binds the texture-table entry it cooks there; a
  // cooked context root holds it by key, and the runtime resolves it in
  // whichever mount holds that path. Only an index lists a texture.
  const auto output_root = BuildMountedCookedRoots(*request).front();
  auto relpath = std::string {};
  const auto in_output
    = TryVirtualPathToRelPath(*request, virtual_path, relpath);
  auto output_error = std::error_code {};
  for (const auto& mount : std::views::reverse(mounts)) {
    if (!mount.content) {
      continue;
    }
    const auto* const named = mount.content->FindNamedTexture(virtual_path);
    if (named == nullptr) {
      continue;
    }
    const auto is_output
      = std::filesystem::equivalent(mount.root, output_root, output_error);
    if (is_output && named->row == data::pak::core::kNoResourceIndex) {
      report_error("texture_descriptor_invalid",
        "Texture descriptor names no texture-table entry");
      co_return std::nullopt;
    }
    auto descriptor = data::pak::core::TextureResourceDesc {};
    try {
      descriptor = mount.content->ResourceTable(data::ResourceKind::kTexture)
                     .Row<data::pak::core::TextureResourceDesc>(named->row);
    } catch (const std::exception& error) {
      report_error("texture_descriptor_invalid", error.what());
      co_return std::nullopt;
    }
    co_return ResolvedTextureReference {
      .target = is_output
        ? data::TextureTarget { named->row }
        : data::TextureTarget { data::TextureKeyFromVirtualPath(virtual_path) },
      .descriptor = descriptor,
    };
  }

  if (!in_output) {
    session->AddDiagnostic(
      RefuseUnmountedReference(*request, *resolved, reference.json_pointer));
    co_return std::nullopt;
  }

  // A reference in the job's own mount names what its index would list.
  const auto looked_for = "'" + resolved->written + "' resolves to "
    + virtual_path + "; looked for "
    + (output_root / std::filesystem::path(relpath)).generic_string();

  if (reference.allow_missing) {
    session->AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code
      = std::string(reference.diagnostic_prefix) + "texture_descriptor_missing",
      .message = "Texture descriptor not found: " + looked_for
        + "; cooking continues without it",
      .source_path = request->source_path.string(),
      .json_pointer = reference.json_pointer,
    });
    co_return ResolvedTextureReference { .missing = true };
  }

  report_error("texture_descriptor_missing",
    "Texture descriptor not found: " + looked_for);
  co_return std::nullopt;
}

} // namespace oxygen::content::import::internal
