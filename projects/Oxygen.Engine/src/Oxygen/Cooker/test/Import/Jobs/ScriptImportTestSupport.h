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
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/AuthoredFiles.h>
#include <Oxygen/Cooker/test/Support/CookedFiles.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Core/Types/DescriptorExtensions.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

namespace oxygen::content::import::test {

namespace lc = oxygen::content::lc;
using oxygen::cooker::test::CookedFile;
using oxygen::cooker::test::HasCookedFile;
using oxygen::cooker::test::HasDiagnosticCode;
using oxygen::cooker::test::ReadBytes;
using oxygen::cooker::test::SpellReference;
using oxygen::cooker::test::SubmitAndWait;
using oxygen::cooker::test::WriteText;
using oxygen::data::AssetType;
using oxygen::testing::ScopedTempDir;

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

//! The identity these tests give a script: its name at the mount root.
inline auto CanonicalScriptVirtualPath(std::string_view script_name)
  -> std::string
{
  return std::string("/Cooked/") + std::string(script_name)
    + std::string(data::kScriptDescriptorExtension);
}

//! A script asset import: the script, where it cooks and how it is stored.
struct ScriptRequestSpec final {
  std::filesystem::path source_path;
  std::filesystem::path cooked_root;
  ScriptStorageMode storage = ScriptStorageMode::kEmbedded;
  bool compile_scripts = false;
};

inline auto MakeScriptRequest(const ScriptRequestSpec& spec) -> ImportRequest
{
  ImportRequest request {};
  request.source_path = spec.source_path;
  request.cooked_root = spec.cooked_root;
  request.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
  request.options.scripting.compile_scripts = spec.compile_scripts;
  request.options.scripting.script_storage = spec.storage;
  request.asset_virtual_path
    = CanonicalScriptVirtualPath(spec.source_path.stem().string());
  request.source_mount_path
    = oxygen::cooker::test::SourceMountPath(request.source_path);
  return request;
}

//! A scripting sidecar import: the sidecar, where it cooks and the scene it
//! binds scripts to.
struct SidecarRequestSpec final {
  std::filesystem::path source_path;
  std::filesystem::path cooked_root;
  std::string scene_virtual_path;
};

inline auto MakeSidecarRequest(SidecarRequestSpec spec) -> ImportRequest
{
  ImportRequest request {};
  request.source_path = std::move(spec.source_path);
  request.cooked_root = std::move(spec.cooked_root);
  request.options.scripting.import_kind
    = ScriptingImportKind::kScriptingSidecar;
  request.options.scripting.target_scene_virtual_path
    = std::move(spec.scene_virtual_path);
  return request;
}

//! A model scene import: the model and where it cooks.
struct SceneRequestSpec final {
  std::filesystem::path source_path;
  std::filesystem::path cooked_root;
};

//! A scene cooked with an external script: where both cook, and the script's
//! text and file name.
struct SceneWithScriptSpec final {
  std::filesystem::path cooked_root;
  std::string_view script_text;
  std::string_view script_file_name = "main_logic.luau";
};

//! The identity these tests give an authored scene: its name at the mount
//! root.
inline auto CanonicalSceneVirtualPath(std::string_view scene_name)
  -> std::string
{
  return std::string("/Cooked/") + std::string(scene_name)
    + std::string(data::kSceneDescriptorExtension);
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
  std::string source_path;
};

//! The path of the descriptor of `key` below the loose root `content` reads:
//! where the writer lays it out.
inline auto DescriptorRelPath(const cooked::CookedContent& content,
  const data::AssetKey& key) -> std::string
{
  return content.DescriptorStored(key)
    .file.lexically_relative(content.Source().path)
    .generic_string();
}

//! The file name of the descriptor of `key` as the layout names it: the
//! stored file carries its content digest, the layout name does not.
inline auto DescriptorLayoutName(const cooked::CookedContent& content,
  const data::AssetKey& key) -> std::string
{
  return oxygen::cooker::test::LayoutPathOf(
    content.DescriptorStored(key).file.filename().generic_string());
}

inline auto MakeAssetRef(const cooked::CookedContent& content,
  const cooked::CookedAsset& entry) -> AssetRef
{
  return AssetRef {
    .key = entry.key,
    .virtual_path = entry.virtual_path,
    .descriptor_relpath = DescriptorRelPath(content, entry.key),
    .references = entry.references,
    .type = entry.type,
    .source_path = entry.source_path,
  };
}

inline auto FindFirstAssetByType(const cooked::CookedContent& content,
  const AssetType type) -> std::optional<AssetRef>
{
  for (const auto& entry : content.Assets()) {
    if (entry.type != type) {
      continue;
    }
    return MakeAssetRef(content, entry);
  }
  return std::nullopt;
}

inline auto FindScriptAssetByDescriptorName(
  const cooked::CookedContent& content, const std::string_view descriptor_name)
  -> std::optional<AssetRef>
{
  for (const auto& asset : content.Assets()) {
    if (asset.type != AssetType::kScript) {
      continue;
    }
    if (DescriptorLayoutName(content, asset.key) != descriptor_name) {
      continue;
    }
    return MakeAssetRef(content, asset);
  }
  return std::nullopt;
}

//! Whether `content` stores a file named like the retired script-binding
//! tables: among its resource tables or its sidecars.
inline auto StoresScriptBindingsFile(const cooked::CookedContent& content)
  -> bool
{
  const auto is_bindings_file = [](const std::string& name) -> bool {
    return name.ends_with("script-bindings.table")
      || name.ends_with("script-bindings.data");
  };
  for (const auto kind : {
         data::ResourceKind::kBuffer,
         data::ResourceKind::kTexture,
         data::ResourceKind::kScript,
         data::ResourceKind::kPhysics,
       }) {
    const auto& table = content.ResourceTable(kind);
    if (is_bindings_file(table.RowsStored().file.generic_string())
      || is_bindings_file(table.DataStored().file.generic_string())) {
      return true;
    }
  }
  return std::ranges::any_of(content.Sidecars(),
    [&is_bindings_file](const cooked::CookedSidecar& sidecar) -> bool {
      return is_bindings_file(sidecar.name);
    });
}

struct SceneScriptState {
  std::vector<data::pak::scripting::ScriptSlotRecord> slots;
  std::vector<data::pak::scripting::ScriptParamRecord> params;
  uint64_t parameter_base = 0U;
};

//! The bytes of the current descriptor of `key` in the loose root `root`.
/*!
 A re-import stores a changed descriptor under a new content name, so the path
 an earlier snapshot of the root recorded can name a retired file.
*/
inline auto ReadCurrentDescriptor(const std::filesystem::path& root,
  const data::AssetKey& key) -> std::vector<std::byte>
{
  const auto content
    = cooked::CookedContent::Open(cooked::RealCookerStorage(), root);
  return ReadBytes(content.DescriptorStored(key).file);
}

inline auto ReadSceneScriptState(
  const std::filesystem::path& root, const AssetRef& asset) -> SceneScriptState
{
  const auto bytes = ReadCurrentDescriptor(root, asset.key);
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
    .source_path = scene_asset.source_path,
    .descriptor_relpath = scene_asset.descriptor_relpath,
    .descriptor_bytes = ReadBytes(cooked_root / scene_asset.descriptor_relpath),
    .references = scene_asset.references,
  };
}

//! Canonical slot ids of the fixtures; readable and deterministic.
inline constexpr std::string_view kMainSlotId
  = "00000000-0000-4000-8000-00000000000a";
inline constexpr std::string_view kAuxSlotId
  = "00000000-0000-4000-8000-00000000000b";

//! One script binding of a sidecar payload. Unset optional fields are
//! omitted from the emitted JSON.
struct SidecarBindingSpec final {
  uint32_t node_index = 0;
  std::string slot_id = std::string(kMainSlotId);
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

//! Source of a script declaring the float `speed` that MakeSpeedBinding
//! overrides; `tag` keeps otherwise identical sources distinct.
inline auto SpeedScriptSource(const std::string_view tag = {}) -> std::string
{
  return "local Script = oxygen.script {\n"
         "  params = { speed = oxygen.param.float {} },\n"
         "}\n"
         "-- "
    + std::string(tag) + "\nreturn Script\n";
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
    .slot_id = std::string(kMainSlotId),
    .script_virtual_path = std::string(script_virtual_path),
    .execution_order = 2,
    .params = nlohmann::json::array({ std::move(param) }),
  };
}

class ScriptingImportTestBase : public ::testing::Test {
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
    if (report->success && !report->surface_provenance_json.empty()) {
      source_provenance_ = SurfaceProvenance::Parse(
        report->surface_provenance_json, source_provenance_->SourceIdentity());
    }
    return *report;
  }

  [[nodiscard]] auto MakeSceneRequest(const SceneRequestSpec& spec) const
    -> ImportRequest
  {
    ImportRequest request {};
    request.source_path = spec.source_path;
    request.asset_virtual_path
      = oxygen::cooker::test::ModelVirtualPath(spec.source_path);
    request.source_mount_path
      = oxygen::cooker::test::SourceMountPath(request.source_path);
    request.cooked_root = spec.cooked_root;
    request.surface_provenance = source_provenance_;
    request.model_import = oxygen::content::import::ModelImportInputs {};
    return request;
  }

  [[nodiscard]] static auto LoadInspection(
    const std::filesystem::path& cooked_root) -> cooked::CookedContent
  {
    return cooked::CookedContent::Open(
      cooked::RealCookerStorage(), cooked_root);
  }

  //! Scene and script assets found in a cooked root, plus its inspection.
  struct SceneAndScript final {
    cooked::CookedContent inspection;
    std::optional<AssetRef> scene;
    std::optional<AssetRef> script;
  };

  //! Imports an external script, then `Tabuleiro.glb`, into `cooked_root` and
  //! finds the first scene and script assets. Missing assets are reported as
  //! failures and left empty, so callers must still check them.
  [[nodiscard]] auto CookSceneWithScript(const SceneWithScriptSpec& spec)
    -> SceneAndScript
  {
    const auto& cooked_root = spec.cooked_root;
    const auto script_source = cooked_root / "input" / spec.script_file_name;
    WriteText(script_source, spec.script_text);

    const auto model_path = ModelPath();
    EXPECT_TRUE(std::filesystem::exists(model_path));
    EXPECT_TRUE(Submit(MakeScriptRequest({
                         .source_path = script_source,
                         .cooked_root = cooked_root,
                         .storage = ScriptStorageMode::kExternal,
                       }))
        .success);
    EXPECT_TRUE(Submit(MakeSceneRequest({
                         .source_path = model_path,
                         .cooked_root = cooked_root,
                       }))
        .success);

    auto result = SceneAndScript { .inspection = LoadInspection(cooked_root) };
    result.scene = FindFirstAssetByType(result.inspection, AssetType::kScene);
    result.script = FindFirstAssetByType(result.inspection, AssetType::kScript);
    return result;
  }

private:
  std::shared_ptr<const SurfaceProvenance> source_provenance_
    = std::make_shared<const SurfaceProvenance>(Uuid::Generate());
};

} // namespace oxygen::content::import::test
