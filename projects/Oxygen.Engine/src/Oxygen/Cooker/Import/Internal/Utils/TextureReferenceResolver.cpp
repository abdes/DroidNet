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
#include <utility>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/TextureResourceLocator.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/Utils/TextureReferenceResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Data/TextureResourceDescriptor.h>
#include <Oxygen/OxCo/Co.h>

namespace oxygen::content::import::internal {
auto ResolveTextureReference(observer_ptr<ImportSession> session,
  observer_ptr<const ImportRequest> request,
  observer_ptr<IAsyncFileReader> reader, TextureReferenceRequest reference)
  -> co::Co<std::optional<ResolvedTextureReference>>
{
  const auto virtual_path = reference.virtual_path;
  const auto report_error = [&](const char* code, std::string message) -> void {
    session->AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = std::string(reference.diagnostic_prefix) + code,
      .message = std::move(message),
      .source_path = request->source_path.string(),
      .object_path = reference.object_path,
    });
  };
  if (!IsCanonicalVirtualPath(virtual_path)) {
    report_error("texture_virtual_path_invalid",
      "Texture reference virtual_path must be canonical");
    co_return std::nullopt;
  }

  auto relpath = std::string {};
  if (!TryVirtualPathToRelPath(*request, virtual_path, relpath)) {
    report_error("texture_virtual_path_unmounted",
      "Texture reference virtual_path is outside mounted cooked roots");
    co_return std::nullopt;
  }

  auto mounted_roots = BuildMountedCookedRoots(*request);

  for (const auto& root : std::views::reverse(mounted_roots)) {
    auto descriptor_path = std::optional<std::filesystem::path> {};
    try {
      descriptor_path = content::FindTextureResourceDescriptorPath(
        root / std::filesystem::path(relpath));
    } catch (const std::exception& error) {
      report_error("texture_descriptor_ambiguous", error.what());
      co_return std::nullopt;
    }
    if (!descriptor_path) {
      continue;
    }

    const auto read_result = co_await reader->ReadFile(*descriptor_path);
    if (!read_result.has_value()) {
      report_error("texture_descriptor_read_failed",
        "Failed reading texture descriptor: " + read_result.error().ToString());
      co_return std::nullopt;
    }

    const auto decoded
      = data::DecodeTextureResourceDescriptor(read_result.value());
    if (!decoded) {
      report_error("texture_descriptor_invalid", decoded.error());
      co_return std::nullopt;
    }
    auto sidecar = ResolvedTextureReference {
      .index = decoded->index,
      .descriptor = decoded->descriptor,
      .cooked_root = {},
    };

    sidecar.cooked_root = root;
    co_return sidecar;
  }

  report_error("texture_descriptor_missing",
    "Texture descriptor virtual_path was not found: "
      + std::string(virtual_path));
  co_return std::nullopt;
}

} // namespace oxygen::content::import::internal
