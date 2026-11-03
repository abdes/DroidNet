//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/AssetReferenceBuilder.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/InputSource.h>
#include <Oxygen/Cooker/Import/Internal/Pipelines/InputImportPipeline.h>
#include <Oxygen/Cooker/Import/Internal/Utils/MountedCookedSources.h>
#include <Oxygen/Cooker/Import/Internal/Utils/VirtualPathResolution.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/DescriptorStrings.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>

namespace oxygen::content::import {

namespace {

  using data::AssetType;
  using data::pak::input::InputActionAssetDesc;
  using data::pak::input::InputActionAssetFlags;
  using data::pak::input::InputActionMappingRecord;
  using data::pak::input::InputMappingContextAssetDesc;
  using data::pak::input::InputMappingContextFlags;
  using data::pak::input::InputTriggerAuxRecord;
  using data::pak::input::InputTriggerRecord;
  using data::pak::input::InputTriggerType;
  using nlohmann::json;

  struct BuiltContextAsset final {
    std::string name;
    data::AssetKey key;
    std::string virtual_path;
    std::string descriptor_relpath;
    InputMappingContextFlags flags = InputMappingContextFlags::kNone;
    int32_t default_priority = 0;
    std::vector<InputActionMappingRecord> mappings;
    std::vector<InputTriggerRecord> triggers;
    std::vector<InputTriggerAuxRecord> trigger_aux;
    //! The descriptor string table, holding the slot names.
    data::DescriptorStringsBuilder strings;
  };

  [[nodiscard]] auto IsStopRequested(const std::stop_token& token) noexcept
    -> bool
  {
    return token.stop_possible() && token.stop_requested();
  }

  auto AddDiagnostic(ImportSession& session, const ImportRequest& request,
    const ImportSeverity severity, std::string code, std::string message,
    std::string json_pointer = {}) -> void
  {
    session.AddDiagnostic({
      .severity = severity,
      .code = std::move(code),
      .message = std::move(message),
      .source_path = request.source_path.string(),
      .json_pointer = std::move(json_pointer),
    });
  }

  //! The last extension of an input source's identity (`Player.input`).
  constexpr std::string_view kInputSourceSuffix = ".input";

  //! Where an input source's products are named: under its identity.
  /*!
   The source's identity is its path without the authoring `.json`
   (`/Content/Input/Player.input`); each product is named under it
   (`/Content/Input/Player.input/Jump.oiact`), and its descriptor lives at
   the same place under the cooked root.
  */
  struct InputContainer final {
    internal::AssetPlacement placement;

    [[nodiscard]] auto ProductVirtualPath(const std::string_view name,
      const std::string_view extension) const -> std::string
    {
      return placement.virtual_path + "/" + std::string(name)
        + std::string(extension);
    }

    [[nodiscard]] auto ProductRelPath(const std::string_view name,
      const std::string_view extension) const -> std::string
    {
      return placement.descriptor_relpath + "/" + std::string(name)
        + std::string(extension);
    }
  };

  auto SerializeAction(const ActionSource& action) -> std::vector<std::byte>
  {
    auto desc = InputActionAssetDesc {};
    desc.header.asset_type = static_cast<uint8_t>(AssetType::kInputAction);
    desc.header.version = data::pak::input::kInputActionAssetVersion;
    auto strings = data::DescriptorStringsBuilder {};
    desc.header.name_offset = strings.Add(action.name);
    desc.value_type = action.value_type;
    desc.flags = action.consumes_input ? InputActionAssetFlags::kConsumesInput
                                       : InputActionAssetFlags::kNone;
    if (action.cumulative) {
      desc.flags |= InputActionAssetFlags::kCumulativeAccumulation;
    }

    return data::MakeDescriptorBytes(desc, strings);
  }

  auto SerializeContext(const BuiltContextAsset& context, std::string& error)
    -> std::optional<std::vector<std::byte>>
  {
    if (context.mappings.size() > std::numeric_limits<uint32_t>::max()
      || context.triggers.size() > std::numeric_limits<uint32_t>::max()
      || context.trigger_aux.size() > std::numeric_limits<uint32_t>::max()) {
      error = "context record table exceeds uint32 limits";
      return std::nullopt;
    }

    auto desc = InputMappingContextAssetDesc {};
    desc.header.asset_type
      = static_cast<uint8_t>(AssetType::kInputMappingContext);
    desc.header.version = data::pak::input::kInputMappingContextAssetVersion;
    auto strings = context.strings;
    desc.header.name_offset = strings.Add(context.name);
    desc.flags = context.flags;
    desc.default_priority = context.default_priority;

    desc.mappings.offset = sizeof(InputMappingContextAssetDesc);
    desc.mappings.count = static_cast<uint32_t>(context.mappings.size());
    desc.mappings.entry_size = sizeof(InputActionMappingRecord);
    desc.triggers.offset = desc.mappings.offset
      + (context.mappings.size() * sizeof(InputActionMappingRecord));
    desc.triggers.count = static_cast<uint32_t>(context.triggers.size());
    desc.triggers.entry_size = sizeof(InputTriggerRecord);
    desc.trigger_aux.offset = desc.triggers.offset
      + (context.triggers.size() * sizeof(InputTriggerRecord));
    desc.trigger_aux.count = static_cast<uint32_t>(context.trigger_aux.size());
    desc.trigger_aux.entry_size = sizeof(InputTriggerAuxRecord);

    serio::MemoryStream stream;
    serio::Writer writer(stream);
    auto packed = writer.ScopedAlignment(1);
    if (!writer
          .WriteBlob(std::as_bytes(
            std::span<const InputMappingContextAssetDesc, 1>(&desc, 1)))
          .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.mappings)))
        .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.triggers)))
        .has_value()
      || !writer.WriteBlob(std::as_bytes(std::span(context.trigger_aux)))
        .has_value()) {
      error = "failed writing context descriptor payload";
      return std::nullopt;
    }

    const auto data = stream.Data();
    auto bytes = std::vector<std::byte>(data.begin(), data.end());
    data::AppendDescriptorStrings(bytes, strings);
    return bytes;
  }

  //! Resolves a mapping's action: a local name this source declares, or a
  //! reference to an action, told apart by the `.oiact` suffix.
  class ActionResolver final {
  public:
    ActionResolver(const InputSourceDocument& source, ImportSession& session,
      const ImportRequest& request)
      : source_(source)
      , session_(session)
      , request_(request)
    {
    }

    [[nodiscard]] auto Resolve(const std::string& action,
      const json::json_pointer& location) -> std::optional<data::AssetKey>
    {
      if (!action.ends_with(data::kInputActionDescriptorExtension)) {
        // The preparation refused a name that no action of the source has.
        const auto it = source_.local_action_keys.find(action);
        CHECK_F(it != source_.local_action_keys.end(),
          "Prepared input source names no action '{}'", action);
        return it->second;
      }

      const auto reference = internal::ResolveJobReference(request_, action);
      if (!reference.has_value()) {
        session_.AddDiagnostic(internal::RefuseReference(
          request_, action, reference.error(), location.to_string()));
        return std::nullopt;
      }
      // A product of this source is not mounted yet; the source declares it.
      if (const auto it = source_.local_action_keys.find(reference->resolved);
        it != source_.local_action_keys.end()) {
        return it->second;
      }
      if (!mounts_.has_value()) {
        mounts_ = internal::LoadMountedCookedSources(
          session_, request_, "input.import.");
      }
      const auto resolved = internal::LookupReference(
        *mounts_, request_, *reference, AssetType::kInputAction);
      if (resolved.has_value()) {
        return resolved->key;
      }
      using internal::AssetReferenceRefusal;
      const auto& refused = resolved.error();
      if (refused.reason == AssetReferenceRefusal::kUnmounted) {
        session_.AddDiagnostic(internal::RefuseUnmountedReference(
          request_, *reference, location.to_string()));
      } else if (refused.reason == AssetReferenceRefusal::kTypeMismatch) {
        Refuse("input.context.action_type_mismatch",
          internal::DescribeLookup(*reference, refused)
            + "; it does not name an input action",
          location.to_string());
      } else {
        Refuse("input.context.action_unresolved",
          internal::DescribeLookup(*reference, refused), location.to_string());
      }
      return std::nullopt;
    }

  private:
    auto Refuse(std::string code, std::string message, std::string json_pointer)
      -> void
    {
      AddDiagnostic(session_, request_, ImportSeverity::kError, std::move(code),
        std::move(message), std::move(json_pointer));
    }

    // NOLINTBEGIN(*-avoid-const-or-ref-data-members)
    const InputSourceDocument& source_;
    ImportSession& session_;
    const ImportRequest& request_;
    // NOLINTEND(*-avoid-const-or-ref-data-members)
    std::optional<std::vector<internal::MountedCookedSource>> mounts_;
  };

  auto BuildInputContextAssets(const InputSourceDocument& source,
    const InputContainer& container, ImportSession& session,
    const ImportRequest& request)
    -> std::optional<std::vector<BuiltContextAsset>>
  {
    auto actions = ActionResolver { source, session, request };
    const auto resolve_action_key
      = [&](const std::string& reference,
          const json::json_pointer& location) -> std::optional<data::AssetKey> {
      return actions.Resolve(reference, location);
    };

    auto built_contexts = std::vector<BuiltContextAsset> {};
    built_contexts.reserve(source.contexts.size());
    for (const auto& source_ctx : source.contexts) {
      auto context = BuiltContextAsset {};
      context.name = source_ctx.name;
      context.flags = InputMappingContextFlags::kNone;
      context.default_priority = source_ctx.default_priority;
      context.descriptor_relpath = container.ProductRelPath(
        context.name, data::kInputMappingContextDescriptorExtension);
      context.virtual_path = container.ProductVirtualPath(
        context.name, data::kInputMappingContextDescriptorExtension);
      context.key = data::AssetKey::FromVirtualPath(context.virtual_path);

      const auto intern_slot
        = [&](const std::string& slot_name) -> std::optional<uint32_t> {
        try {
          return context.strings.Add(slot_name);
        } catch (const std::length_error&) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.context.string_table_overflow",
            "Input mapping context string table exceeded uint32 limits",
            source_ctx.json_pointer);
          return std::nullopt;
        }
      };

      bool build_ok = true;
      for (const auto& source_mapping : source_ctx.mappings) {
        const auto mapping_path
          = json::json_pointer { source_mapping.json_pointer };
        const auto action_key = resolve_action_key(
          source_mapping.action_name, mapping_path / "action");
        const auto slot_offset = intern_slot(source_mapping.slot_name);
        if (!action_key.has_value() || !slot_offset.has_value()) {
          build_ok = false;
          break;
        }

        if (context.triggers.size() > std::numeric_limits<uint32_t>::max()) {
          AddDiagnostic(session, request, ImportSeverity::kError,
            "input.context.trigger_count_overflow",
            "Trigger table exceeded uint32 limits", mapping_path.to_string());
          build_ok = false;
          break;
        }
        const auto trigger_start
          = static_cast<uint32_t>(context.triggers.size());

        for (const auto& source_trigger : source_mapping.triggers) {
          const auto trigger_path
            = json::json_pointer { source_trigger.json_pointer };
          auto trigger = InputTriggerRecord {};
          trigger.type = source_trigger.type;
          trigger.behavior = source_trigger.behavior;
          trigger.actuation_threshold = source_trigger.actuation_threshold;
          if (source_trigger.has_hold_time) {
            trigger.fparams[0] = source_trigger.hold_time;
          }
          if (source_trigger.has_interval) {
            trigger.fparams[1] = source_trigger.interval;
          }
          if (source_trigger.linked_action_name.has_value()) {
            const auto linked
              = resolve_action_key(*source_trigger.linked_action_name,
                trigger_path / "chord_action");
            if (!linked.has_value()) {
              build_ok = false;
              break;
            }
            trigger.linked_action_asset_key = *linked;
          }

          if (source_trigger.type == InputTriggerType::kCombo) {
            trigger.aux_start_index
              = static_cast<uint32_t>(context.trigger_aux.size());
            trigger.aux_count
              = static_cast<uint32_t>(source_trigger.aux.size());
            for (const auto& source_aux : source_trigger.aux) {
              const auto aux_key = resolve_action_key(source_aux.action_name,
                json::json_pointer { source_aux.json_pointer } / "action");
              if (!aux_key.has_value()) {
                build_ok = false;
                break;
              }
              context.trigger_aux.push_back(InputTriggerAuxRecord {
                .action_asset_key = *aux_key,
                .completion_states = source_aux.completion_states,
                .time_to_complete_ns = source_aux.time_to_complete_ns,
                .flags = 0,
              });
            }
          }

          if (!build_ok) {
            break;
          }
          context.triggers.push_back(trigger);
        }

        if (!build_ok) {
          break;
        }

        const auto trigger_end = static_cast<uint32_t>(context.triggers.size());
        auto mapping = InputActionMappingRecord {};
        mapping.action_asset_key = *action_key;
        mapping.slot_name_offset = *slot_offset;
        mapping.trigger_start_index = trigger_start;
        mapping.trigger_count = trigger_end - trigger_start;
        mapping.scale[0] = source_mapping.scale[0];
        mapping.scale[1] = source_mapping.scale[1];
        mapping.bias[0] = source_mapping.bias[0];
        mapping.bias[1] = source_mapping.bias[1];
        context.mappings.push_back(mapping);
      }

      if (!build_ok) {
        return std::nullopt;
      }
      built_contexts.push_back(std::move(context));
    }

    return built_contexts;
  }

  auto EmitInputActions(const std::vector<ActionSource>& actions,
    const InputContainer& container, ImportSession& session,
    const ImportRequest& request) -> bool
  {
    for (const auto& action : actions) {
      const auto bytes = SerializeAction(action);
      try {
        session.AssetEmitter().Emit(action.key, AssetType::kInputAction,
          action.virtual_path,
          container.ProductRelPath(
            action.name, data::kInputActionDescriptorExtension),
          bytes, {});
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.asset.descriptor_emit_failed", ex.what(),
          "actions." + action.name);
        return false;
      }
    }
    return true;
  }

  auto EmitInputContexts(const std::vector<BuiltContextAsset>& contexts,
    ImportSession& session, const ImportRequest& request) -> bool
  {
    for (const auto& context : contexts) {
      auto error = std::string {};
      const auto bytes = SerializeContext(context, error);
      if (!bytes.has_value()) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.context.serialize_failed", error, "contexts." + context.name);
        return false;
      }
      try {
        AssetReferenceBuilder references;
        for (const auto& mapping : context.mappings) {
          references.AddAsset(
            mapping.action_asset_key, AssetType::kInputAction);
        }
        for (const auto& trigger : context.triggers) {
          references.AddAsset(
            trigger.linked_action_asset_key, AssetType::kInputAction);
        }
        for (const auto& auxiliary : context.trigger_aux) {
          references.AddAsset(
            auxiliary.action_asset_key, AssetType::kInputAction);
        }
        session.AssetEmitter().Emit(context.key,
          AssetType::kInputMappingContext, context.virtual_path,
          context.descriptor_relpath, *bytes, std::move(references).Build());
      } catch (const std::exception& ex) {
        AddDiagnostic(session, request, ImportSeverity::kError,
          "input.context.descriptor_emit_failed", ex.what(),
          "contexts." + context.name);
        return false;
      }
    }
    return true;
  }

} // namespace

InputImportPipeline::InputImportPipeline()
  : InputImportPipeline(Config {})
{
}

InputImportPipeline::InputImportPipeline(Config config)
  : config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

InputImportPipeline::~InputImportPipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "InputImportPipeline destroyed with {} pending items", PendingCount());
  }
  input_channel_.Close();
  output_channel_.Close();
}

auto InputImportPipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "InputImportPipeline::Start() called more than once");
  started_ = true;
  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this] -> co::Co<> { co_await Worker(); });
  }
}

auto InputImportPipeline::Submit(WorkItem item) -> co::Co<>
{
  pending_.fetch_add(1, std::memory_order_acq_rel);
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto InputImportPipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed() || input_channel_.Full()) {
    return false;
  }
  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    pending_.fetch_add(1, std::memory_order_acq_rel);
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto InputImportPipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .telemetry = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto InputImportPipeline::Close() -> void { input_channel_.Close(); }

auto InputImportPipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto InputImportPipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto InputImportPipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto InputImportPipeline::Worker() -> co::Co<>
{
  const auto MakeDuration
    = [](const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end)
    -> std::chrono::microseconds {
    return std::chrono::duration_cast<std::chrono::microseconds>(end - start);
  };

  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (IsStopRequested(item.stop_token)) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto start = std::chrono::steady_clock::now();
    auto result = WorkResult {
      .source_id = item.source_id,
      .telemetry = {},
      .success = false,
    };

    try {
      result.success = co_await Process(item);
    } catch (const std::exception& ex) {
      if (item.session != nullptr) {
        AddDiagnostic(*item.session, item.session->Request(),
          ImportSeverity::kError, "input.import.pipeline_exception",
          std::string("Unhandled exception in InputImportPipeline: ")
            + ex.what());
      }
      result.success = false;
    }

    result.telemetry.cook_duration
      = MakeDuration(start, std::chrono::steady_clock::now());
    co_await output_channel_.Send(std::move(result));
  }

  co_return;
}

auto InputImportPipeline::Process(WorkItem& item) -> co::Co<bool>
{
  auto* const session = item.session.get();
  if (session == nullptr) {
    co_return false;
  }

  const auto& request = session->Request();
  if (!request.input.has_value()) {
    AddDiagnostic(*session, request, ImportSeverity::kError,
      "input.request.invalid_job_type",
      "Input import pipeline requires request input payload to be set");
    co_return false;
  }

  auto placement = internal::PlaceAsset(request, kInputSourceSuffix);
  if (!placement.has_value()) {
    session->AddDiagnostic(std::move(placement.error()));
    co_return false;
  }
  const auto container = InputContainer { .placement = std::move(*placement) };

  std::string source_text {};
  const auto source_bytes = item.source_bytes.Bytes();
  source_text.resize(source_bytes.size());
  if (!source_bytes.empty()) {
    std::memcpy(source_text.data(), source_bytes.data(), source_bytes.size());
  }

  auto diagnostics = std::vector<ImportDiagnostic> {};
  const auto parsed_source = PrepareInputDocument(source_text,
    request.source_path, container.placement.virtual_path, diagnostics);
  for (auto& diagnostic : diagnostics) {
    session->AddDiagnostic(std::move(diagnostic));
  }
  if (!parsed_source.has_value()) {
    co_return false;
  }

  auto built_contexts
    = BuildInputContextAssets(*parsed_source, container, *session, request);
  if (!built_contexts.has_value()) {
    co_return false;
  }

  if (!EmitInputActions(parsed_source->actions, container, *session, request)) {
    co_return false;
  }
  if (!EmitInputContexts(*built_contexts, *session, request)) {
    co_return false;
  }

  co_return !session->HasErrors();
}

auto InputImportPipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  co_await output_channel_.Send(WorkResult {
    .source_id = std::move(item.source_id),
    .telemetry = {},
    .success = false,
  });
}

} // namespace oxygen::content::import
