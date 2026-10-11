//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Config/PathFinder.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/Scene.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Analysis/ScriptAnalyzer.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Compilers/ScriptCompileKey.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>
#include <Oxygen/Scripting/Module/ScriptSourceReload.h>

namespace oxygen::scripting {

namespace {
  constexpr auto kCompileMode
    = core::meta::scripting::ScriptCompileMode::kDebug;

  //! Calls `visit` with every node of `scene`.
  auto ForEachNode(scene::Scene& scene,
    const std::function<void(scene::SceneNode&)>& visit) -> void
  {
    auto pending = scene.GetRootNodes();
    while (!pending.empty()) {
      auto node = std::move(pending.back());
      pending.pop_back();
      if (!node.IsAlive()) {
        continue;
      }
      visit(node);
      for (auto child = node.GetFirstChild(); child.has_value();
        child = child->GetNextSibling()) {
        pending.push_back(*child);
      }
    }
  }

  //! The distinct script assets the slots of `scene` use, by asset key.
  auto CollectScripts(scene::Scene& scene) -> std::unordered_map<data::AssetKey,
    std::shared_ptr<const data::ScriptAsset>>
  {
    auto scripts = std::unordered_map<data::AssetKey,
      std::shared_ptr<const data::ScriptAsset>> {};
    ForEachNode(scene, [&scripts](scene::SceneNode& node) -> void {
      if (!node.HasScripting()) {
        return;
      }
      for (const auto& slot : node.GetScripting().Slots()) {
        if (const auto& asset = slot.Asset()) {
          scripts.try_emplace(asset->GetAssetKey(), asset);
        }
      }
    });
    return scripts;
  }

  //! Installs a reloaded script in every slot of `scene` that uses it.
  auto InstallReloaded(scene::Scene& scene,
    const std::shared_ptr<const data::ScriptAsset>& asset,
    const std::shared_ptr<const ScriptExecutable>& executable) -> void
  {
    const auto key = asset->GetAssetKey();
    ForEachNode(scene, [&](scene::SceneNode& node) -> void {
      if (!node.HasScripting()) {
        return;
      }
      auto scripting = node.GetScripting();
      for (const auto& slot : scripting.Slots()) {
        if (slot.Asset() && slot.Asset()->GetAssetKey() == key) {
          (void)scripting.ReplaceSlotAsset(slot, asset);
          (void)scripting.MarkSlotReady(slot, executable);
        }
      }
    });
  }

  auto SameFile(
    const std::filesystem::path& lhs, const std::filesystem::path& rhs) -> bool
  {
    std::error_code error;
    const auto equivalent = std::filesystem::equivalent(lhs, rhs, error);
    return !error && equivalent;
  }
} // namespace

ScriptSourceReload::ScriptSourceReload(PathFinder path_finder,
  const observer_ptr<IScriptCompilationService> compiler, DiagnosticSink sink)
  : resolver_(std::move(path_finder))
  , compiler_(compiler)
  , sink_(std::move(sink))
{
}

auto ScriptSourceReload::OnSourceChanged(
  const std::shared_ptr<scene::Scene>& scene,
  const std::filesystem::path& changed_file) -> std::size_t
{
  if (!scene) {
    return 0;
  }
  auto submitted = std::size_t { 0 };
  for (const auto& [key, asset] : CollectScripts(*scene)) {
    const auto source = resolver_.ResolvePath(*asset);
    if (source.has_value() && SameFile(*source, changed_file)
      && Reload(scene, asset)) {
      ++submitted;
    }
  }
  if (submitted == 0) {
    LOG_F(1, "script source {} is used by no script of the scene",
      changed_file.generic_string());
  }
  return submitted;
}

auto ScriptSourceReload::ReloadAll(const std::shared_ptr<scene::Scene>& scene)
  -> std::size_t
{
  if (!scene) {
    return 0;
  }
  auto submitted = std::size_t { 0 };
  for (const auto& [key, asset] : CollectScripts(*scene)) {
    if (asset->TryGetExternalSourcePath().has_value() && Reload(scene, asset)) {
      ++submitted;
    }
  }
  return submitted;
}

auto ScriptSourceReload::Reload(const std::shared_ptr<scene::Scene>& scene,
  const std::shared_ptr<const data::ScriptAsset>& asset) -> bool
{
  if (compiler_ == nullptr) {
    return false;
  }
  auto resolved = resolver_.Resolve(IScriptSourceResolver::ResolveRequest {
    .asset = *asset,
    .load_script_resource = {},
    .map_resource_origin = {},
  });
  if (!resolved.ok || !resolved.blob.has_value()
    || !std::holds_alternative<ScriptSourceBlob>(*resolved.blob)) {
    LOG_F(WARNING, "cannot read the source of script {}: {}",
      data::to_string(asset->GetAssetKey()), resolved.error_message);
    return false;
  }
  auto source = std::get<ScriptSourceBlob>(std::move(*resolved.blob));

  // Declarations come from the new source; on errors nothing changes.
  const auto source_name = source.GetCanonicalName().get();
  const auto analysis = analyzer_.Analyze(ScriptAnalysisRequest {
    .source_name = source_name,
    .source = std::as_bytes(source.BytesView()),
    .level = AnalysisLevel::kDeclarations,
  });
  if (analysis.HasErrors()) {
    for (const auto& diagnostic : analysis.diagnostics) {
      sink_(diagnostic);
    }
    return false;
  }
  auto declarations = std::vector<data::pak::scripting::ScriptParamRecord> {};
  declarations.reserve(analysis.declarations.size());
  for (const auto& declaration : analysis.declarations) {
    if (const auto record = data::MakeScriptParamRecord(
          declaration.name, declaration.default_value)) {
      declarations.push_back(*record);
    }
  }
  const std::shared_ptr<const data::ScriptAsset> reloaded
    = asset->WithDeclarations(declarations);

  const auto compile_key
    = ComputeScriptCompileKey(asset->GetAssetKey(), source, kCompileMode);
  (void)compiler_->AcquireForSlot(
    IScriptCompilationService::Request {
      .compile_key = compile_key,
      .source = std::move(source),
      .compile_mode = kCompileMode,
    },
    IScriptCompilationService::SlotAcquireCallbacks {
      .on_ready
      = [alive = std::weak_ptr(alive_), weak_scene = std::weak_ptr(scene),
          reloaded](
          std::shared_ptr<const ScriptBytecodeBlob> bytecode) -> void {
        const auto live_scene = weak_scene.lock();
        if (alive.expired() || !live_scene) {
          return;
        }
        LOG_F(
          INFO, "reloaded script {}", data::to_string(reloaded->GetAssetKey()));
        InstallReloaded(*live_scene, reloaded,
          std::make_shared<const CompiledScriptExecutable>(
            std::move(bytecode)));
      },
      .on_failed = [alive = std::weak_ptr(alive_), sink = sink_](
                     const std::vector<ScriptDiagnostic>& diagnostics) -> void {
        if (alive.expired()) {
          return;
        }
        for (const auto& diagnostic : diagnostics) {
          sink(diagnostic);
        }
      },
    });
  return true;
}

} // namespace oxygen::scripting
