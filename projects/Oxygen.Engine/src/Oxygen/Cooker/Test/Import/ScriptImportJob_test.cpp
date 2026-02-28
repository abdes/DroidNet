//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::content::import::AssetKeyPolicy;
  using oxygen::content::import::AsyncImportService;
  using oxygen::content::import::ImportDiagnostic;
  using oxygen::content::import::ImportReport;
  using oxygen::content::import::ImportRequest;
  using oxygen::content::import::ScriptingImportKind;
  using oxygen::content::import::ScriptStorageMode;
  using oxygen::data::AssetType;
  namespace lc = oxygen::content::lc;

  auto HasDiagnosticCode(const std::vector<ImportDiagnostic>& diagnostics,
    std::string_view code) -> bool
  {
    return std::ranges::any_of(diagnostics,
      [code](const auto& diagnostic) { return diagnostic.code == code; });
  }

  auto MakeTempCookedRoot(std::string_view suffix) -> std::filesystem::path
  {
    auto root = std::filesystem::temp_directory_path() / "oxygen_script_import";
    root /= std::filesystem::path { std::string { suffix } };
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    return root;
  }

  auto WriteTextFile(const std::filesystem::path& path, std::string_view text)
    -> void
  {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }

  auto MakeScriptRequest(const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_root, const ScriptStorageMode storage,
    const bool compile_scripts = false) -> ImportRequest
  {
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = cooked_root;
    request.options.scripting.import_kind = ScriptingImportKind::kScriptAsset;
    request.options.scripting.compile_scripts = compile_scripts;
    request.options.scripting.script_storage = storage;
    return request;
  }

  auto MakeSceneRequest(const std::filesystem::path& source_path,
    const std::filesystem::path& cooked_root,
    const AssetKeyPolicy key_policy
    = AssetKeyPolicy::kDeterministicFromVirtualPath) -> ImportRequest
  {
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = cooked_root;
    request.options.asset_key_policy = key_policy;
    return request;
  }

  auto MakeSidecarRequest(const std::filesystem::path& source_path,
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

  auto SubmitAndWait(AsyncImportService& service, ImportRequest request)
    -> ImportReport
  {
    ImportReport report {};
    std::latch done(1);

    const auto submitted = service.SubmitImport(std::move(request),
      [&report, &done](const auto /*job_id*/, const ImportReport& completed) {
        report = completed;
        done.count_down();
      });
    EXPECT_TRUE(submitted.has_value());
    done.wait();

    return report;
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

  auto ReadScriptDescriptor(const std::filesystem::path& descriptor_path)
    -> data::pak::scripting::ScriptAssetDesc
  {
    using data::pak::scripting::ScriptAssetDesc;

    ScriptAssetDesc desc {};
    std::ifstream in(descriptor_path, std::ios::binary);
    in.read(reinterpret_cast<char*>(&desc), sizeof(desc));
    return desc;
  }

  auto ReadAllBytes(const std::filesystem::path& path) -> std::vector<std::byte>
  {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
      return {};
    }
    const auto size = static_cast<size_t>(in.tellg());
    std::vector<std::byte> bytes(size);
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
    return bytes;
  }

  struct AssetRef final {
    data::AssetKey key {};
    std::string virtual_path;
    std::string descriptor_relpath;
    AssetType type = AssetType::kUnknown;
  };

  auto FindFirstAssetByType(const lc::Inspection& inspection,
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
        .type = static_cast<AssetType>(entry.asset_type),
      };
    }
    return std::nullopt;
  }

  auto FindScriptAssetByDescriptorName(const lc::Inspection& inspection,
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
        .type = AssetType::kScript,
      };
    }
    return std::nullopt;
  }

  auto MakeInflightSceneContext(const std::filesystem::path& cooked_root,
    const AssetRef& scene_asset) -> ImportRequest::InflightSceneContext
  {
    return ImportRequest::InflightSceneContext {
      .scene_key = scene_asset.key,
      .virtual_path = scene_asset.virtual_path,
      .descriptor_relpath = scene_asset.descriptor_relpath,
      .descriptor_bytes
      = ReadAllBytes(cooked_root / scene_asset.descriptor_relpath),
    };
  }

  auto MakeSidecarPayload(std::string_view script_virtual_path) -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    payload += "    {\n";
    payload += "      \"node_index\": 0,\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \"";
    payload += std::string(script_virtual_path);
    payload += "\",\n";
    payload += "      \"execution_order\": 2,\n";
    payload += "      \"params\": [\n";
    payload += "        { \"key\": \"speed\", \"type\": \"float\", \"value\": "
               "3.5 }\n";
    payload += "      ]\n";
    payload += "    }\n";
    payload += "  ]\n";
    payload += "}\n";
    return payload;
  }

  struct SidecarBindingSpec final {
    uint32_t node_index = 0;
    std::string slot_id;
    std::string script_virtual_path;
    int32_t execution_order = 0;
  };

  auto MakeSidecarPayload(const std::vector<SidecarBindingSpec>& bindings)
    -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    for (size_t i = 0; i < bindings.size(); ++i) {
      const auto& binding = bindings[i];
      payload += "    {\n";
      payload += "      \"node_index\": " + std::to_string(binding.node_index)
        + ",\n";
      payload += "      \"slot_id\": \"" + binding.slot_id + "\",\n";
      payload += "      \"script_virtual_path\": \""
        + binding.script_virtual_path + "\",\n";
      payload += "      \"execution_order\": "
        + std::to_string(binding.execution_order) + "\n";
      payload += "    }";
      if (i + 1U < bindings.size()) {
        payload += ",";
      }
      payload += "\n";
    }
    payload += "  ]\n";
    payload += "}\n";
    return payload;
  }

  auto MakeSidecarPayloadWithNodeIndex(std::string_view script_virtual_path,
    const uint32_t node_index) -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    payload += "    {\n";
    payload += "      \"node_index\": " + std::to_string(node_index) + ",\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \"";
    payload += std::string(script_virtual_path);
    payload += "\"\n";
    payload += "    }\n";
    payload += "  ]\n";
    payload += "}\n";
    return payload;
  }

  auto MakeSidecarPayloadWithDuplicateSlot(std::string_view script_virtual_path)
    -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    payload += "    {\n";
    payload += "      \"node_index\": 0,\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \"";
    payload += std::string(script_virtual_path);
    payload += "\"\n";
    payload += "    },\n";
    payload += "    {\n";
    payload += "      \"node_index\": 0,\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \"";
    payload += std::string(script_virtual_path);
    payload += "\"\n";
    payload += "    }\n";
    payload += "  ]\n";
    payload += "}\n";
    return payload;
  }

  auto FindFileRelPathByKind(const lc::Inspection& inspection,
    const data::loose_cooked::FileKind kind) -> std::optional<std::string>
  {
    for (const auto& file : inspection.Files()) {
      if (file.kind == kind) {
        return file.relpath;
      }
    }
    return std::nullopt;
  }

  template <typename T>
  auto ReadPackedRecords(const std::filesystem::path& path) -> std::vector<T>
  {
    static_assert(std::is_trivially_copyable_v<T>);
    auto bytes = ReadAllBytes(path);
    if (bytes.empty() || (bytes.size() % sizeof(T)) != 0U) {
      return {};
    }

    auto records = std::vector<T> {};
    records.resize(bytes.size() / sizeof(T));
    std::memcpy(records.data(), bytes.data(), bytes.size());
    return records;
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
      return std::filesystem::path(__FILE__).parent_path() / "Models"
        / "Tabuleiro.glb";
    }

    [[nodiscard]] auto Submit(ImportRequest request) -> ImportReport
    {
      return SubmitAndWait(Service(), std::move(request));
    }

    [[nodiscard]] static auto LoadInspection(
      const std::filesystem::path& cooked_root) -> lc::Inspection
    {
      auto inspection = lc::Inspection {};
      inspection.LoadFromFile(cooked_root / "container.index.bin");
      return inspection;
    }
  };

  class ScriptAssetImportTest : public ScriptingImportTestBase { };

  class ScriptingSidecarImportTest : public ScriptingImportTestBase { };

  NOLINT_TEST_F(
    ScriptAssetImportTest, EmbeddedScriptImportWritesDescriptorAndScriptFiles)
  {
    using data::loose_cooked::FileKind;
    using data::pak::core::kNoResourceIndex;
    using data::pak::scripting::ScriptAssetFlags;

    const auto cooked_root
      = MakeTempCookedRoot("script_import_embedded_success");
    const auto source_path = cooked_root / "input" / "hello.luau";
    WriteTextFile(source_path, "return 7");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "hello.oscript";
    EXPECT_TRUE(std::filesystem::exists(descriptor_path));

    const auto inspection = LoadInspection(cooked_root);

    const auto files = inspection.Files();
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsTable; }));
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsData; }));

    const auto assets = inspection.Assets();
    ASSERT_EQ(assets.size(), 1U);
    EXPECT_EQ(
      static_cast<AssetType>(assets.front().asset_type), AssetType::kScript);

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_NE(desc.source_resource_index, kNoResourceIndex);
    EXPECT_EQ(desc.bytecode_resource_index, kNoResourceIndex);
    EXPECT_EQ(desc.flags, ScriptAssetFlags::kNone);
  }

  NOLINT_TEST_F(ScriptAssetImportTest,
    ExternalScriptImportStoresExternalPathAndNoScriptFiles)
  {
    using data::loose_cooked::FileKind;
    using data::pak::core::kNoResourceIndex;
    using data::pak::scripting::ScriptAssetFlags;

    const auto cooked_root
      = MakeTempCookedRoot("script_import_external_success");
    const auto source_path = cooked_root / "input" / "external.lua";
    WriteTextFile(source_path, "print('hello')");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "external.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));

    const auto inspection = LoadInspection(cooked_root);

    const auto files = inspection.Files();
    EXPECT_FALSE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsTable; }));
    EXPECT_FALSE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsData; }));

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_EQ(desc.source_resource_index, kNoResourceIndex);
    EXPECT_EQ(desc.bytecode_resource_index, kNoResourceIndex);
    EXPECT_EQ((desc.flags & ScriptAssetFlags::kAllowExternalSource),
      ScriptAssetFlags::kAllowExternalSource);

    const auto expected_external_path = source_path.filename().generic_string();
    const auto actual_external_path
      = std::string_view { desc.external_source_path };
    EXPECT_EQ(actual_external_path, expected_external_path);
  }

  NOLINT_TEST_F(
    ScriptAssetImportTest, CompileEnabledFailsWhenCompilerUnavailable)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_import_compile_unavailable");
    const auto source_path = cooked_root / "input" / "compile_me.luau";
    WriteTextFile(source_path, "return 99");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.asset.compiler_unavailable"));
  }

  NOLINT_TEST_F(ScriptAssetImportTest, CompileWithExternalStorageIsRejected)
  {
    const auto cooked_root = MakeTempCookedRoot("script_import_invalid_combo");
    const auto source_path = cooked_root / "input" / "combo.luau";
    WriteTextFile(source_path, "return 1");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.request.invalid_option_combo"));
  }

  NOLINT_TEST_F(
    ScriptAssetImportTest, ReimportOverwritesScriptDescriptorIdentity)
  {
    const auto cooked_root = MakeTempCookedRoot("script_import_reimport");
    const auto source_path = cooked_root / "input" / "reload.luau";
    WriteTextFile(source_path, "return 'first'");

    const auto report_first = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, false));
    ASSERT_TRUE(report_first.success);

    const auto first_inspection = LoadInspection(cooked_root);
    const auto first_assets = first_inspection.Assets();
    ASSERT_EQ(first_assets.size(), 1U);
    ASSERT_EQ(static_cast<AssetType>(first_assets.front().asset_type),
      AssetType::kScript);
    const auto first_key = first_assets.front().key;

    WriteTextFile(source_path, "return 'second'");

    const auto report_second = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, false));
    ASSERT_TRUE(report_second.success);

    const auto second_inspection = LoadInspection(cooked_root);
    const auto second_assets = second_inspection.Assets();
    ASSERT_EQ(second_assets.size(), 1U);
    ASSERT_EQ(static_cast<AssetType>(second_assets.front().asset_type),
      AssetType::kScript);
    EXPECT_EQ(second_assets.front().key, first_key);
  }

  NOLINT_TEST_F(
    ScriptAssetImportTest, EmbeddedScriptImportIsDeterministicForSameInputs)
  {
    const auto cooked_root_a
      = MakeTempCookedRoot("script_import_determinism_a");
    const auto cooked_root_b
      = MakeTempCookedRoot("script_import_determinism_b");
    const auto source_a = cooked_root_a / "input" / "stable.luau";
    const auto source_b = cooked_root_b / "input" / "stable.luau";
    constexpr auto kSourceText = std::string_view { "return 'stable'" };
    WriteTextFile(source_a, kSourceText);
    WriteTextFile(source_b, kSourceText);

    const auto report_a = Submit(
      MakeScriptRequest(source_a, cooked_root_a, ScriptStorageMode::kEmbedded));
    const auto report_b = Submit(
      MakeScriptRequest(source_b, cooked_root_b, ScriptStorageMode::kEmbedded));
    ASSERT_TRUE(report_a.success);
    ASSERT_TRUE(report_b.success);

    const auto inspection_a = LoadInspection(cooked_root_a);
    const auto inspection_b = LoadInspection(cooked_root_b);

    const auto assets_a = inspection_a.Assets();
    const auto assets_b = inspection_b.Assets();
    ASSERT_EQ(assets_a.size(), 1U);
    ASSERT_EQ(assets_b.size(), 1U);
    ASSERT_EQ(
      static_cast<AssetType>(assets_a.front().asset_type), AssetType::kScript);
    ASSERT_EQ(
      static_cast<AssetType>(assets_b.front().asset_type), AssetType::kScript);
    EXPECT_EQ(assets_a.front().key, assets_b.front().key);

    const auto descriptor_a
      = ReadAllBytes(cooked_root_a / "Scripts" / "stable.oscript");
    const auto descriptor_b
      = ReadAllBytes(cooked_root_b / "Scripts" / "stable.oscript");
    EXPECT_EQ(descriptor_a, descriptor_b);
  }

  NOLINT_TEST_F(
    ScriptAssetImportTest, MissingSourceFileFailsWithAssetReadDiagnostic)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_import_missing_source_file");
    const auto missing_source = cooked_root / "input" / "does_not_exist.luau";

    const auto report = Submit(MakeScriptRequest(
      missing_source, cooked_root, ScriptStorageMode::kExternal, false));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.asset.source_read_failed"));
  }

  //! Sidecar success path uses external script assets so sidecar owns
  //! scripts.table/scripts.data slot+param layout in this cooked root.
  NOLINT_TEST_F(
    ScriptingSidecarImportTest, ScriptingSidecarBindsScriptToSceneNode)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const auto cooked_root = MakeTempCookedRoot("script_sidecar_binds_scene");
    const auto script_source = cooked_root / "input" / "main_logic.luau";
    WriteTextFile(script_source, "return 123");

    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_report = Submit(MakeScriptRequest(
      script_source, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(script_report.success);

    const auto scene_report = Submit(MakeSceneRequest(model_path, cooked_root));
    ASSERT_TRUE(scene_report.success);

    const auto inspection_before_sidecar = LoadInspection(cooked_root);

    const auto scene_asset
      = FindFirstAssetByType(inspection_before_sidecar, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());
    const auto script_asset
      = FindFirstAssetByType(inspection_before_sidecar, AssetType::kScript);
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    const auto sidecar_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(sidecar_report.success);

    const auto inspection_after_sidecar = LoadInspection(cooked_root);
    const auto files = inspection_after_sidecar.Files();
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsTable; }));
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsData; }));

    const auto scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components[0].node_index, 0U);
    EXPECT_EQ(components[0].slot_count, 1U);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, ScriptingSidecarRejectsMissingTargetScenePath)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_missing_target_scene");
    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteTextFile(sidecar_source, "{ \"bindings\": [] }");

    auto request = MakeSidecarRequest(sidecar_source, cooked_root, "");

    const auto report = Submit(std::move(request));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.request.target_scene_virtual_path_missing"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ScriptingSidecarRejectsInvalidCanonicalTargetScenePath)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_invalid_target_scene_path");
    const auto script_source = cooked_root / "input" / "seed_script.luau";
    WriteTextFile(script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto sidecar_source = cooked_root / "input" / "invalid_scene.json";
    WriteTextFile(sidecar_source, "{ \"bindings\": [] }");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, "Scenes/not_canonical.oscene"));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.target_scene_virtual_path_invalid"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, SidecarPathOnlySuccessWithInflightSceneContext)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const auto cooked_root = MakeTempCookedRoot("script_sidecar_inflight_ok");
    const auto inflight_scene_root
      = MakeTempCookedRoot("script_sidecar_inflight_scene_source");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "runtime.luau";
    WriteTextFile(script_source, "return 7");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root)).success);

    const auto inflight_inspection = LoadInspection(inflight_scene_root);
    const auto inflight_scene
      = FindFirstAssetByType(inflight_inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(LoadInspection(cooked_root), AssetType::kScript);
    ASSERT_TRUE(inflight_scene.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, inflight_scene->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));

    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto patched_scene
      = FindFirstAssetByType(inspection_after, AssetType::kScene);
    ASSERT_TRUE(patched_scene.has_value());
    EXPECT_EQ(patched_scene->key, inflight_scene->key);

    const auto files = inspection_after.Files();
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsTable; }));
    EXPECT_TRUE(std::ranges::any_of(files,
      [](const auto& file) { return file.kind == FileKind::kScriptsData; }));

    const auto scene_bytes
      = ReadAllBytes(cooked_root / patched_scene->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(patched_scene->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components[0].node_index, 0U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    InflightDuplicatePathMatchRejectedDeterministically)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_inflight_duplicate_match");
    const auto inflight_scene_root_a
      = MakeTempCookedRoot("script_sidecar_inflight_scene_a");
    const auto inflight_scene_root_b
      = MakeTempCookedRoot("script_sidecar_inflight_scene_b");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "seed.luau";
    WriteTextFile(script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, inflight_scene_root_a,
                         AssetKeyPolicy::kDeterministicFromVirtualPath))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, inflight_scene_root_b,
                         AssetKeyPolicy::kRandom))
        .success);

    const auto inspection_a = LoadInspection(inflight_scene_root_a);
    const auto inspection_b = LoadInspection(inflight_scene_root_b);
    const auto scene_a = FindFirstAssetByType(inspection_a, AssetType::kScene);
    const auto scene_b = FindFirstAssetByType(inspection_b, AssetType::kScene);
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());
    ASSERT_EQ(scene_a->virtual_path, scene_b->virtual_path);
    ASSERT_NE(scene_a->key, scene_b->key);

    const auto sidecar_source = cooked_root / "input" / "duplicate.json";
    WriteTextFile(sidecar_source, "{ \"bindings\": [] }");

    auto request
      = MakeSidecarRequest(sidecar_source, cooked_root, scene_a->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root_a, *scene_a));
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root_b, *scene_b));

    const auto report = Submit(std::move(request));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.inflight_target_scene_ambiguous"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ResolverDelegationUsesLastMountedCookedContextPrecedence)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_resolver_precedence_target");
    const auto context_root_a
      = MakeTempCookedRoot("script_sidecar_resolver_precedence_a");
    const auto context_root_b
      = MakeTempCookedRoot("script_sidecar_resolver_precedence_b");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "resolver_ref.luau";
    WriteTextFile(script_source, "return 5");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root_a,
                         AssetKeyPolicy::kDeterministicFromVirtualPath))
        .success);
    ASSERT_TRUE(Submit(
      MakeSceneRequest(model_path, context_root_b, AssetKeyPolicy::kRandom))
        .success);

    const auto inspection_a = LoadInspection(context_root_a);
    const auto inspection_b = LoadInspection(context_root_b);
    const auto scene_a = FindFirstAssetByType(inspection_a, AssetType::kScene);
    const auto scene_b = FindFirstAssetByType(inspection_b, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(LoadInspection(cooked_root), AssetType::kScript);
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());
    ASSERT_TRUE(script_asset.has_value());
    ASSERT_EQ(scene_a->virtual_path, scene_b->virtual_path);
    ASSERT_NE(scene_a->key, scene_b->key);

    const auto sidecar_source = cooked_root / "input" / "precedence.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    auto request
      = MakeSidecarRequest(sidecar_source, cooked_root, scene_a->virtual_path);
    request.cooked_context_roots.push_back(context_root_a);
    request.cooked_context_roots.push_back(context_root_b);

    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto resolved_scene
      = FindFirstAssetByType(inspection_after, AssetType::kScene);
    ASSERT_TRUE(resolved_scene.has_value());
    EXPECT_EQ(resolved_scene->key, scene_b->key);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ContextParityInflightAndStandaloneProduceEquivalentSerializedState)
  {
    using data::loose_cooked::FileKind;

    const auto concurrent_root = MakeTempCookedRoot("script_sidecar_parity_a");
    const auto standalone_root = MakeTempCookedRoot("script_sidecar_parity_b");
    const auto inflight_scene_root
      = MakeTempCookedRoot("script_sidecar_parity_inflight_scene");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source_a = concurrent_root / "input" / "parity.luau";
    const auto script_source_b = standalone_root / "input" / "parity.luau";
    constexpr auto kScriptText = std::string_view { "return 42" };
    WriteTextFile(script_source_a, kScriptText);
    WriteTextFile(script_source_b, kScriptText);

    ASSERT_TRUE(Submit(MakeScriptRequest(script_source_a, concurrent_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source_b, standalone_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, standalone_root)).success);

    const auto inflight_inspection = LoadInspection(inflight_scene_root);
    const auto inflight_scene
      = FindFirstAssetByType(inflight_inspection, AssetType::kScene);
    ASSERT_TRUE(inflight_scene.has_value());

    const auto script_a = FindFirstAssetByType(
      LoadInspection(concurrent_root), AssetType::kScript);
    const auto script_b = FindFirstAssetByType(
      LoadInspection(standalone_root), AssetType::kScript);
    ASSERT_TRUE(script_a.has_value());
    ASSERT_TRUE(script_b.has_value());

    const auto sidecar_source_a
      = concurrent_root / "input" / "parity_sidecar.json";
    const auto sidecar_source_b
      = standalone_root / "input" / "parity_sidecar.json";
    WriteTextFile(sidecar_source_a, MakeSidecarPayload(script_a->virtual_path));
    WriteTextFile(sidecar_source_b, MakeSidecarPayload(script_b->virtual_path));

    auto concurrent_request = MakeSidecarRequest(
      sidecar_source_a, concurrent_root, inflight_scene->virtual_path);
    concurrent_request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));
    ASSERT_TRUE(Submit(std::move(concurrent_request)).success);

    const auto standalone_scene_before = FindFirstAssetByType(
      LoadInspection(standalone_root), AssetType::kScene);
    ASSERT_TRUE(standalone_scene_before.has_value());
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source_b, standalone_root,
                         standalone_scene_before->virtual_path))
        .success);

    const auto concurrent_inspection = LoadInspection(concurrent_root);
    const auto standalone_inspection = LoadInspection(standalone_root);

    const auto concurrent_scene
      = FindFirstAssetByType(concurrent_inspection, AssetType::kScene);
    const auto standalone_scene
      = FindFirstAssetByType(standalone_inspection, AssetType::kScene);
    ASSERT_TRUE(concurrent_scene.has_value());
    ASSERT_TRUE(standalone_scene.has_value());

    const auto concurrent_table_relpath
      = FindFileRelPathByKind(concurrent_inspection, FileKind::kScriptsTable);
    const auto standalone_table_relpath
      = FindFileRelPathByKind(standalone_inspection, FileKind::kScriptsTable);
    const auto concurrent_data_relpath
      = FindFileRelPathByKind(concurrent_inspection, FileKind::kScriptsData);
    const auto standalone_data_relpath
      = FindFileRelPathByKind(standalone_inspection, FileKind::kScriptsData);
    ASSERT_TRUE(concurrent_table_relpath.has_value());
    ASSERT_TRUE(standalone_table_relpath.has_value());
    ASSERT_TRUE(concurrent_data_relpath.has_value());
    ASSERT_TRUE(standalone_data_relpath.has_value());

    const auto concurrent_scene_bytes
      = ReadAllBytes(concurrent_root / concurrent_scene->descriptor_relpath);
    const auto standalone_scene_bytes
      = ReadAllBytes(standalone_root / standalone_scene->descriptor_relpath);
    const auto concurrent_table_bytes
      = ReadAllBytes(concurrent_root / *concurrent_table_relpath);
    const auto standalone_table_bytes
      = ReadAllBytes(standalone_root / *standalone_table_relpath);
    const auto concurrent_data_bytes
      = ReadAllBytes(concurrent_root / *concurrent_data_relpath);
    const auto standalone_data_bytes
      = ReadAllBytes(standalone_root / *standalone_data_relpath);

    EXPECT_EQ(concurrent_scene_bytes, standalone_scene_bytes);
    EXPECT_EQ(concurrent_table_bytes, standalone_table_bytes);
    EXPECT_EQ(concurrent_data_bytes, standalone_data_bytes);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, NodeBindingOverwriteAndAdditiveUpdateBehavior)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_overwrite_additive");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "logic_a.luau";
    const auto script_b_source = cooked_root / "input" / "logic_b.luau";
    const auto script_c_source = cooked_root / "input" / "logic_c.luau";
    WriteTextFile(script_a_source, "return 'A'");
    WriteTextFile(script_b_source, "return 'B'");
    WriteTextFile(script_c_source, "return 'C'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_a_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_b_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_c_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    const auto script_a
      = FindScriptAssetByDescriptorName(inspection_before, "logic_a.oscript");
    const auto script_b
      = FindScriptAssetByDescriptorName(inspection_before, "logic_b.oscript");
    const auto script_c
      = FindScriptAssetByDescriptorName(inspection_before, "logic_c.oscript");
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_a.has_value());
    ASSERT_TRUE(script_b.has_value());
    ASSERT_TRUE(script_c.has_value());

    const auto base_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    auto base_scene = data::SceneAsset(scene_asset->key, base_scene_bytes);
    ASSERT_GT(base_scene.GetNodes().size(), 1U);

    const auto sidecar_source
      = cooked_root / "input" / "overwrite_additive.sidescript.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec { .node_index = 0,
          .slot_id = "main",
          .script_virtual_path = script_a->virtual_path,
          .execution_order = 1 },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    WriteTextFile(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec { .node_index = 0,
          .slot_id = "main",
          .script_virtual_path = script_b->virtual_path,
          .execution_order = 2 },
        SidecarBindingSpec { .node_index = 1,
          .slot_id = "aux",
          .script_virtual_path = script_c->virtual_path,
          .execution_order = 3 },
      }));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptsTable);
    ASSERT_TRUE(table_relpath.has_value());
    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);

    const auto patched_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    auto patched_scene
      = data::SceneAsset(scene_asset->key, patched_scene_bytes);
    const auto components
      = patched_scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 2U);

    const auto node0_component
      = std::find_if(components.begin(), components.end(),
        [](const auto& component) { return component.node_index == 0U; });
    const auto node1_component
      = std::find_if(components.begin(), components.end(),
        [](const auto& component) { return component.node_index == 1U; });
    ASSERT_NE(node0_component, components.end());
    ASSERT_NE(node1_component, components.end());
    ASSERT_EQ(node0_component->slot_count, 1U);
    ASSERT_EQ(node1_component->slot_count, 1U);

    ASSERT_LT(node0_component->slot_start_index, slots.size());
    ASSERT_LT(node1_component->slot_start_index, slots.size());
    const auto& node0_slot = slots[node0_component->slot_start_index];
    const auto& node1_slot = slots[node1_component->slot_start_index];
    EXPECT_EQ(node0_slot.script_asset_key, script_b->key);
    EXPECT_EQ(node1_slot.script_asset_key, script_c->key);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ScriptingSidecarRejectsUnresolvedScriptReference)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_unresolved_script_reference");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto scene_report = Submit(MakeSceneRequest(model_path, cooked_root));
    ASSERT_TRUE(scene_report.success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload("/Scripts/does_not_exist.oscript"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_ref_unresolved"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, SidecarFailureIsAtomicAndDoesNotMutateOutputs)
  {
    using data::loose_cooked::FileKind;

    const auto cooked_root = MakeTempCookedRoot("script_sidecar_atomicity");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "stable.luau";
    WriteTextFile(script_source, "return 3");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "atomic.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto inspection_after_success = LoadInspection(cooked_root);
    const auto table_relpath = FindFileRelPathByKind(
      inspection_after_success, FileKind::kScriptsTable);
    const auto data_relpath
      = FindFileRelPathByKind(inspection_after_success, FileKind::kScriptsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto scene_before_failure
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto table_before_failure
      = ReadAllBytes(cooked_root / *table_relpath);
    const auto data_before_failure = ReadAllBytes(cooked_root / *data_relpath);

    WriteTextFile(
      sidecar_source, MakeSidecarPayload("/Scripts/does_not_exist.oscript"));
    const auto failure_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(failure_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      failure_report.diagnostics, "script.sidecar.script_ref_unresolved"));

    const auto scene_after_failure
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto table_after_failure = ReadAllBytes(cooked_root / *table_relpath);
    const auto data_after_failure = ReadAllBytes(cooked_root / *data_relpath);

    EXPECT_EQ(scene_before_failure, scene_after_failure);
    EXPECT_EQ(table_before_failure, table_after_failure);
    EXPECT_EQ(data_before_failure, data_after_failure);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, MissingSourceFileFailsWithSidecarReadDiagnostic)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_missing_source_file");
    const auto missing_source = cooked_root / "input" / "missing_sidecar.json";

    const auto report = Submit(MakeSidecarRequest(
      missing_source, cooked_root, "/Scenes/any_scene.oscene"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.source_read_failed"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, RejectsMalformedJson)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_malformed_json");
    const auto sidecar_source = cooked_root / "input" / "bad_sidecar.json";
    WriteTextFile(sidecar_source, "{ this is : not valid json ]");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, "/Scenes/any_scene.oscene"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.parse_failed"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, RejectsMissingTargetSceneAssetInCookedRoot)
  {
    const auto cooked_root = MakeTempCookedRoot("script_sidecar_missing_scene");
    const auto script_source = cooked_root / "input" / "seed_script.luau";
    WriteTextFile(script_source, "return 0");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto sidecar_source = cooked_root / "input" / "scene.sidescript.json";
    WriteTextFile(sidecar_source, "{ \"bindings\": [] }");

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, "/Scenes/missing_scene.oscene"));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.target_scene_missing"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, RejectsDuplicateBindingIdentity)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_duplicate_binding_identity");
    const auto script_source = cooked_root / "input" / "main_logic.luau";
    WriteTextFile(script_source, "return 123");

    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "dup.sidescript.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithDuplicateSlot(script_asset->virtual_path));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.duplicate_slot_conflict"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, RejectsOutOfBoundsNodeReference)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_out_of_bounds_node");
    const auto script_source = cooked_root / "input" / "main_logic.luau";
    WriteTextFile(script_source, "return 123");

    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "oob.sidescript.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithNodeIndex(script_asset->virtual_path, 99999U));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.node_ref_unresolved"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, ReimportRebindsSlotToNewScriptKey)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root = MakeTempCookedRoot("script_sidecar_rebind");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_a_source = cooked_root / "input" / "script_a.luau";
    const auto script_b_source = cooked_root / "input" / "script_b.luau";
    WriteTextFile(script_a_source, "return 'A'");
    WriteTextFile(script_b_source, "return 'B'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_a_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_b_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto before_inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(before_inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    auto script_a_asset = std::optional<AssetRef> {};
    auto script_b_asset = std::optional<AssetRef> {};
    for (const auto& asset : before_inspection.Assets()) {
      if (static_cast<AssetType>(asset.asset_type) == AssetType::kScript) {
        const auto descriptor_name
          = std::filesystem::path(asset.descriptor_relpath).filename().string();
        if (descriptor_name == "script_a.oscript") {
          script_a_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath = asset.descriptor_relpath,
            .type = AssetType::kScript,
          };
        } else if (descriptor_name == "script_b.oscript") {
          script_b_asset = AssetRef {
            .key = asset.key,
            .virtual_path = asset.virtual_path,
            .descriptor_relpath = asset.descriptor_relpath,
            .type = AssetType::kScript,
          };
        }
      }
    }
    ASSERT_TRUE(script_a_asset.has_value());
    ASSERT_TRUE(script_b_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "rebind.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_a_asset->virtual_path));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_b_asset->virtual_path));
    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(second_report.success);

    const auto after_inspection = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(after_inspection, FileKind::kScriptsTable);
    ASSERT_TRUE(table_relpath.has_value());

    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    ASSERT_EQ(slots.size(), 1U);
    EXPECT_EQ(slots[0].script_asset_key, script_b_asset->key);

    const auto scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components[0].slot_count, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, ReimportWithSamePayloadIsIdempotent)
  {
    using data::loose_cooked::FileKind;

    const auto cooked_root = MakeTempCookedRoot("script_sidecar_idempotent");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "idempotent_script.luau";
    WriteTextFile(script_source, "return 'stable'");

    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "idempotent.sidescript.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto first_inspection = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(first_inspection, FileKind::kScriptsTable);
    const auto data_relpath
      = FindFileRelPathByKind(first_inspection, FileKind::kScriptsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto first_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto first_table_bytes = ReadAllBytes(cooked_root / *table_relpath);
    const auto first_data_bytes = ReadAllBytes(cooked_root / *data_relpath);

    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto second_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto second_table_bytes = ReadAllBytes(cooked_root / *table_relpath);
    const auto second_data_bytes = ReadAllBytes(cooked_root / *data_relpath);

    EXPECT_EQ(first_scene_bytes, second_scene_bytes);
    EXPECT_EQ(first_table_bytes, second_table_bytes);
    EXPECT_EQ(first_data_bytes, second_data_bytes);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, RejectsInvalidScriptVirtualPathFormat)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_invalid_script_virtual_path");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "invalid_path.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload("Scripts/not_absolute_or_virtual_path.oscript"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_virtual_path_invalid"));
  }

} // namespace

} // namespace oxygen::content::import::test
