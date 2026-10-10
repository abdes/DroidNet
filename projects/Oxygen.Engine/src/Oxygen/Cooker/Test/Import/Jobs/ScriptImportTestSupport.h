//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/MaterialSlotProvenance.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/Diagnostics.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/ImportHarness.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace lc = oxygen::content::lc;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::ReadBytes;
using oxygen::cooker::test::ScopedTempDir;
using oxygen::cooker::test::SubmitAndWait;
using oxygen::cooker::test::WriteText;
using oxygen::data::AssetType;

inline auto CountSeverity(const std::vector<ImportDiagnostic>& diagnostics,
  const ImportSeverity severity) -> uint32_t
{
  return static_cast<uint32_t>(std::ranges::count_if(
    diagnostics, [severity](const ImportDiagnostic& diagnostic) -> bool {
      return diagnostic.severity == severity;
    }));
}

inline auto ExpectDiagnosticFieldsComplete(
  const std::vector<ImportDiagnostic>& diagnostics) -> void
{
  ASSERT_FALSE(diagnostics.empty());
  for (const auto& diagnostic : diagnostics) {
    const auto valid_severity = diagnostic.severity == ImportSeverity::kInfo
      || diagnostic.severity == ImportSeverity::kWarning
      || diagnostic.severity == ImportSeverity::kError;
    EXPECT_TRUE(valid_severity);
    EXPECT_FALSE(diagnostic.code.empty());
    EXPECT_FALSE(diagnostic.message.empty());
    EXPECT_FALSE(diagnostic.source_path.empty());
  }
}

inline auto ExpectPackagingSummaryMatchesDiagnostics(const ImportReport& report)
  -> void
{
  EXPECT_EQ(report.packaging.outputs_written,
    static_cast<uint32_t>(report.outputs.size()));
  EXPECT_EQ(report.packaging.diagnostics_info,
    CountSeverity(report.diagnostics, ImportSeverity::kInfo));
  EXPECT_EQ(report.packaging.diagnostics_warning,
    CountSeverity(report.diagnostics, ImportSeverity::kWarning));
  EXPECT_EQ(report.packaging.diagnostics_error,
    CountSeverity(report.diagnostics, ImportSeverity::kError));
}

inline auto MakeScriptRequest(const std::filesystem::path& source_path,
  const std::filesystem::path& cooked_root, const ScriptStorageMode storage,
  const bool compile_scripts = false) -> ImportRequest
{
  ImportRequest request {};
  request.source_path = source_path;
  request.cooked_root = cooked_root;
  request.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  request.options.scripting.compile_scripts = compile_scripts;
  request.options.scripting.script_storage = storage;
  request.options.scripting.source_root = cooked_root.parent_path();
  return request;
}

inline auto MakeSidecarRequest(const std::filesystem::path& source_path,
  const std::filesystem::path& cooked_root, std::string scene_virtual_path)
  -> ImportRequest
{
  ImportRequest request {};
  request.source_path = source_path;
  request.cooked_root = cooked_root;
  request.options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  request.options.scripting.target_scene_virtual_path
    = std::move(scene_virtual_path);
  return request;
}

inline auto CanonicalScriptVirtualPath(std::string_view script_name)
  -> std::string
{
  return ImportRequest {}.loose_cooked_layout.ScriptVirtualPath(script_name);
}

inline auto CanonicalSceneVirtualPath(std::string_view scene_name)
  -> std::string
{
  return ImportRequest {}.loose_cooked_layout.SceneVirtualPath(scene_name);
}

struct CallbackCapture final {
  ImportReport report {};
  std::vector<ImportPhase> phases;
  uint32_t completion_calls = 0;
};

inline auto SubmitAndCaptureCallbacks(AsyncImportService& service,
  ImportRequest request) -> std::optional<CallbackCapture>
{
  auto capture = CallbackCapture {};
  auto callback_mutex = std::mutex {};
  std::latch done(1);

  const auto submitted = service.SubmitImport(
    std::move(request),
    [&capture, &callback_mutex, &done](
      const auto /*job_id*/, const ImportReport& completed) -> auto {
      std::scoped_lock lock(callback_mutex);
      ++capture.completion_calls;
      capture.report = completed;
      done.count_down();
    },
    [&capture, &callback_mutex](const ProgressEvent& progress) -> void {
      std::scoped_lock lock(callback_mutex);
      capture.phases.push_back(progress.header.phase);
    });
  if (!submitted.has_value()) {
    return std::nullopt;
  }

  done.wait();
  return capture;
}

inline auto ContainsPhase(
  const std::vector<ImportPhase>& phases, const ImportPhase phase) -> bool
{
  return std::ranges::find(phases, phase) != phases.end();
}

class ScopedImportService final {
public:
  explicit ScopedImportService(const AsyncImportService::Config& config)
    : service_(config)
  {
  }

  ~ScopedImportService() { service_.Stop(); }

  ScopedImportService(const ScopedImportService&) = delete;
  auto operator=(const ScopedImportService&) -> ScopedImportService& = delete;
  ScopedImportService(ScopedImportService&&) = delete;
  auto operator=(ScopedImportService&&) -> ScopedImportService& = delete;

  auto Service() noexcept -> AsyncImportService& { return service_; }

private:
  AsyncImportService service_;
};

struct AssetRef final {
  data::AssetKey key;
  std::string virtual_path;
  std::string descriptor_relpath;
  data::AssetReferences references;
  AssetType type = AssetType::kUnknown;
};

inline auto FindFirstAssetByType(const lc::Inspection& inspection,
  const AssetType type) -> std::optional<AssetRef>
{
  for (const auto& entry : inspection.Assets()) {
    if (static_cast<AssetType>(entry.asset_type) != type) {
      continue;
    }
    return AssetRef {
      .key = entry.key,
      .virtual_path = entry.virtual_path,
      .descriptor_relpath = entry.descriptor_relpath,
      .references = entry.references,
      .type = static_cast<AssetType>(entry.asset_type),
    };
  }
  return std::nullopt;
}

inline auto FindScriptAssetByDescriptorName(const lc::Inspection& inspection,
  const std::string_view descriptor_name) -> std::optional<AssetRef>
{
  for (const auto& asset : inspection.Assets()) {
    if (static_cast<AssetType>(asset.asset_type) != AssetType::kScript) {
      continue;
    }
    const auto name
      = std::filesystem::path(asset.descriptor_relpath).filename().string();
    if (name != descriptor_name) {
      continue;
    }
    return AssetRef {
      .key = asset.key,
      .virtual_path = asset.virtual_path,
      .descriptor_relpath = asset.descriptor_relpath,
      .references = asset.references,
      .type = AssetType::kScript,
    };
  }
  return std::nullopt;
}

struct SceneScriptState {
  std::vector<data::pak::scripting::ScriptSlotRecord> slots;
  std::vector<data::pak::scripting::ScriptParamRecord> params;
  uint64_t parameter_base = 0U;
};

inline auto ReadSceneScriptState(
  const std::filesystem::path& root, const AssetRef& asset) -> SceneScriptState
{
  const auto bytes = ReadBytes(root / asset.descriptor_relpath);
  const auto scene = data::SceneAsset(asset.key, bytes);
  uint32_t count = 0U;
  for (const auto& component :
    scene.GetComponents<data::pak::scripting::ScriptingComponentRecord>()) {
    count += component.slot_count;
  }
  SceneScriptState result;
  result.slots = scene.ReadScriptSlots(0U, count);
  for (const auto& slot : result.slots) {
    const auto params = scene.ReadScriptParameters(slot);
    if (!params.empty() && result.params.empty()) {
      result.parameter_base = slot.params_array_offset;
    }
    result.params.insert(result.params.end(), params.begin(), params.end());
  }
  return result;
}

inline auto MakeInflightSceneContext(const std::filesystem::path& cooked_root,
  const AssetRef& scene_asset) -> ImportRequest::InflightSceneContext
{
  return ImportRequest::InflightSceneContext {
    .scene_key = scene_asset.key,
    .virtual_path = scene_asset.virtual_path,
    .descriptor_relpath = scene_asset.descriptor_relpath,
    .descriptor_bytes = ReadBytes(cooked_root / scene_asset.descriptor_relpath),
    .references = scene_asset.references,
  };
}

//! One script binding of a sidecar payload. Unset optional fields are
//! omitted from the emitted JSON.
struct SidecarBindingSpec final {
  uint32_t node_index = 0;
  std::string slot_id = "main";
  std::string script_virtual_path;
  std::optional<int32_t> execution_order;
  nlohmann::json params;
};

//! Builds sidecar JSON for several bindings.
inline auto MakeSidecarPayload(const std::vector<SidecarBindingSpec>& bindings)
  -> std::string
{
  auto binding_list = nlohmann::json::array();
  for (const auto& spec : bindings) {
    auto binding = nlohmann::json::object();
    binding["node_index"] = spec.node_index;
    binding["slot_id"] = spec.slot_id;
    binding["script_virtual_path"] = spec.script_virtual_path;
    if (spec.execution_order.has_value()) {
      binding["execution_order"] = *spec.execution_order;
    }
    if (!spec.params.is_null()) {
      binding["params"] = spec.params;
    }
    binding_list.push_back(std::move(binding));
  }
  auto payload = nlohmann::json::object();
  payload["bindings"] = std::move(binding_list);
  return payload.dump(2) + "\n";
}

//! Builds sidecar JSON for a single binding.
inline auto MakeSidecarPayload(const SidecarBindingSpec& binding) -> std::string
{
  return MakeSidecarPayload(std::vector { binding });
}

//! Binding to node 0, slot `main`, order 2, with one float `speed` param.
inline auto MakeSpeedBinding(const std::string_view script_virtual_path,
  const float speed = 3.5F) -> SidecarBindingSpec
{
  auto param = nlohmann::json::object();
  param["key"] = "speed";
  param["type"] = "float";
  param["value"] = speed;
  return SidecarBindingSpec {
    .node_index = 0,
    .slot_id = "main",
    .script_virtual_path = std::string(script_virtual_path),
    .execution_order = 2,
    .params = nlohmann::json::array({ std::move(param) }),
  };
}

class ScriptingImportTestBase : public testing::Test {
protected:
  AsyncImportService::Config config_ {
    .thread_pool_size = 2,
  };
  std::unique_ptr<ScopedImportService> service_;

  void SetUp() override
  {
    service_ = std::make_unique<ScopedImportService>(config_);
  }

  void TearDown() override { service_.reset(); }

  [[nodiscard]] auto Service() noexcept -> AsyncImportService&
  {
    return service_->Service();
  }

  [[nodiscard]] static auto ModelPath() -> std::filesystem::path
  {
    return oxygen::cooker::test::ModelPath("Tabuleiro.glb");
  }

  [[nodiscard]] auto Submit(ImportRequest request) -> ImportReport
  {
    auto report = SubmitAndWait(Service(), std::move(request));
    if (!report.has_value()) {
      return ImportReport {};
    }
    if (report->success && !report->material_slot_provenance_json.empty()) {
      source_provenance_
        = MaterialSlotProvenance::Parse(report->material_slot_provenance_json);
    }
    return *report;
  }

  [[nodiscard]] auto MakeSceneRequest(const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_root) const -> ImportRequest
  {
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = cooked_root;
    request.material_slot_provenance = source_provenance_;
    return request;
  }

  [[nodiscard]] static auto LoadInspection(
    const std::filesystem::path& cooked_root) -> lc::Inspection
  {
    auto inspection = lc::Inspection {};
    inspection.LoadFromFile(cooked_root / "container.index.bin");
    return inspection;
  }

  //! Scene and script assets found in a cooked root, plus its inspection.
  struct SceneAndScript final {
    lc::Inspection inspection;
    std::optional<AssetRef> scene;
    std::optional<AssetRef> script;
  };

  //! Imports an external script, then `Tabuleiro.glb`, into `cooked_root` and
  //! finds the first scene and script assets. Missing assets are reported as
  //! failures and left empty, so callers must still check them.
  [[nodiscard]] auto CookSceneWithScript(
    const std::filesystem::path& cooked_root,
    const std::string_view script_text,
    const std::string_view script_file_name = "main_logic.luau")
    -> SceneAndScript
  {
    const auto script_source = cooked_root / "input" / script_file_name;
    WriteText(script_source, script_text);

    const auto model_path = ModelPath();
    EXPECT_TRUE(std::filesystem::exists(model_path));
    EXPECT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    EXPECT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    auto result = SceneAndScript { .inspection = LoadInspection(cooked_root) };
    result.scene = FindFirstAssetByType(result.inspection, AssetType::kScene);
    result.script = FindFirstAssetByType(result.inspection, AssetType::kScript);
    return result;
  }

private:
  std::shared_ptr<const MaterialSlotProvenance> source_provenance_
    = std::make_shared<const MaterialSlotProvenance>(Uuid::Generate());
};

} // namespace oxygen::content::import::test
