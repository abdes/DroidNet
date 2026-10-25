//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <fmt/format.h>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Span.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/PakFormat_scripting.h>
#include <Oxygen/Data/PakFormat_world.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Data/SceneComposition.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scene/SceneNode.h>
#include <Oxygen/Scripting/Analysis/ScriptDiagnostic.h>
#include <Oxygen/Scripting/Attachment/SceneScriptAttachment.h>
#include <Oxygen/Scripting/Attachment/ScriptOverrideCheck.h>
#include <Oxygen/Scripting/Compilers/ScriptCompileKey.h>
#include <Oxygen/Scripting/Execution/CompiledScriptExecutable.h>
#include <Oxygen/Scripting/IScriptSourceResolver.h>

namespace oxygen::scripting {

namespace {
  using Slot = scene::SceneNode::Scripting::Slot;
  using State = SceneScriptSlotOutcome::State;

  constexpr auto kCompileMode
    = core::meta::scripting::ScriptCompileMode::kDebug;

  //! Attaches the slots of one node and records their outcomes.
  class NodeAttachment {
  public:
    NodeAttachment(const observer_ptr<content::IAssetLoader> loader,
      const observer_ptr<const IScriptSourceResolver> resolver,
      const observer_ptr<IScriptCompilationService> compiler,
      const observer_ptr<const LateScriptDiagnosticHandler> on_late_diagnostic,
      const data::ComposedScene& composed, const std::size_t occurrence,
      std::span<const std::vector<scene::SceneNode>> nodes,
      const uint32_t node_index)
      : loader_(loader)
      , resolver_(resolver)
      , compiler_(compiler)
      , on_late_diagnostic_(on_late_diagnostic)
      , composed_(&composed)
      , asset_(composed.occurrences.at(occurrence).scene.get())
      , nodes_(nodes)
      , node_index_(node_index)
      , node_(base::CheckedAt(nodes, occurrence).at(node_index))
    {
    }

    auto AttachSlot(const data::ComposedSlot& record,
      SceneScriptAttachmentResult& result) -> void
    {
      auto outcome = SceneScriptSlotOutcome {
        .node_index = node_index_,
        .slot_index = 0,
        .script = record.script,
        .state = State::kFailed,
        .diagnostics = {},
      };
      const auto script = record.script.IsNil()
        ? nullptr
        : loader_->GetScriptAsset(record.script, *asset_);

      auto scripting = node_.GetScripting();
      if (!scripting.AddSlot(script, record.execution_order)) {
        Fail(outcome, nullptr, "the script slot could not be added");
        result.slots.push_back(std::move(outcome));
        return;
      }
      const auto& slot = scripting.Slots().back();
      outcome.slot_index = static_cast<uint32_t>(scripting.Slots().size() - 1U);

      if (script == nullptr) {
        Fail(outcome, &slot,
          fmt::format(
            "script asset {} is not loaded", data::to_string(record.script)));
      } else {
        ApplyOverrides(outcome, slot, *script, record.parameters);
        Resolve(outcome, slot, script);
      }
      result.slots.push_back(std::move(outcome));
    }

  private:
    [[nodiscard]] auto Diagnostic(const SceneScriptSlotOutcome& outcome,
      const DiagnosticCode code, std::string message) const -> ScriptDiagnostic
    {
      return ScriptDiagnostic {
        .severity = DiagnosticSeverity::kError,
        .code = code,
        .message = std::move(message),
        .labels = {},
        .notes = {},
        .help = std::nullopt,
        .slot = SlotContext {
          .node = std::string(node_.GetName()),
          .slot_index = outcome.slot_index,
          .hook = "attach",
        },
      };
    }

    auto Fail(SceneScriptSlotOutcome& outcome, const Slot* slot,
      std::string message) -> void
    {
      auto diagnostic = Diagnostic(
        outcome, DiagnosticCode::kAttachFailed, std::move(message));
      LOG_F(ERROR, "{}", FormatSummary(diagnostic));
      if (slot != nullptr) {
        (void)node_.GetScripting().MarkSlotCompilationFailed(
          *slot, diagnostic.message);
      }
      outcome.state = State::kFailed;
      outcome.diagnostics.push_back(std::move(diagnostic));
    }

    auto Reject(SceneScriptSlotOutcome& outcome, std::string message) -> void
    {
      auto diagnostic = Diagnostic(
        outcome, DiagnosticCode::kOverrideRejected, std::move(message));
      LOG_F(WARNING, "{}", FormatSummary(diagnostic));
      outcome.diagnostics.push_back(std::move(diagnostic));
    }

    //! Applies the parameters that match a declaration and binds node
    //! parameters to the nodes created for them.
    auto ApplyOverrides(SceneScriptSlotOutcome& outcome, const Slot& slot,
      const data::ScriptAsset& script,
      std::span<const data::ComposedParameter> params) -> void
    {
      auto scripting = node_.GetScripting();
      for (const auto& [key, value, supplier] : params) {
        if (auto rejection = CheckParameterOverride(
              script, key, value, SupplierNodeCount(supplier))) {
          Reject(outcome, std::move(*rejection));
          continue;
        }
        const auto* node = std::get_if<data::ScriptNodeParam>(&value);
        if (node != nullptr
          && node->node_index != data::pak::scripting::kUnsetNodeParam) {
          const auto target = FindNode(supplier, *node);
          if (!target.has_value()) {
            Reject(outcome,
              fmt::format(
                "parameter '{}' names a node that was not created", key));
            continue;
          }
          (void)scripting.BindNodeParameter(slot, key, *target);
        }
        (void)scripting.SetParameter(slot, key, value);
      }
    }

    //! The node count of the scene that supplied a value, when it is one of
    //! the composed scenes.
    [[nodiscard]] auto SupplierNodeCount(
      const data::ChangeSupplier supplier) const -> std::optional<std::size_t>
    {
      if (supplier >= composed_->occurrences.size()) {
        return std::nullopt;
      }
      return composed_->occurrences.at(supplier).scene->GetNodes().size();
    }

    //! The node a node value names in the scene that supplied it.
    [[nodiscard]] auto FindNode(const data::ChangeSupplier supplier,
      const data::ScriptNodeParam& value) const
      -> std::optional<scene::SceneNode>
    {
      if (supplier >= nodes_.size()
        || value.node_index >= base::CheckedAt(nodes_, supplier).size()) {
        return std::nullopt;
      }
      auto node = base::CheckedAt(nodes_, supplier).at(value.node_index);
      if (value.node_path == data::pak::world::kNoNodePath) {
        return node;
      }
      const auto path = composed_->occurrences.at(supplier).scene->GetNodePath({
        .node_index = value.node_index,
        .node_path = value.node_path,
      });
      return node.GetInstance().Find(path);
    }

    //! Installs embedded bytecode, or submits source for compilation.
    auto Resolve(SceneScriptSlotOutcome& outcome, const Slot& slot,
      const std::shared_ptr<const data::ScriptAsset>& script) -> void
    {
      const auto read_resource
        = [this, script](const data::ResourceReferenceIndex index)
        -> std::shared_ptr<const data::ScriptResource> {
        return ReadScriptResource(*script, index);
      };
      const auto map_origin
        = [this, script](const data::ResourceReferenceIndex index)
        -> std::optional<ScriptBlobOrigin> {
        if (ReadScriptResource(*script, index) != nullptr) {
          return ScriptBlobOrigin::kEmbeddedResource;
        }
        return std::nullopt;
      };
      auto resolved = resolver_->Resolve(IScriptSourceResolver::ResolveRequest {
        .asset = *script,
        .load_script_resource = read_resource,
        .map_resource_origin = map_origin,
      });
      if (!resolved.ok || !resolved.blob.has_value()) {
        const auto external = script->TryGetExternalSourcePath();
        Fail(outcome, &slot,
          fmt::format("the script source could not be resolved: {}{}",
            resolved.error_message,
            external.has_value()
              ? fmt::format(" (external source '{}')", *external)
              : std::string {}));
        return;
      }

      auto blob = std::move(*resolved.blob);
      if (auto* bytecode = std::get_if<ScriptBytecodeBlob>(&blob)) {
        if (bytecode->Size() == 0) {
          Fail(outcome, &slot, "the script bytecode is empty");
          return;
        }
        (void)node_.GetScripting().MarkSlotReady(slot,
          std::make_shared<const CompiledScriptExecutable>(
            std::make_shared<const ScriptBytecodeBlob>(std::move(*bytecode))));
        outcome.state = State::kReady;
        return;
      }

      auto source = std::get<ScriptSourceBlob>(std::move(blob));
      if (source.Size() == 0) {
        Fail(outcome, &slot, "the script source is empty");
        return;
      }
      if (compiler_ == nullptr) {
        Fail(outcome, &slot, "no script compilation service is available");
        return;
      }
      Compile(
        slot, outcome.slot_index, script->GetAssetKey(), std::move(source));
      outcome.state = State::kCompiling;
    }

    //! The service calls back on completion; the callbacks share one copy of
    //! the slot and touch the node only while it lives.
    auto Compile(const Slot& slot, const uint32_t slot_index,
      const data::AssetKey& script_key, ScriptSourceBlob source) const -> void
    {
      const auto compile_key
        = ComputeScriptCompileKey(script_key, source, kCompileMode);
      const auto shared_slot = std::make_shared<const Slot>(slot);
      (void)compiler_->AcquireForSlot(
        IScriptCompilationService::Request {
          .compile_key = compile_key,
          .source = std::move(source),
          .compile_mode = kCompileMode,
        },
        IScriptCompilationService::SlotAcquireCallbacks {
          .on_ready
          = [node = node_, slot = shared_slot](
              std::shared_ptr<const ScriptBytecodeBlob> bytecode) mutable
            -> void {
            if (node.IsAlive()) {
              (void)node.GetScripting().MarkSlotReady(*slot,
                std::make_shared<const CompiledScriptExecutable>(
                  std::move(bytecode)));
            }
          },
          .on_failed
          = [node = node_, slot = shared_slot, slot_index,
              on_late_diagnostic = *on_late_diagnostic_](
              const std::vector<ScriptDiagnostic>& diagnostics) mutable
            -> void {
            if (!node.IsAlive()) {
              return;
            }
            if (on_late_diagnostic) {
              for (auto diagnostic : diagnostics) {
                if (!diagnostic.slot.has_value()) {
                  diagnostic.slot = SlotContext {
                    .node = std::string(node.GetName()),
                    .slot_index = slot_index,
                    .hook = "compile",
                  };
                }
                on_late_diagnostic(diagnostic);
              }
            }
            auto summary = std::string {};
            for (const auto& diagnostic : diagnostics) {
              if (!summary.empty()) {
                summary += '\n';
              }
              summary += FormatSummary(diagnostic);
            }
            (void)node.GetScripting().MarkSlotCompilationFailed(
              *slot, std::move(summary));
          },
        });
    }

    [[nodiscard]] auto ReadScriptResource(const data::ScriptAsset& script,
      const data::ResourceReferenceIndex index) const
      -> std::shared_ptr<const data::ScriptResource>
    {
      const auto key = loader_->MakeScriptResourceKeyForAsset(script, index);
      if (!key.has_value()) {
        return nullptr;
      }
      if (auto resource = loader_->GetScriptResource(*key)) {
        return resource;
      }
      return loader_->ReadScriptResourceForAsset(script, index);
    }

    observer_ptr<content::IAssetLoader> loader_;
    observer_ptr<const IScriptSourceResolver> resolver_;
    observer_ptr<IScriptCompilationService> compiler_;
    observer_ptr<const LateScriptDiagnosticHandler> on_late_diagnostic_;
    observer_ptr<const data::ComposedScene> composed_;
    observer_ptr<const data::SceneAsset> asset_;
    std::span<const std::vector<scene::SceneNode>> nodes_;
    uint32_t node_index_;
    scene::SceneNode node_;
  };
} // namespace

SceneScriptAttachment::SceneScriptAttachment(content::IAssetLoader& loader,
  const IScriptSourceResolver& resolver,
  const observer_ptr<IScriptCompilationService> compiler,
  LateScriptDiagnosticHandler on_late_diagnostic)
  : loader_(&loader)
  , resolver_(&resolver)
  , compiler_(compiler)
  , on_late_diagnostic_(std::move(on_late_diagnostic))
{
}

auto SceneScriptAttachment::Attach(const data::ComposedScene& composed,
  const std::size_t occurrence,
  const std::span<const std::vector<scene::SceneNode>> nodes) const
  -> SceneScriptAttachmentResult
{
  auto result = SceneScriptAttachmentResult {};
  if (occurrence >= composed.occurrences.size() || occurrence >= nodes.size()) {
    return result;
  }
  const auto& created = base::CheckedAt(nodes, occurrence);
  for (const auto& slot : composed.occurrences.at(occurrence).slots) {
    const auto node_index = slot.node_index;
    if (node_index >= created.size() || !created.at(node_index).IsAlive()) {
      LOG_F(
        ERROR, "script slot names node {}, which was not created", node_index);
      continue;
    }
    auto node = created.at(node_index);
    if (!node.HasScripting() && !node.AttachScripting()) {
      LOG_F(ERROR, "cannot attach scripting to node {}", node_index);
      continue;
    }
    auto attachment = NodeAttachment(loader_, resolver_, compiler_,
      observer_ptr { &on_late_diagnostic_ }, composed, occurrence, nodes,
      node_index);
    attachment.AttachSlot(slot, result);
  }
  return result;
}

} // namespace oxygen::scripting
