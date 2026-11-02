//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import::detail {

namespace {

  struct SceneBindingContextOutcome final {
    std::optional<ResolvedSceneState> state;
    bool failed = false;
  };

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message)
    -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
    });
  }

  auto BuildResolvedSceneStateFromDescriptor(ImportSession& session,
    const ImportRequest& request,
    const SidecarSceneResolverDiagnostics& diagnostics,
    const data::AssetKey scene_key, std::string scene_virtual_path,
    std::string scene_descriptor_relpath,
    std::vector<std::byte> descriptor_bytes, data::AssetReferences references)
    -> std::optional<ResolvedSceneState>
  {
    auto scene_asset = std::optional<data::SceneAsset> {};
    try {
      scene_asset.emplace(
        scene_key, std::span<const std::byte>(descriptor_bytes));
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.target_scene_invalid_code),
        "Target scene descriptor failed validation: " + std::string(ex.what()));
      return std::nullopt;
    }

    const auto environment_offset = descriptor_bytes.size()
      - scene_asset->GetEnvironmentBlockHeader().byte_size;
    auto state = ResolvedSceneState {
      .scene_key = scene_key,
      .scene_virtual_path = std::move(scene_virtual_path),
      .scene_descriptor_relpath = std::move(scene_descriptor_relpath),
      .source_scene_descriptor = std::move(descriptor_bytes),
      .references = std::move(references),
      .environment_offset = environment_offset,
      .node_count = static_cast<uint32_t>(scene_asset->GetNodes().size()),
      .existing_scripting_components = {},
    };

    const auto existing_components_span
      = scene_asset
          ->GetComponents<data::pak::scripting::ScriptingComponentRecord>();
    state.existing_scripting_components.assign(
      existing_components_span.begin(), existing_components_span.end());

    return state;
  }

  auto ResolveSceneBindingContextFromInflightByVirtualPath(
    ImportSession& session, const ImportRequest& request,
    std::string_view target_scene_virtual_path,
    const SidecarSceneResolverDiagnostics& diagnostics)
    -> SceneBindingContextOutcome
  {
    auto matches = std::vector<const ImportRequest::InflightSceneContext*> {};
    matches.reserve(request.inflight_scene_contexts.size());
    for (const auto& candidate : request.inflight_scene_contexts) {
      if (candidate.virtual_path == target_scene_virtual_path) {
        matches.push_back(&candidate);
      }
    }

    if (matches.size() > 1U) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.inflight_target_scene_ambiguous_code),
        "Multiple inflight scene contexts match target_scene_virtual_path");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }
    if (matches.empty()) {
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = false,
      };
    }

    const auto* match = matches.front();
    if (match->descriptor_relpath.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.target_scene_invalid_code),
        "Inflight scene context has empty descriptor_relpath");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }

    const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
      diagnostics, match->scene_key, match->virtual_path,
      match->descriptor_relpath, match->descriptor_bytes, match->references);
    return SceneBindingContextOutcome {
      .state = state,
      .failed = !state.has_value(),
    };
  }

  auto ResolveSceneBindingContextFromInflightBySceneKey(ImportSession& session,
    const ImportRequest& request, const data::AssetKey& scene_key,
    const SidecarSceneResolverDiagnostics& diagnostics)
    -> SceneBindingContextOutcome
  {
    auto matches = std::vector<const ImportRequest::InflightSceneContext*> {};
    matches.reserve(request.inflight_scene_contexts.size());
    for (const auto& candidate : request.inflight_scene_contexts) {
      if (candidate.scene_key == scene_key) {
        matches.push_back(&candidate);
      }
    }

    if (matches.size() > 1U) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.inflight_target_scene_ambiguous_code),
        "Multiple inflight scene contexts match the resolved target scene key");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }
    if (matches.empty()) {
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = false,
      };
    }

    const auto* match = matches.front();
    if (match->descriptor_relpath.empty()) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.target_scene_invalid_code),
        "Inflight scene context has empty descriptor_relpath");
      return SceneBindingContextOutcome {
        .state = std::nullopt,
        .failed = true,
      };
    }

    const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
      diagnostics, match->scene_key, match->virtual_path,
      match->descriptor_relpath, match->descriptor_bytes, match->references);
    return SceneBindingContextOutcome {
      .state = state,
      .failed = !state.has_value(),
    };
  }

  auto ResolveSceneBindingContextFromCookedInspection(ImportSession& session,
    const ImportRequest& request, const data::AssetKey& scene_key,
    std::span<const CookedInspectionContext> cooked_contexts,
    const SidecarSceneResolverDiagnostics& diagnostics)
    -> SceneBindingContextOutcome
  {
    using data::AssetType;

    for (const auto& cooked_context : std::views::reverse(cooked_contexts)) {
      const auto* scene_asset = cooked_context.content.FindAsset(scene_key);
      if (scene_asset == nullptr) {
        continue;
      }
      if (scene_asset->type != AssetType::kScene) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(diagnostics.target_scene_not_scene_code),
          "Target scene virtual path does not reference a scene asset");
        return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      auto descriptor_bytes = std::vector<std::byte> {};
      try {
        descriptor_bytes = cooked_context.content.Descriptor(scene_key);
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(diagnostics.target_scene_read_failed_code),
          "Failed reading target scene descriptor: " + std::string(ex.what()));
        return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      // A cooked root holds a descriptor at its identity, below the mount.
      auto descriptor_relpath = std::string {};
      (void)internal::TryVirtualPathToRelPath(
        scene_asset->virtual_path, descriptor_relpath);
      const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
        diagnostics, scene_asset->key, scene_asset->virtual_path,
        std::move(descriptor_relpath), std::move(descriptor_bytes),
        scene_asset->references);
      return SceneBindingContextOutcome {
        .state = state,
        .failed = !state.has_value(),
      };
    }

    return SceneBindingContextOutcome {
      .state = std::nullopt,
      .failed = false,
    };
  }

} // namespace

auto LoadCookedInspectionContext(const std::filesystem::path& cooked_root,
  ImportSession& session, const ImportRequest& request,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> std::optional<CookedInspectionContext>
{
  try {
    return CookedInspectionContext {
      .cooked_root = cooked_root,
      .content
      = cooked::CookedContent::Open(cooked::RealCookerStorage(), cooked_root),
    };
  } catch (const std::exception& ex) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.index_load_failed_code),
      "Failed loading cooked index: " + std::string(ex.what()));
    return std::nullopt;
  }
}

auto MountCookedInspectionContext(content::VirtualPathResolver& resolver,
  const CookedInspectionContext& context) -> void
{
  if (context.content.Source().kind == data::CookedSourceKind::kPak) {
    resolver.AddPakFile(context.cooked_root);
  } else {
    resolver.AddLooseCookedRoot(context.cooked_root);
  }
}

auto ReadCookedDescriptor(const CookedInspectionContext& context,
  const data::AssetKey& key) -> std::optional<std::vector<std::byte>>
{
  try {
    return context.content.Descriptor(key);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

auto ResolveSceneInspectionContextByKey(
  std::span<const CookedInspectionContext> cooked_contexts,
  const data::AssetKey& scene_key) -> const CookedInspectionContext*
{
  for (size_t i = cooked_contexts.size(); i > 0; --i) {
    const auto& context = cooked_contexts[i - 1U];
    const auto* asset = context.content.FindAsset(scene_key);
    if (asset != nullptr && asset->type == data::AssetType::kScene) {
      return &context;
    }
  }
  return nullptr;
}

auto ResolveTargetSceneState(ImportSession& session,
  const ImportRequest& request, content::VirtualPathResolver& resolver,
  std::span<const CookedInspectionContext> cooked_contexts,
  std::string_view target_scene_virtual_path,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> std::optional<ResolvedSceneState>
{
  std::optional<data::AssetKey> resolver_key {};
  try {
    resolver_key = resolver.ResolveAssetKey(target_scene_virtual_path);
  } catch (const std::invalid_argument& ex) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.target_scene_virtual_path_invalid_code),
      "Target scene virtual path is invalid: " + std::string(ex.what()));
    return std::nullopt;
  }

  const auto inflight_path_outcome
    = ResolveSceneBindingContextFromInflightByVirtualPath(
      session, request, target_scene_virtual_path, diagnostics);
  if (inflight_path_outcome.state.has_value()) {
    return inflight_path_outcome.state;
  }
  if (inflight_path_outcome.failed) {
    return std::nullopt;
  }

  if (!resolver_key.has_value()) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.target_scene_missing_code),
      "Target scene virtual path was not found: "
        + std::string(target_scene_virtual_path));
    return std::nullopt;
  }

  const auto inflight_key_outcome
    = ResolveSceneBindingContextFromInflightBySceneKey(
      session, request, *resolver_key, diagnostics);
  if (inflight_key_outcome.state.has_value()) {
    return inflight_key_outcome.state;
  }
  if (inflight_key_outcome.failed) {
    return std::nullopt;
  }

  const auto cooked_outcome = ResolveSceneBindingContextFromCookedInspection(
    session, request, *resolver_key, cooked_contexts, diagnostics);
  if (cooked_outcome.state.has_value()) {
    return cooked_outcome.state;
  }
  if (cooked_outcome.failed) {
    return std::nullopt;
  }

  AddDiagnostic(session, request, ImportSeverity::kError,
    std::string(diagnostics.target_scene_missing_code),
    "Resolved target scene key is not present in cooked scene context");
  return std::nullopt;
}

} // namespace oxygen::content::import::detail
