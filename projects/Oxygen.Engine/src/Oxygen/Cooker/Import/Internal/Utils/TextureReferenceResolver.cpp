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
#include <Oxygen/Content/TextureDescriptorPath.h>
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
  observer_ptr<IAsyncFileReader> reader,
  const std::span<const MountedCookedSource> mounts,
  TextureReferenceRequest reference)
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

  // The job's own output binds the texture-table entry it cooks there.
  const auto output_root = BuildMountedCookedRoots(*request).front();
  auto relpath = std::string {};
  const auto in_output
    = TryVirtualPathToRelPath(*request, virtual_path, relpath);
  if (in_output) {
    auto descriptor_path = std::optional<std::filesystem::path> {};
    try {
      descriptor_path = content::FindTextureResourceDescriptorPath(
        output_root / std::filesystem::path(relpath));
    } catch (const std::exception& error) {
      report_error("texture_descriptor_ambiguous", error.what());
      co_return std::nullopt;
    }
    if (descriptor_path) {
      const auto read_result = co_await reader->ReadFile(*descriptor_path);
      if (!read_result.has_value()) {
        report_error("texture_descriptor_read_failed",
          "Failed reading texture descriptor: "
            + read_result.error().ToString());
        co_return std::nullopt;
      }
      const auto decoded
        = data::DecodeTextureResourceDescriptor(read_result.value());
      if (!decoded) {
        report_error("texture_descriptor_invalid", decoded.error());
        co_return std::nullopt;
      }
      if (decoded->index == data::pak::core::kNoResourceIndex) {
        report_error("texture_descriptor_invalid",
          "Texture descriptor names no texture-table entry");
        co_return std::nullopt;
      }
      co_return ResolvedTextureReference {
        .target = decoded->index,
        .descriptor = decoded->descriptor,
      };
    }
  }

  // A cooked context root holds it: the descriptor names it by key, and the
  // runtime resolves it in whichever mount holds that path.
  auto output_error = std::error_code {};
  for (const auto& mount : std::views::reverse(mounts)) {
    if (!mount.inspection
      || std::filesystem::equivalent(mount.root, output_root, output_error)) {
      continue;
    }
    auto descriptor = std::optional<data::pak::core::TextureResourceDesc> {};
    try {
      descriptor = mount.inspection->FindTexture(virtual_path);
    } catch (const std::exception& error) {
      report_error("texture_descriptor_invalid", error.what());
      co_return std::nullopt;
    }
    if (descriptor) {
      co_return ResolvedTextureReference {
        .target = data::TextureKeyFromVirtualPath(virtual_path),
        .descriptor = *descriptor,
      };
    }
  }

  if (!in_output) {
    report_error("texture_virtual_path_unmounted",
      "Texture reference virtual_path is in no mounted cooked root");
    co_return std::nullopt;
  }

  if (reference.allow_missing) {
    session->AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code
      = std::string(reference.diagnostic_prefix) + "texture_descriptor_missing",
      .message = "Texture descriptor virtual_path was not found: "
        + std::string(virtual_path) + "; cooking continues without it",
      .source_path = request->source_path.string(),
      .object_path = reference.object_path,
    });
    co_return ResolvedTextureReference { .missing = true };
  }

  report_error("texture_descriptor_missing",
    "Texture descriptor virtual_path was not found: "
      + std::string(virtual_path));
  co_return std::nullopt;
}

} // namespace oxygen::content::import::internal
