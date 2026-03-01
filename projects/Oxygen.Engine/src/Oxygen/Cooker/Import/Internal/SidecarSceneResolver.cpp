//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <exception>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Content/VirtualPathResolver.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/SidecarSceneResolver.h>
#include <Oxygen/Data/AssetType.h>

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

  auto FindAssetEntryByKey(const lc::Inspection& inspection,
    const data::AssetKey& key) -> const lc::AssetEntry*
  {
    for (const auto& entry : inspection.Assets()) {
      if (entry.key == key) {
        return &entry;
      }
    }
    return nullptr;
  }

  auto BuildResolvedSceneStateFromDescriptor(ImportSession& session,
    const ImportRequest& request,
    const SidecarSceneResolverDiagnostics& diagnostics,
    const data::AssetKey scene_key, std::string scene_virtual_path,
    std::string scene_descriptor_relpath,
    std::vector<std::byte> descriptor_bytes)
    -> std::optional<ResolvedSceneState>
  {
    auto scene_asset = std::optional<data::SceneAsset> {};
    try {
      scene_asset.emplace(scene_key, descriptor_bytes);
    } catch (const std::exception& ex) {
      AddDiagnostic(session, request, ImportSeverity::kError,
        std::string(diagnostics.target_scene_invalid_code),
        "Target scene descriptor failed validation: " + std::string(ex.what()));
      return std::nullopt;
    }

    auto state = ResolvedSceneState {
      .scene_key = scene_key,
      .scene_virtual_path = std::move(scene_virtual_path),
      .scene_descriptor_relpath = std::move(scene_descriptor_relpath),
      .source_scene_descriptor = std::move(descriptor_bytes),
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
      match->descriptor_relpath, match->descriptor_bytes);
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
      match->descriptor_relpath, match->descriptor_bytes);
    return SceneBindingContextOutcome {
      .state = state,
      .failed = !state.has_value(),
    };
  }

  auto ResolveSceneBindingContextFromCookedInspection(ImportSession& session,
    const ImportRequest& request, const data::AssetKey& scene_key,
    std::span<const CookedInspectionContext> cooked_contexts,
    IAsyncFileReader& reader,
    const SidecarSceneResolverDiagnostics& diagnostics)
    -> co::Co<SceneBindingContextOutcome>
  {
    using data::AssetType;

    for (auto it = cooked_contexts.rbegin(); it != cooked_contexts.rend();
      ++it) {
      const auto* scene_asset_entry
        = FindAssetEntryByKey(it->inspection, scene_key);
      if (scene_asset_entry == nullptr) {
        continue;
      }
      if (static_cast<AssetType>(scene_asset_entry->asset_type)
        != AssetType::kScene) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(diagnostics.target_scene_not_scene_code),
          "Target scene virtual path does not reference a scene asset");
        co_return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      const auto descriptor_path
        = it->cooked_root / scene_asset_entry->descriptor_relpath;
      const auto scene_read = co_await reader.ReadFile(descriptor_path);
      if (!scene_read.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          std::string(diagnostics.target_scene_read_failed_code),
          "Failed reading target scene descriptor: "
            + scene_read.error().ToString());
        co_return SceneBindingContextOutcome {
          .state = std::nullopt,
          .failed = true,
        };
      }

      const auto state = BuildResolvedSceneStateFromDescriptor(session, request,
        diagnostics, scene_asset_entry->key, scene_asset_entry->virtual_path,
        scene_asset_entry->descriptor_relpath, scene_read.value());
      co_return SceneBindingContextOutcome {
        .state = state,
        .failed = !state.has_value(),
      };
    }

    co_return SceneBindingContextOutcome {
      .state = std::nullopt,
      .failed = false,
    };
  }

} // namespace

auto LoadCookedInspectionContext(const std::filesystem::path& cooked_root,
  ImportSession& session, const ImportRequest& request,
  const SidecarSceneResolverDiagnostics& diagnostics,
  CookedInspectionContext& out_context) -> bool
{
  try {
    out_context.cooked_root = cooked_root;
    out_context.inspection.LoadFromRoot(cooked_root);
    return true;
  } catch (const std::exception& ex) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.index_load_failed_code),
      "Failed loading cooked index: " + std::string(ex.what()));
    return false;
  }
}

auto ResolveSceneInspectionContextByKey(
  std::span<const CookedInspectionContext> cooked_contexts,
  const data::AssetKey& scene_key) -> const CookedInspectionContext*
{
  for (size_t i = cooked_contexts.size(); i > 0; --i) {
    const auto& context = cooked_contexts[i - 1U];
    for (const auto& asset : context.inspection.Assets()) {
      if (asset.key == scene_key
        && static_cast<data::AssetType>(asset.asset_type)
          == data::AssetType::kScene) {
        return &context;
      }
    }
  }
  return nullptr;
}

auto ResolveTargetSceneState(ImportSession& session,
  const ImportRequest& request, content::VirtualPathResolver& resolver,
  std::span<const CookedInspectionContext> cooked_contexts,
  IAsyncFileReader& reader, std::string_view target_scene_virtual_path,
  const SidecarSceneResolverDiagnostics& diagnostics)
  -> co::Co<std::optional<ResolvedSceneState>>
{
  std::optional<data::AssetKey> resolver_key {};
  try {
    resolver_key = resolver.ResolveAssetKey(target_scene_virtual_path);
  } catch (const std::invalid_argument& ex) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.target_scene_virtual_path_invalid_code),
      "Target scene virtual path is invalid: " + std::string(ex.what()));
    co_return std::nullopt;
  }

  const auto inflight_path_outcome
    = ResolveSceneBindingContextFromInflightByVirtualPath(
      session, request, target_scene_virtual_path, diagnostics);
  if (inflight_path_outcome.state.has_value()) {
    co_return inflight_path_outcome.state;
  }
  if (inflight_path_outcome.failed) {
    co_return std::nullopt;
  }

  if (!resolver_key.has_value()) {
    AddDiagnostic(session, request, ImportSeverity::kError,
      std::string(diagnostics.target_scene_missing_code),
      "Target scene virtual path was not found: "
        + std::string(target_scene_virtual_path));
    co_return std::nullopt;
  }

  const auto inflight_key_outcome
    = ResolveSceneBindingContextFromInflightBySceneKey(
      session, request, *resolver_key, diagnostics);
  if (inflight_key_outcome.state.has_value()) {
    co_return inflight_key_outcome.state;
  }
  if (inflight_key_outcome.failed) {
    co_return std::nullopt;
  }

  const auto cooked_outcome
    = co_await ResolveSceneBindingContextFromCookedInspection(
      session, request, *resolver_key, cooked_contexts, reader, diagnostics);
  if (cooked_outcome.state.has_value()) {
    co_return cooked_outcome.state;
  }
  if (cooked_outcome.failed) {
    co_return std::nullopt;
  }

  AddDiagnostic(session, request, ImportSeverity::kError,
    std::string(diagnostics.target_scene_missing_code),
    "Resolved target scene key is not present in cooked scene context");
  co_return std::nullopt;
}

} // namespace oxygen::content::import::detail
