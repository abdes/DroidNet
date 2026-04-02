//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <latch>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Data/SceneAsset.h>

namespace oxygen::content::import::test {

namespace {

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

  auto CountSeverity(const std::vector<ImportDiagnostic>& diagnostics,
    const ImportSeverity severity) -> uint32_t
  {
    return static_cast<uint32_t>(std::ranges::count_if(
      diagnostics, [severity](const ImportDiagnostic& diagnostic) {
        return diagnostic.severity == severity;
      }));
  }

  auto HasAnyObjectPath(const std::vector<ImportDiagnostic>& diagnostics)
    -> bool
  {
    return std::ranges::any_of(
      diagnostics, [](const ImportDiagnostic& diagnostic) {
        return !diagnostic.object_path.empty();
      });
  }

  auto ExpectDiagnosticFieldsComplete(
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

  auto ExpectPackagingSummaryMatchesDiagnostics(const ImportReport& report)
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
    const std::filesystem::path& cooked_root) -> ImportRequest
  {
    ImportRequest request {};
    request.source_path = source_path;
    request.cooked_root = cooked_root;
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

  auto MakeInlineSidecarRequest(const std::filesystem::path& cooked_root,
    std::string scene_virtual_path, std::string inline_bindings_json)
    -> ImportRequest
  {
    ImportRequest request {};
    request.cooked_root = cooked_root;
    request.options.scripting.import_kind
      = ScriptingImportKind::kScriptingSidecar;
    request.options.scripting.target_scene_virtual_path
      = std::move(scene_virtual_path);
    request.options.scripting.inline_bindings_json
      = std::move(inline_bindings_json);
    return request;
  }

  auto CanonicalScriptVirtualPath(std::string_view script_name) -> std::string
  {
    return ImportRequest {}.loose_cooked_layout.ScriptVirtualPath(script_name);
  }

  auto CanonicalSceneVirtualPath(std::string_view scene_name) -> std::string
  {
    return ImportRequest {}.loose_cooked_layout.SceneVirtualPath(scene_name);
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

  struct CallbackCapture final {
    ImportReport report {};
    std::vector<ImportPhase> phases;
    uint32_t completion_calls = 0;
  };

  auto SubmitAndCaptureCallbacks(AsyncImportService& service,
    ImportRequest request) -> std::optional<CallbackCapture>
  {
    auto capture = CallbackCapture {};
    auto callback_mutex = std::mutex {};
    std::latch done(1);

    const auto submitted = service.SubmitImport(
      std::move(request),
      [&capture, &callback_mutex, &done](
        const auto /*job_id*/, const ImportReport& completed) {
        std::scoped_lock lock(callback_mutex);
        ++capture.completion_calls;
        capture.report = completed;
        done.count_down();
      },
      [&capture, &callback_mutex](const ProgressEvent& progress) {
        std::scoped_lock lock(callback_mutex);
        capture.phases.push_back(progress.header.phase);
      });
    if (!submitted.has_value()) {
      return std::nullopt;
    }

    done.wait();
    return capture;
  }

  auto ContainsPhase(
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

  auto MakeSidecarPayloadWithFloatParam(
    std::string_view script_virtual_path, const float speed) -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    payload += "    {\n";
    payload += "      \"node_index\": 0,\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \""
      + std::string(script_virtual_path) + "\",\n";
    payload += "      \"execution_order\": 2,\n";
    payload += "      \"params\": [\n";
    payload += "        { \"key\": \"speed\", \"type\": \"float\", \"value\": "
      + std::to_string(speed) + " }\n";
    payload += "      ]\n";
    payload += "    }\n";
    payload += "  ]\n";
    payload += "}\n";
    return payload;
  }

  auto MakeSidecarPayloadWithParams(std::string_view script_virtual_path,
    std::string_view params_json) -> std::string
  {
    auto payload = std::string {};
    payload += "{\n";
    payload += "  \"bindings\": [\n";
    payload += "    {\n";
    payload += "      \"node_index\": 0,\n";
    payload += "      \"slot_id\": \"main\",\n";
    payload += "      \"script_virtual_path\": \""
      + std::string(script_virtual_path) + "\",\n";
    payload += "      \"execution_order\": 2,\n";
    payload += "      \"params\": ";
    payload += std::string(params_json);
    payload += "\n";
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
    ScriptAssetImportTest, SubmitRejectionReturnsNulloptForInvalidAndShutdown)
  {
    const auto cooked_root = MakeTempCookedRoot("script_submit_rejection");

    auto invalid_callback_invoked = std::atomic<bool> { false };
    auto invalid_request = ImportRequest {};
    invalid_request.source_path = cooked_root / "input" / "unsupported.abc";
    invalid_request.cooked_root = cooked_root;

    const auto invalid_submit
      = Service().SubmitImport(std::move(invalid_request),
        [&invalid_callback_invoked](
          const auto /*job_id*/, const ImportReport& /*report*/) {
          invalid_callback_invoked.store(true, std::memory_order_release);
        });
    EXPECT_FALSE(invalid_submit.has_value());
    EXPECT_FALSE(invalid_callback_invoked.load(std::memory_order_acquire));

    Service().RequestShutdown();

    auto shutdown_callback_invoked = std::atomic<bool> { false };
    const auto shutdown_submit = Service().SubmitImport(
      MakeScriptRequest(cooked_root / "input" / "shutdown.luau", cooked_root,
        ScriptStorageMode::kExternal, false),
      [&shutdown_callback_invoked](
        const auto /*job_id*/, const ImportReport& /*report*/) {
        shutdown_callback_invoked.store(true, std::memory_order_release);
      });
    EXPECT_FALSE(shutdown_submit.has_value());
    EXPECT_FALSE(shutdown_callback_invoked.load(std::memory_order_acquire));
  }

  NOLINT_TEST_F(ScriptAssetImportTest,
    ScriptAssetCallbacksProvideProgressAndSingleCompletion)
  {
    constexpr auto kScriptSource = std::string_view { "return 17" };
    const auto cooked_root = MakeTempCookedRoot("script_callbacks_asset");
    const auto source_path = cooked_root / "input" / "callback_asset.luau";
    WriteTextFile(source_path, kScriptSource);

    const auto capture = SubmitAndCaptureCallbacks(Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(capture.has_value());
    EXPECT_EQ(capture->completion_calls, 1U);
    EXPECT_TRUE(capture->report.success);
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kLoading));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kWorking));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kComplete));
  }

  NOLINT_TEST_F(ScriptAssetImportTest, ScriptAssetReportCountersArePopulated)
  {
    constexpr auto kScriptSource = std::string_view { "return 5" };
    const auto cooked_root = MakeTempCookedRoot("script_report_counters_asset");
    const auto source_path = cooked_root / "input" / "counter_asset.luau";
    WriteTextFile(source_path, kScriptSource);

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 1U);
    EXPECT_EQ(report.scripting_components_written, 0U);
    EXPECT_EQ(report.script_slots_written, 0U);
    EXPECT_EQ(report.script_params_written, 0U);
  }

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
    const auto source_path = cooked_root.parent_path() / "scripts"
      / "script_import_external_success" / "external.lua";
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

    const auto expected_external_path
      = std::filesystem::relative(source_path, cooked_root.parent_path())
          .lexically_normal()
          .generic_string();
    const auto actual_external_path
      = std::string_view { desc.external_source_path };
    EXPECT_EQ(actual_external_path, expected_external_path);
  }

  NOLINT_TEST_F(ScriptAssetImportTest,
    ExternalScriptImportNormalizesRelativeSourcePathAgainstContentRoot)
  {
    using data::pak::scripting::ScriptAssetFlags;

    auto test_root = std::filesystem::current_path()
      / "tmp_script_import_relative_external_path";
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    const auto cooked_root = test_root / "Content" / ".cooked";
    const auto source_path_absolute = test_root / "Content" / "scenes"
      / "physics_domains" / "relative_source.lua";
    const auto source_path_relative
      = (std::filesystem::path { "tmp_script_import_relative_external_path" }
        / "Content" / "scenes" / "physics_domains" / "relative_source.lua")
          .lexically_normal();
    WriteTextFile(source_path_absolute, "print('relative')");

    const auto report = Submit(MakeScriptRequest(
      source_path_relative, cooked_root, ScriptStorageMode::kExternal, false));
    EXPECT_TRUE(report.success);

    const auto descriptor_path
      = cooked_root / "Scripts" / "relative_source.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_EQ((desc.flags & ScriptAssetFlags::kAllowExternalSource),
      ScriptAssetFlags::kAllowExternalSource);
    EXPECT_EQ(std::string_view { desc.external_source_path },
      "scenes/physics_domains/relative_source.lua");

    std::filesystem::remove_all(test_root, ec);
  }

  NOLINT_TEST_F(ScriptAssetImportTest,
    ExternalScriptImportNormalizesRelativeCookedRootAgainstContentRoot)
  {
    using data::pak::scripting::ScriptAssetFlags;

    auto test_root = std::filesystem::current_path()
      / "tmp_script_import_relative_cooked_root";
    std::error_code ec;
    std::filesystem::remove_all(test_root, ec);

    const auto cooked_root_absolute = test_root / "Content" / ".cooked";
    const auto cooked_root_relative
      = (std::filesystem::path { "tmp_script_import_relative_cooked_root" }
        / "Content" / ".cooked")
          .lexically_normal();
    const auto source_path_absolute = test_root / "Content" / "scenes"
      / "multi-script" / "relative_cooked_root.lua";
    WriteTextFile(source_path_absolute, "print('relative cooked root')");

    const auto report = Submit(MakeScriptRequest(source_path_absolute,
      cooked_root_relative, ScriptStorageMode::kExternal, false));
    EXPECT_TRUE(report.success);

    const auto descriptor_path
      = cooked_root_absolute / "Scripts" / "relative_cooked_root.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_EQ((desc.flags & ScriptAssetFlags::kAllowExternalSource),
      ScriptAssetFlags::kAllowExternalSource);
    EXPECT_EQ(std::string_view { desc.external_source_path },
      "scenes/multi-script/relative_cooked_root.lua");

    std::filesystem::remove_all(test_root, ec);
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

  NOLINT_TEST_F(ScriptAssetImportTest,
    CompileEnabledEmbeddedWritesSourceAndBytecodeResources)
  {
    using data::loose_cooked::FileKind;
    using data::pak::core::kNoResourceIndex;
    using data::pak::scripting::ScriptEncoding;
    using data::pak::scripting::ScriptResourceDesc;
    constexpr auto kCompileSentinel = std::byte { 0xAA };

    auto config = AsyncImportService::Config {};
    config.thread_pool_size = 2;
    config.script_compile_callback
      = [kCompileSentinel](
          const AsyncImportService::ScriptCompileRequest& request)
      -> AsyncImportService::ScriptCompileResult {
      auto bytecode = std::vector<std::byte> { kCompileSentinel };
      bytecode.insert(bytecode.end(), request.source_bytes.begin(),
        request.source_bytes.end());
      return AsyncImportService::ScriptCompileResult {
        .success = true,
        .bytecode = std::move(bytecode),
        .diagnostics = {},
      };
    };

    auto service = ScopedImportService(config);
    const auto cooked_root
      = MakeTempCookedRoot("script_import_compile_success");
    const auto source_path = cooked_root / "input" / "compile_ok.luau";
    WriteTextFile(source_path, "return 21");

    const auto report = SubmitAndWait(service.Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, true));
    ASSERT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "compile_ok.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));
    const auto desc = ReadScriptDescriptor(descriptor_path);
    ASSERT_NE(desc.source_resource_index, kNoResourceIndex);
    ASSERT_NE(desc.bytecode_resource_index, kNoResourceIndex);

    const auto inspection = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection, FileKind::kScriptsTable);
    ASSERT_TRUE(table_relpath.has_value());
    const auto resources
      = ReadPackedRecords<ScriptResourceDesc>(cooked_root / *table_relpath);

    const auto source_index = static_cast<uint32_t>(desc.source_resource_index);
    const auto bytecode_index
      = static_cast<uint32_t>(desc.bytecode_resource_index);
    ASSERT_LT(source_index, resources.size());
    ASSERT_LT(bytecode_index, resources.size());
    EXPECT_EQ(resources.at(source_index).encoding, ScriptEncoding::kSource);
    EXPECT_EQ(resources.at(bytecode_index).encoding, ScriptEncoding::kBytecode);
  }

  NOLINT_TEST_F(ScriptAssetImportTest, CompileFailureReportsCompileDiagnostic)
  {
    auto config = AsyncImportService::Config {};
    config.thread_pool_size = 2;
    config.script_compile_callback
      = [](const AsyncImportService::ScriptCompileRequest& /*request*/)
      -> AsyncImportService::ScriptCompileResult {
      return AsyncImportService::ScriptCompileResult {
        .success = false,
        .bytecode = {},
        .diagnostics = "forced test compile failure",
      };
    };

    auto service = ScopedImportService(config);
    const auto cooked_root
      = MakeTempCookedRoot("script_import_compile_failure");
    const auto source_path = cooked_root / "input" / "compile_fail.luau";
    WriteTextFile(source_path, "return 22");

    const auto report = SubmitAndWait(service.Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, true));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.asset.compile_failed"));

    const auto descriptor_path
      = cooked_root / "Scripts" / "compile_fail.oscript";
    EXPECT_FALSE(std::filesystem::exists(descriptor_path));
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

  NOLINT_TEST_F(
    ScriptAssetImportTest, DiagnosticsFieldsAreCompleteForScriptAssetFailures)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_diagnostics_fields_asset_failure");
    const auto source_path = cooked_root / "input" / "invalid_combo.luau";
    WriteTextFile(source_path, "return 3");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));
    ASSERT_FALSE(report.success);
    ExpectDiagnosticFieldsComplete(report.diagnostics);
  }

  NOLINT_TEST_F(
    ScriptAssetImportTest, PackagingSummaryMatchesDiagnosticsForFailures)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_packaging_summary_asset_failure");
    const auto source_path = cooked_root / "input" / "summary_failure.luau";
    WriteTextFile(source_path, "return 8");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));
    ASSERT_FALSE(report.success);
    ExpectPackagingSummaryMatchesDiagnostics(report);
  }

  NOLINT_TEST_F(ScriptAssetImportTest, DispatchMatrixScriptOnly)
  {
    const auto cooked_root = MakeTempCookedRoot("script_dispatch_script_only");
    const auto source_path = cooked_root / "input" / "dispatch_only.luau";
    WriteTextFile(source_path, "return 11");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 1U);
  }

  //! Sidecar success path uses external script assets so sidecar owns
  //! script-bindings.table/script-bindings.data slot+param layout in this
  //! cooked root.
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
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsTable;
    }));
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsData;
    }));

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
    ScriptingSidecarImportTest, InlineBindingsPayloadBindsScriptToSceneNode)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_inline_binds_scene");
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
    const auto script_asset
      = FindFirstAssetByType(inspection_before_sidecar, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_report
      = Submit(MakeInlineSidecarRequest(cooked_root, scene_asset->virtual_path,
        MakeSidecarPayload(script_asset->virtual_path)));
    ASSERT_TRUE(sidecar_report.success);

    const auto inspection_after_sidecar = LoadInspection(cooked_root);
    const auto files = inspection_after_sidecar.Files();
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsTable;
    }));
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsData;
    }));

    const auto scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(scene_asset->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
    EXPECT_EQ(components[0].node_index, 0U);
    EXPECT_EQ(components[0].slot_count, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ScriptingSidecarCallbacksProvideProgressAndSingleCompletion)
  {
    constexpr auto kScriptSource = std::string_view { "return 123" };
    const auto cooked_root = MakeTempCookedRoot("script_callbacks_sidecar");
    const auto script_source = cooked_root / "input" / "callback_sidecar.luau";
    WriteTextFile(script_source, kScriptSource);

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

    const auto sidecar_source = cooked_root / "input" / "callback_sidecar.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    const auto capture = SubmitAndCaptureCallbacks(Service(),
      MakeSidecarRequest(
        sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(capture.has_value());
    EXPECT_EQ(capture->completion_calls, 1U);
    EXPECT_TRUE(capture->report.success);
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kLoading));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kWorking));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kComplete));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, ScriptingSidecarReportCountersArePopulated)
  {
    constexpr auto kScriptSource = std::string_view { "return 123" };
    const auto cooked_root
      = MakeTempCookedRoot("script_report_counters_sidecar");
    const auto script_source = cooked_root / "input" / "counter_sidecar.luau";
    WriteTextFile(script_source, kScriptSource);

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

    const auto sidecar_source = cooked_root / "input" / "counter_sidecar.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 0U);
    EXPECT_EQ(report.scripting_components_written, 1U);
    EXPECT_EQ(report.script_slots_written, 1U);
    EXPECT_EQ(report.script_params_written, 1U);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    MixedBatchCommitPreservesSuccessfulScriptWhenSidecarFails)
  {
    constexpr auto kScriptSource = std::string_view { "return 10" };
    const auto cooked_root = MakeTempCookedRoot("script_mixed_batch_commit");
    const auto script_source = cooked_root / "input" / "batch_success.luau";
    const auto sidecar_source = cooked_root / "input" / "batch_failure.json";
    WriteTextFile(script_source, kScriptSource);
    WriteTextFile(sidecar_source, "{ \"bindings\": [] }");

    auto script_report = ImportReport {};
    auto sidecar_report = ImportReport {};
    std::latch script_done(1);
    std::latch sidecar_done(1);

    const auto script_submit
      = Service().SubmitImport(MakeScriptRequest(script_source, cooked_root,
                                 ScriptStorageMode::kExternal, false),
        [&script_report, &script_done](
          const auto /*job_id*/, const ImportReport& report) {
          script_report = report;
          script_done.count_down();
        });
    ASSERT_TRUE(script_submit.has_value());
    script_done.wait();
    ASSERT_TRUE(script_report.success);

    const auto sidecar_submit
      = Service().SubmitImport(MakeSidecarRequest(sidecar_source, cooked_root,
                                 CanonicalSceneVirtualPath("missing_scene")),
        [&sidecar_report, &sidecar_done](
          const auto /*job_id*/, const ImportReport& report) {
          sidecar_report = report;
          sidecar_done.count_down();
        });
    ASSERT_TRUE(sidecar_submit.has_value());
    sidecar_done.wait();
    EXPECT_FALSE(sidecar_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      sidecar_report.diagnostics, "script.sidecar.target_scene_missing"));

    const auto inspection = LoadInspection(cooked_root);
    const auto script_asset
      = FindFirstAssetByType(inspection, AssetType::kScript);
    ASSERT_TRUE(script_asset.has_value());
    EXPECT_TRUE(
      std::filesystem::exists(cooked_root / script_asset->descriptor_relpath));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, DispatchMatrixSidecarOnlyCookedContext)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_dispatch_sidecar_cooked_context");
    const auto script_source = cooked_root / "input" / "dispatch_cooked.luau";
    WriteTextFile(script_source, "return 21");

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

    const auto sidecar_source = cooked_root / "input" / "dispatch_cooked.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, DispatchMatrixSidecarOnlyInflightContext)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_dispatch_sidecar_inflight_context");
    const auto inflight_scene_root
      = MakeTempCookedRoot("script_dispatch_sidecar_inflight_scene");
    const auto script_source = cooked_root / "input" / "dispatch_inflight.luau";
    WriteTextFile(script_source, "return 31");

    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
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

    const auto sidecar_source
      = cooked_root / "input" / "dispatch_inflight.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, inflight_scene->virtual_path);
    request.inflight_scene_contexts.push_back(
      MakeInflightSceneContext(inflight_scene_root, *inflight_scene));
    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, DispatchMatrixScriptAndSidecarBatch)
  {
    const auto cooked_root = MakeTempCookedRoot("script_dispatch_batch");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto base_script_source = cooked_root / "input" / "base_batch.luau";
    WriteTextFile(base_script_source, "return 1");
    ASSERT_TRUE(Submit(MakeScriptRequest(base_script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    const auto base_script
      = FindScriptAssetByDescriptorName(inspection, "base_batch.oscript");
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(base_script.has_value());

    const auto sidecar_source = cooked_root / "input" / "dispatch_batch.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(base_script->virtual_path));

    const auto extra_script_source = cooked_root / "input" / "extra_batch.luau";
    WriteTextFile(extra_script_source, "return 2");

    auto script_report = ImportReport {};
    auto sidecar_report = ImportReport {};
    std::latch done(2);

    const auto script_submit = Service().SubmitImport(
      MakeScriptRequest(
        extra_script_source, cooked_root, ScriptStorageMode::kExternal, false),
      [&script_report, &done](
        const auto /*job_id*/, const ImportReport& report) {
        script_report = report;
        done.count_down();
      });
    ASSERT_TRUE(script_submit.has_value());

    const auto sidecar_submit
      = Service().SubmitImport(MakeSidecarRequest(sidecar_source, cooked_root,
                                 scene_asset->virtual_path),
        [&sidecar_report, &done](
          const auto /*job_id*/, const ImportReport& report) {
          sidecar_report = report;
          done.count_down();
        });
    ASSERT_TRUE(sidecar_submit.has_value());

    done.wait();
    EXPECT_TRUE(script_report.success);
    EXPECT_TRUE(sidecar_report.success);
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, DiagnosticsFieldsAreCompleteForSidecarFailures)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_diagnostics_fields_sidecar_failure");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "diagnostics_fields_sidecar.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload("Scripts/not_canonical_script_path.oscript"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(report.success);
    ExpectDiagnosticFieldsComplete(report.diagnostics);
    EXPECT_TRUE(HasAnyObjectPath(report.diagnostics));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, PackagingSummaryMatchesDiagnosticsForFailures)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_packaging_summary_sidecar_failure");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "summary_sidecar_failure.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload(CanonicalScriptVirtualPath("missing_script")));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(report.success);
    ExpectPackagingSummaryMatchesDiagnostics(report);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    DependencyOrderingRequiresScriptAvailabilityBeforeSidecarResolution)
  {
    const auto cooked_root = MakeTempCookedRoot("script_dependency_ordering");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "dependency_ordering.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload(CanonicalScriptVirtualPath("deferred")));

    const auto first_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(first_report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      first_report.diagnostics, "script.sidecar.script_ref_unresolved"));

    const auto script_source = cooked_root / "input" / "deferred.luau";
    WriteTextFile(script_source, "return 55");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_after_script = LoadInspection(cooked_root);
    const auto deferred_script = FindScriptAssetByDescriptorName(
      inspection_after_script, "deferred.oscript");
    ASSERT_TRUE(deferred_script.has_value());
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(deferred_script->virtual_path));

    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_TRUE(second_report.success);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    CycleRejectionFailsWhenReferenceResolvesToNonScriptAsset)
  {
    const auto cooked_root = MakeTempCookedRoot("script_cycle_rejection");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "cycle_reject.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(scene_asset->virtual_path));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.script_ref_not_script_asset"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    DiagnosticsOrderingIsDeterministicAcrossRepeatedInvalidRuns)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_diagnostics_ordering_determinism");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection, AssetType::kScene);
    ASSERT_TRUE(scene_asset.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "diagnostics_ordering.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayload(std::vector {
        SidecarBindingSpec { .node_index = 99999U,
          .slot_id = "oob",
          .script_virtual_path = CanonicalScriptVirtualPath("missing"),
          .execution_order = 1 },
        SidecarBindingSpec { .node_index = 0U,
          .slot_id = "bad_path",
          .script_virtual_path = "Scripts/not_canonical.oscript",
          .execution_order = 2 },
      }));

    const auto first_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    const auto second_report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_FALSE(first_report.success);
    ASSERT_FALSE(second_report.success);

    const auto MakeDiagnosticSignature
      = [](const ImportReport& report) -> std::vector<std::string> {
      auto signature = std::vector<std::string> {};
      signature.reserve(report.diagnostics.size());
      for (const auto& diagnostic : report.diagnostics) {
        signature.push_back(diagnostic.code + "|" + diagnostic.object_path);
      }
      return signature;
    };
    EXPECT_EQ(MakeDiagnosticSignature(first_report),
      MakeDiagnosticSignature(second_report));
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
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsTable;
    }));
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsData;
    }));

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
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root_a)).success);
    ASSERT_TRUE(
      Submit(MakeSceneRequest(model_path, inflight_scene_root_b)).success);

    const auto inspection_a = LoadInspection(inflight_scene_root_a);
    const auto inspection_b = LoadInspection(inflight_scene_root_b);
    const auto scene_a = FindFirstAssetByType(inspection_a, AssetType::kScene);
    const auto scene_b = FindFirstAssetByType(inspection_b, AssetType::kScene);
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());
    ASSERT_EQ(scene_a->virtual_path, scene_b->virtual_path);
    ASSERT_EQ(scene_a->key, scene_b->key);

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
    ResolverDelegationSupportsDeterministicKeyAcrossMountedContexts)
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
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root_a)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root_b)).success);

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
    ASSERT_EQ(scene_a->key, scene_b->key);

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
    ContextResolvedSceneLoadsScriptsTablesFromWinningCookedContext)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;

    const auto request_root
      = MakeTempCookedRoot("script_sidecar_context_table_source_request");
    const auto context_root
      = MakeTempCookedRoot("script_sidecar_context_table_source_context");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto context_script_source
      = context_root / "input" / "context_script.luau";
    WriteTextFile(context_script_source, "return 77");
    ASSERT_TRUE(Submit(MakeScriptRequest(context_script_source, context_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, context_root)).success);

    const auto context_inspection_before = LoadInspection(context_root);
    const auto context_scene
      = FindFirstAssetByType(context_inspection_before, AssetType::kScene);
    const auto context_script
      = FindFirstAssetByType(context_inspection_before, AssetType::kScript);
    ASSERT_TRUE(context_scene.has_value());
    ASSERT_TRUE(context_script.has_value());

    const auto context_sidecar_source
      = context_root / "input" / "seed_context_sidecar.json";
    WriteTextFile(
      context_sidecar_source, MakeSidecarPayload(context_script->virtual_path));
    ASSERT_TRUE(Submit(MakeSidecarRequest(context_sidecar_source, context_root,
                         context_scene->virtual_path))
        .success);

    const auto request_script_source
      = request_root / "input" / "context_script.luau";
    WriteTextFile(request_script_source, "return 88");
    ASSERT_TRUE(Submit(MakeScriptRequest(request_script_source, request_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto request_inspection_before = LoadInspection(request_root);
    const auto request_script
      = FindFirstAssetByType(request_inspection_before, AssetType::kScript);
    ASSERT_TRUE(request_script.has_value());

    const auto request_sidecar_source
      = request_root / "input" / "context_resolve_sidecar.json";
    WriteTextFile(
      request_sidecar_source, MakeSidecarPayload(request_script->virtual_path));

    auto request = MakeSidecarRequest(
      request_sidecar_source, request_root, context_scene->virtual_path);
    request.cooked_context_roots.push_back(context_root);
    const auto report = Submit(std::move(request));
    ASSERT_TRUE(report.success);

    const auto request_inspection_after = LoadInspection(request_root);
    const auto patched_scene
      = FindFirstAssetByType(request_inspection_after, AssetType::kScene);
    ASSERT_TRUE(patched_scene.has_value());
    EXPECT_EQ(patched_scene->key, context_scene->key);

    const auto files = request_inspection_after.Files();
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsTable;
    }));
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) {
      return file.kind == FileKind::kScriptBindingsData;
    }));

    const auto scene_bytes
      = ReadAllBytes(request_root / patched_scene->descriptor_relpath);
    ASSERT_FALSE(scene_bytes.empty());
    auto scene = data::SceneAsset(patched_scene->key, scene_bytes);
    const auto components = scene.GetComponents<ScriptingComponentRecord>();
    ASSERT_EQ(components.size(), 1U);
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

    const auto concurrent_table_relpath = FindFileRelPathByKind(
      concurrent_inspection, FileKind::kScriptBindingsTable);
    const auto standalone_table_relpath = FindFileRelPathByKind(
      standalone_inspection, FileKind::kScriptBindingsTable);
    const auto concurrent_data_relpath = FindFileRelPathByKind(
      concurrent_inspection, FileKind::kScriptBindingsData);
    const auto standalone_data_relpath = FindFileRelPathByKind(
      standalone_inspection, FileKind::kScriptBindingsData);
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
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable);
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
    SequentialSidecarImportsKeepDistinctParamsAcrossScenes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_multiscene_distinct_params");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "rotate_shared.luau";
    WriteTextFile(script_source, "return 1");

    ASSERT_TRUE(Submit(MakeSceneRequest(scene_a_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_b_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto script_asset = FindScriptAssetByDescriptorName(
      inspection_before, "rotate_shared.oscript");
    ASSERT_TRUE(script_asset.has_value());

    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    for (const auto& entry : inspection_before.Assets()) {
      if (static_cast<AssetType>(entry.asset_type) != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = AssetType::kScene,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = AssetType::kScene,
        };
      }
    }
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());

    const auto sidecar_a = input_dir / "scene_a.sidescript.json";
    const auto sidecar_b = input_dir / "scene_b.sidescript.json";
    WriteTextFile(sidecar_a,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 1.0F));
    WriteTextFile(sidecar_b,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 7.5F));

    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path))
        .success);
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_b, cooked_root, scene_b->virtual_path))
        .success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable);
    const auto data_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    const auto params
      = ReadPackedRecords<ScriptParamRecord>(cooked_root / *data_relpath);
    ASSERT_FALSE(slots.empty());
    ASSERT_FALSE(params.empty());

    const auto ReadSpeedForScene = [&](const AssetRef& scene_ref) {
      const auto scene_bytes
        = ReadAllBytes(cooked_root / scene_ref.descriptor_relpath);
      EXPECT_FALSE(scene_bytes.empty());
      if (scene_bytes.empty()) {
        return 0.0F;
      }
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component = std::find_if(components.begin(), components.end(),
        [](const auto& candidate) { return candidate.node_index == 0U; });
      EXPECT_NE(component, components.end());
      if (component == components.end()) {
        return 0.0F;
      }
      const auto slot_index = static_cast<size_t>(component->slot_start_index);
      EXPECT_LT(slot_index, slots.size());
      if (slot_index >= slots.size()) {
        return 0.0F;
      }
      const auto& slot = slots.at(slot_index);
      EXPECT_EQ(slot.params_array_offset % sizeof(ScriptParamRecord), 0U);
      const auto param_index = static_cast<size_t>(
        slot.params_array_offset / sizeof(ScriptParamRecord));
      EXPECT_GT(slot.params_count, 0U);
      EXPECT_LT(param_index, params.size());
      if (slot.params_count == 0U || param_index >= params.size()) {
        return 0.0F;
      }
      return params.at(param_index).value.as_float;
    };

    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_a), 1.0F);
    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_b), 7.5F);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ConcurrentSidecarImportsKeepDistinctParamsAcrossScenes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    auto config = AsyncImportService::Config {};
    config.thread_pool_size = 2;
    config.max_in_flight_jobs = 2;
    auto service = ScopedImportService(config);

    const auto cooked_root = MakeTempCookedRoot(
      "script_sidecar_multiscene_distinct_params_parallel");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "rotate_shared.luau";
    WriteTextFile(script_source, "return 1");

    ASSERT_TRUE(SubmitAndWait(
      service.Service(), MakeSceneRequest(scene_a_source, cooked_root))
        .success);
    ASSERT_TRUE(SubmitAndWait(
      service.Service(), MakeSceneRequest(scene_b_source, cooked_root))
        .success);
    ASSERT_TRUE(SubmitAndWait(service.Service(),
      MakeScriptRequest(
        script_source, cooked_root, ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto script_asset = FindScriptAssetByDescriptorName(
      inspection_before, "rotate_shared.oscript");
    ASSERT_TRUE(script_asset.has_value());

    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    for (const auto& entry : inspection_before.Assets()) {
      if (static_cast<AssetType>(entry.asset_type) != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = AssetType::kScene,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = AssetType::kScene,
        };
      }
    }
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());

    const auto sidecar_a = input_dir / "scene_a_parallel.sidescript.json";
    const auto sidecar_b = input_dir / "scene_b_parallel.sidescript.json";
    WriteTextFile(sidecar_a,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 1.0F));
    WriteTextFile(sidecar_b,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 7.5F));

    auto report_a = ImportReport {};
    auto report_b = ImportReport {};
    std::latch done(2);

    const auto submit_a = service.Service().SubmitImport(
      MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path),
      [&report_a, &done](const auto /*job_id*/, const ImportReport& report) {
        report_a = report;
        done.count_down();
      });
    ASSERT_TRUE(submit_a.has_value());

    const auto submit_b = service.Service().SubmitImport(
      MakeSidecarRequest(sidecar_b, cooked_root, scene_b->virtual_path),
      [&report_b, &done](const auto /*job_id*/, const ImportReport& report) {
        report_b = report;
        done.count_down();
      });
    ASSERT_TRUE(submit_b.has_value());

    done.wait();
    ASSERT_TRUE(report_a.success);
    ASSERT_TRUE(report_b.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable);
    const auto data_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    const auto params
      = ReadPackedRecords<ScriptParamRecord>(cooked_root / *data_relpath);
    ASSERT_FALSE(slots.empty());
    ASSERT_FALSE(params.empty());

    const auto ReadSpeedForScene = [&](const AssetRef& scene_ref) {
      const auto scene_bytes
        = ReadAllBytes(cooked_root / scene_ref.descriptor_relpath);
      EXPECT_FALSE(scene_bytes.empty());
      if (scene_bytes.empty()) {
        return 0.0F;
      }
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component = std::find_if(components.begin(), components.end(),
        [](const auto& candidate) { return candidate.node_index == 0U; });
      EXPECT_NE(component, components.end());
      if (component == components.end()) {
        return 0.0F;
      }
      const auto slot_index = static_cast<size_t>(component->slot_start_index);
      EXPECT_LT(slot_index, slots.size());
      if (slot_index >= slots.size()) {
        return 0.0F;
      }
      const auto& slot = slots.at(slot_index);
      EXPECT_EQ(slot.params_array_offset % sizeof(ScriptParamRecord), 0U);
      const auto param_index = static_cast<size_t>(
        slot.params_array_offset / sizeof(ScriptParamRecord));
      EXPECT_GT(slot.params_count, 0U);
      EXPECT_LT(param_index, params.size());
      if (slot.params_count == 0U || param_index >= params.size()) {
        return 0.0F;
      }
      return params.at(param_index).value.as_float;
    };

    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_a), 1.0F);
    EXPECT_FLOAT_EQ(ReadSpeedForScene(*scene_b), 7.5F);
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
    WriteTextFile(sidecar_source,
      MakeSidecarPayload(CanonicalScriptVirtualPath("does_not_exist")));

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
      inspection_after_success, FileKind::kScriptBindingsTable);
    const auto data_relpath = FindFileRelPathByKind(
      inspection_after_success, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto scene_before_failure
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto table_before_failure
      = ReadAllBytes(cooked_root / *table_relpath);
    const auto data_before_failure = ReadAllBytes(cooked_root / *data_relpath);

    WriteTextFile(sidecar_source,
      MakeSidecarPayload(CanonicalScriptVirtualPath("does_not_exist")));
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

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ScriptsTableWriteFailureDoesNotMutateSceneDescriptor)
  {
    using data::loose_cooked::FileKind;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_table_write_atomicity");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto script_source = cooked_root / "input" / "writer_fail.luau";
    WriteTextFile(script_source, "return 9");
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto scene_before
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_FALSE(scene_before.empty());

    const auto sidecar_source
      = cooked_root / "input" / "writer_failure_sidecar.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));

    auto request = MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path);
    const auto scripts_table_path = cooked_root
      / request.loose_cooked_layout.resources_dir / "script-bindings.table";
    std::filesystem::create_directories(scripts_table_path);

    const auto report = Submit(std::move(request));
    ASSERT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.sidecar.scripts_table_write_failed"));

    const auto scene_after
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    EXPECT_EQ(scene_before, scene_after);

    const auto inspection_after = LoadInspection(cooked_root);
    EXPECT_FALSE(
      FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable)
        .has_value());
    EXPECT_FALSE(
      FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsData)
        .has_value());
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, MissingSourceFileFailsWithSidecarReadDiagnostic)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_missing_source_file");
    const auto missing_source = cooked_root / "input" / "missing_sidecar.json";

    const auto report = Submit(MakeSidecarRequest(
      missing_source, cooked_root, CanonicalSceneVirtualPath("any_scene")));

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
      sidecar_source, cooked_root, CanonicalSceneVirtualPath("any_scene")));

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
      sidecar_source, cooked_root, CanonicalSceneVirtualPath("missing_scene")));

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

  NOLINT_TEST_F(ScriptingSidecarImportTest, SidecarParsesAllSupportedParamTypes)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptParamType;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_all_param_types");
    const auto script_source = cooked_root / "input" / "param_types.luau";
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

    constexpr auto kParamsJson = R"([
        { "key": "enabled", "type": "bool", "value": true },
        { "key": "lives", "type": "int32", "value": 42 },
        { "key": "speed", "type": "float", "value": 3.5 },
        { "key": "title", "type": "string", "value": "agent" },
        { "key": "uv", "type": "vec2", "value": [1.0, 2.0] },
        { "key": "dir", "type": "vec3", "value": [3.0, 4.0, 5.0] },
        { "key": "quat", "type": "vec4", "value": [6.0, 7.0, 8.0, 9.0] }
      ])";

    const auto sidecar_source = cooked_root / "input" / "params_ok.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithParams(script_asset->virtual_path, kParamsJson));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    ASSERT_TRUE(report.success);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable);
    const auto data_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    const auto params
      = ReadPackedRecords<ScriptParamRecord>(cooked_root / *data_relpath);
    ASSERT_EQ(slots.size(), 1U);
    ASSERT_EQ(slots[0].params_count, 7U);
    ASSERT_EQ(slots[0].params_array_offset % sizeof(ScriptParamRecord), 0U);

    const auto param_start = static_cast<size_t>(
      slots[0].params_array_offset / sizeof(ScriptParamRecord));
    ASSERT_LE(param_start + slots[0].params_count, params.size());
    const auto ParamAt = [&](const size_t i) -> const ScriptParamRecord& {
      return params.at(param_start + i);
    };

    EXPECT_EQ(ParamAt(0).type, ScriptParamType::kBool);
    EXPECT_TRUE(ParamAt(0).value.as_bool);
    EXPECT_EQ(ParamAt(1).type, ScriptParamType::kInt32);
    EXPECT_EQ(ParamAt(1).value.as_int32, 42);
    EXPECT_EQ(ParamAt(2).type, ScriptParamType::kFloat);
    EXPECT_FLOAT_EQ(ParamAt(2).value.as_float, 3.5F);
    EXPECT_EQ(ParamAt(3).type, ScriptParamType::kString);
    EXPECT_STREQ(ParamAt(3).value.as_string, "agent");
    EXPECT_EQ(ParamAt(4).type, ScriptParamType::kVec2);
    EXPECT_FLOAT_EQ(ParamAt(4).value.as_vec[0], 1.0F);
    EXPECT_FLOAT_EQ(ParamAt(4).value.as_vec[1], 2.0F);
    EXPECT_EQ(ParamAt(5).type, ScriptParamType::kVec3);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[0], 3.0F);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[1], 4.0F);
    EXPECT_FLOAT_EQ(ParamAt(5).value.as_vec[2], 5.0F);
    EXPECT_EQ(ParamAt(6).type, ScriptParamType::kVec4);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[0], 6.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[1], 7.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[2], 8.0F);
    EXPECT_FLOAT_EQ(ParamAt(6).value.as_vec[3], 9.0F);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, SidecarRejectsUnsupportedParamType)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_unsupported_param_type");
    const auto script_source = cooked_root / "input" / "unsupported_param.luau";
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

    const auto sidecar_source
      = cooked_root / "input" / "unsupported_param.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithParams(script_asset->virtual_path,
        R"([ { "key": "bad", "type": "uint32", "value": 1 } ])"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.param_invalid"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, SidecarRejectsOutOfRangeInt32Param)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_int32_out_of_range");
    const auto script_source = cooked_root / "input" / "int32_range.luau";
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

    const auto sidecar_source = cooked_root / "input" / "int32_range.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithParams(script_asset->virtual_path,
        R"([ { "key": "bad", "type": "int32", "value": 2147483648 } ])"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.param_invalid"));
  }

  NOLINT_TEST_F(
    ScriptingSidecarImportTest, SidecarRejectsInvalidVectorParamShape)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_invalid_vector_shape");
    const auto script_source = cooked_root / "input" / "vec_shape.luau";
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

    const auto sidecar_source = cooked_root / "input" / "vec_shape.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithParams(script_asset->virtual_path,
        R"([ { "key": "bad", "type": "vec3", "value": [1.0, 2.0] } ])"));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.param_invalid"));
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest, SidecarRejectsTooLongStringParam)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_string_param_too_long");
    const auto script_source = cooked_root / "input" / "long_string.luau";
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

    const auto long_value = std::string(60U, 'x');
    const auto params_json
      = std::string(
          "[ { \"key\": \"name\", \"type\": \"string\", \"value\": \"")
      + long_value + "\" } ]";
    const auto sidecar_source = cooked_root / "input" / "long_string.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithParams(script_asset->virtual_path, params_json));

    const auto report = Submit(MakeSidecarRequest(
      sidecar_source, cooked_root, scene_asset->virtual_path));
    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.sidecar.param_invalid"));
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
      = FindFileRelPathByKind(after_inspection, FileKind::kScriptBindingsTable);
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
      = FindFileRelPathByKind(first_inspection, FileKind::kScriptBindingsTable);
    const auto data_relpath
      = FindFileRelPathByKind(first_inspection, FileKind::kScriptBindingsData);
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
    ScriptingSidecarImportTest, ScenePatchPreservesNodeAndStringTables)
  {
    using data::pak::world::SceneAssetDesc;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_scene_patch_preserves_core_tables");
    const auto script_source = cooked_root / "input" / "patch_guard.luau";
    WriteTextFile(script_source, "return 1");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto before_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    ASSERT_GE(before_scene_bytes.size(), sizeof(SceneAssetDesc));

    const auto ExtractCoreTables = [](const std::vector<std::byte>& bytes)
      -> std::optional<
        std::pair<std::vector<std::byte>, std::vector<std::byte>>> {
      if (bytes.size() < sizeof(SceneAssetDesc)) {
        return std::nullopt;
      }
      auto desc = SceneAssetDesc {};
      std::memcpy(&desc, bytes.data(), sizeof(desc));
      const auto range_ok = [&](const uint64_t offset, const uint64_t size) {
        return offset <= bytes.size() && size <= (bytes.size() - offset);
      };

      const auto node_size = static_cast<uint64_t>(desc.nodes.count)
        * static_cast<uint64_t>(desc.nodes.entry_size);
      const auto string_size = static_cast<uint64_t>(desc.scene_strings.size);
      if (!range_ok(desc.nodes.offset, node_size)
        || !range_ok(desc.scene_strings.offset, string_size)) {
        return std::nullopt;
      }

      auto nodes = std::vector<std::byte> {};
      nodes.resize(static_cast<size_t>(node_size));
      if (!nodes.empty()) {
        std::memcpy(nodes.data(),
          bytes.data() + static_cast<size_t>(desc.nodes.offset), nodes.size());
      }

      auto strings = std::vector<std::byte> {};
      strings.resize(static_cast<size_t>(string_size));
      if (!strings.empty()) {
        std::memcpy(strings.data(),
          bytes.data() + static_cast<size_t>(desc.scene_strings.offset),
          strings.size());
      }
      return std::make_optional(
        std::make_pair(std::move(nodes), std::move(strings)));
    };

    const auto core_before = ExtractCoreTables(before_scene_bytes);
    ASSERT_TRUE(core_before.has_value());

    const auto sidecar_source
      = cooked_root / "input" / "patch_guard_sidecar.json";
    WriteTextFile(
      sidecar_source, MakeSidecarPayload(script_asset->virtual_path));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto after_scene_bytes
      = ReadAllBytes(cooked_root / scene_asset->descriptor_relpath);
    const auto core_after = ExtractCoreTables(after_scene_bytes);
    ASSERT_TRUE(core_after.has_value());

    EXPECT_EQ(core_before->first, core_after->first);
    EXPECT_EQ(core_before->second, core_after->second);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ReimportWithSameSlotShapeUpdatesInPlaceWithoutGrowingTables)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_in_place_update");
    const auto script_source = cooked_root / "input" / "in_place.luau";
    WriteTextFile(script_source, "return 1");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);
    ASSERT_TRUE(Submit(MakeSceneRequest(model_path, cooked_root)).success);

    const auto inspection_before = LoadInspection(cooked_root);
    const auto scene_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScene);
    const auto script_asset
      = FindFirstAssetByType(inspection_before, AssetType::kScript);
    ASSERT_TRUE(scene_asset.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_source = cooked_root / "input" / "in_place.json";
    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 1.0F));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto inspection_after_first = LoadInspection(cooked_root);
    const auto table_relpath = FindFileRelPathByKind(
      inspection_after_first, FileKind::kScriptBindingsTable);
    const auto data_relpath = FindFileRelPathByKind(
      inspection_after_first, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());

    const auto first_table_bytes = ReadAllBytes(cooked_root / *table_relpath);
    const auto first_data_bytes = ReadAllBytes(cooked_root / *data_relpath);

    WriteTextFile(sidecar_source,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 9.5F));
    ASSERT_TRUE(Submit(MakeSidecarRequest(sidecar_source, cooked_root,
                         scene_asset->virtual_path))
        .success);

    const auto second_table_bytes = ReadAllBytes(cooked_root / *table_relpath);
    const auto second_data_bytes = ReadAllBytes(cooked_root / *data_relpath);
    EXPECT_EQ(first_table_bytes, second_table_bytes);
    EXPECT_EQ(first_data_bytes.size(), second_data_bytes.size());

    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    const auto params
      = ReadPackedRecords<ScriptParamRecord>(cooked_root / *data_relpath);
    ASSERT_EQ(slots.size(), 1U);
    ASSERT_EQ(slots[0].params_count, 1U);
    const auto param_index = static_cast<size_t>(
      slots[0].params_array_offset / sizeof(ScriptParamRecord));
    ASSERT_LT(param_index, params.size());
    EXPECT_FLOAT_EQ(params[param_index].value.as_float, 9.5F);
  }

  NOLINT_TEST_F(ScriptingSidecarImportTest,
    ReimportWithExpandedParamsPreservesOtherSceneSlotRanges)
  {
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptingComponentRecord;
    using data::pak::scripting::ScriptParamRecord;
    using data::pak::scripting::ScriptSlotRecord;

    const auto cooked_root
      = MakeTempCookedRoot("script_sidecar_expand_preserve_other_scene");
    const auto model_path = ModelPath();
    ASSERT_TRUE(std::filesystem::exists(model_path));

    const auto input_dir = cooked_root / "input";
    std::filesystem::create_directories(input_dir);
    const auto scene_a_source = input_dir / "scene_a.glb";
    const auto scene_b_source = input_dir / "scene_b.glb";
    std::filesystem::copy_file(model_path, scene_a_source);
    std::filesystem::copy_file(model_path, scene_b_source);

    const auto script_source = input_dir / "shared.luau";
    WriteTextFile(script_source, "return 7");
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_a_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeSceneRequest(scene_b_source, cooked_root)).success);
    ASSERT_TRUE(Submit(MakeScriptRequest(script_source, cooked_root,
                         ScriptStorageMode::kExternal, false))
        .success);

    const auto inspection = LoadInspection(cooked_root);
    auto scene_a = std::optional<AssetRef> {};
    auto scene_b = std::optional<AssetRef> {};
    auto script_asset = std::optional<AssetRef> {};
    for (const auto& entry : inspection.Assets()) {
      const auto type = static_cast<AssetType>(entry.asset_type);
      if (type == AssetType::kScript) {
        script_asset = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = type,
        };
        continue;
      }
      if (type != AssetType::kScene) {
        continue;
      }
      const auto descriptor_name
        = std::filesystem::path(entry.descriptor_relpath).filename().string();
      if (descriptor_name == "scene_a.oscene") {
        scene_a = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = type,
        };
      } else if (descriptor_name == "scene_b.oscene") {
        scene_b = AssetRef {
          .key = entry.key,
          .virtual_path = entry.virtual_path,
          .descriptor_relpath = entry.descriptor_relpath,
          .type = type,
        };
      }
    }
    ASSERT_TRUE(scene_a.has_value());
    ASSERT_TRUE(scene_b.has_value());
    ASSERT_TRUE(script_asset.has_value());

    const auto sidecar_a = input_dir / "scene_a.json";
    const auto sidecar_b = input_dir / "scene_b.json";
    WriteTextFile(sidecar_a,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 1.0F));
    WriteTextFile(sidecar_b,
      MakeSidecarPayloadWithFloatParam(script_asset->virtual_path, 7.5F));
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path))
        .success);
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_b, cooked_root, scene_b->virtual_path))
        .success);

    const auto ReadSlotStartForNodeZero = [&](const AssetRef& scene_ref) {
      const auto scene_bytes
        = ReadAllBytes(cooked_root / scene_ref.descriptor_relpath);
      auto scene = data::SceneAsset(scene_ref.key, scene_bytes);
      const auto components = scene.GetComponents<ScriptingComponentRecord>();
      const auto component = std::find_if(components.begin(), components.end(),
        [](const auto& candidate) { return candidate.node_index == 0U; });
      EXPECT_NE(component, components.end());
      return component == components.end() ? 0U : component->slot_start_index;
    };

    const auto scene_b_slot_start_before = ReadSlotStartForNodeZero(*scene_b);

    WriteTextFile(sidecar_a,
      MakeSidecarPayloadWithParams(script_asset->virtual_path,
        R"([
        { "key": "speed", "type": "float", "value": 2.0 },
        { "key": "enabled", "type": "bool", "value": true }
      ])"));
    ASSERT_TRUE(
      Submit(MakeSidecarRequest(sidecar_a, cooked_root, scene_a->virtual_path))
        .success);

    const auto scene_b_slot_start_after = ReadSlotStartForNodeZero(*scene_b);
    EXPECT_EQ(scene_b_slot_start_before, scene_b_slot_start_after);

    const auto inspection_after = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsTable);
    const auto data_relpath
      = FindFileRelPathByKind(inspection_after, FileKind::kScriptBindingsData);
    ASSERT_TRUE(table_relpath.has_value());
    ASSERT_TRUE(data_relpath.has_value());
    const auto slots
      = ReadPackedRecords<ScriptSlotRecord>(cooked_root / *table_relpath);
    const auto params
      = ReadPackedRecords<ScriptParamRecord>(cooked_root / *data_relpath);
    ASSERT_FALSE(slots.empty());
    ASSERT_FALSE(params.empty());

    const auto scene_b_bytes
      = ReadAllBytes(cooked_root / scene_b->descriptor_relpath);
    auto scene_b_asset = data::SceneAsset(scene_b->key, scene_b_bytes);
    const auto components
      = scene_b_asset.GetComponents<ScriptingComponentRecord>();
    const auto component = std::find_if(components.begin(), components.end(),
      [](const auto& candidate) { return candidate.node_index == 0U; });
    ASSERT_NE(component, components.end());
    const auto slot_index = static_cast<size_t>(component->slot_start_index);
    ASSERT_LT(slot_index, slots.size());
    const auto& slot = slots.at(slot_index);
    const auto param_index = static_cast<size_t>(
      slot.params_array_offset / sizeof(ScriptParamRecord));
    ASSERT_LT(param_index, params.size());
    EXPECT_FLOAT_EQ(params.at(param_index).value.as_float, 7.5F);
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
