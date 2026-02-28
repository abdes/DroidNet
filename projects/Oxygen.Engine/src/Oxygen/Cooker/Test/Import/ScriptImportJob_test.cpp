//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <latch>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import::test {

namespace {

  using oxygen::content::import::AsyncImportService;
  using oxygen::content::import::ImportDiagnostic;
  using oxygen::content::import::ImportReport;
  using oxygen::content::import::ImportRequest;
  using oxygen::content::import::ScriptingImportKind;
  using oxygen::content::import::ScriptStorageMode;

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

  class ScriptImportJobTest : public testing::Test {
  protected:
    AsyncImportService::Config config_ {
      .thread_pool_size = 2,
    };
  };

  NOLINT_TEST_F(
    ScriptImportJobTest, EmbeddedScriptImportWritesDescriptorAndScriptFiles)
  {
    using data::AssetType;
    using data::loose_cooked::FileKind;
    using data::pak::core::kNoResourceIndex;
    using data::pak::scripting::ScriptAssetFlags;

    const auto cooked_root
      = MakeTempCookedRoot("script_import_embedded_success");
    const auto source_path = cooked_root / "input" / "hello.luau";
    WriteTextFile(source_path, "return 7");

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();
    const auto report = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "hello.oscript";
    EXPECT_TRUE(std::filesystem::exists(descriptor_path));

    lc::Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

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

  NOLINT_TEST_F(
    ScriptImportJobTest, ExternalScriptImportStoresExternalPathAndNoScriptFiles)
  {
    using data::loose_cooked::FileKind;
    using data::pak::core::kNoResourceIndex;
    using data::pak::scripting::ScriptAssetFlags;

    const auto cooked_root
      = MakeTempCookedRoot("script_import_external_success");
    const auto source_path = cooked_root / "input" / "external.lua";
    WriteTextFile(source_path, "print('hello')");

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();
    const auto report = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kExternal, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "external.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));

    lc::Inspection inspection;
    inspection.LoadFromFile(cooked_root / "container.index.bin");

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

  NOLINT_TEST_F(ScriptImportJobTest, CompileEnabledFailsWhenCompilerUnavailable)
  {
    const auto cooked_root
      = MakeTempCookedRoot("script_import_compile_unavailable");
    const auto source_path = cooked_root / "input" / "compile_me.luau";
    WriteTextFile(source_path, "return 99");

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();
    const auto report = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.asset.compiler_unavailable"));
  }

  NOLINT_TEST_F(ScriptImportJobTest, CompileWithExternalStorageIsRejected)
  {
    const auto cooked_root = MakeTempCookedRoot("script_import_invalid_combo");
    const auto source_path = cooked_root / "input" / "combo.luau";
    WriteTextFile(source_path, "return 1");

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();
    const auto report = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kExternal, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.request.invalid_option_combo"));
  }

  NOLINT_TEST_F(ScriptImportJobTest, ReimportOverwritesScriptDescriptorIdentity)
  {
    using data::AssetType;

    const auto cooked_root = MakeTempCookedRoot("script_import_reimport");
    const auto source_path = cooked_root / "input" / "reload.luau";
    WriteTextFile(source_path, "return 'first'");

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();

    const auto report_first = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, false));
    ASSERT_TRUE(report_first.success);

    lc::Inspection first_inspection;
    first_inspection.LoadFromFile(cooked_root / "container.index.bin");
    const auto first_assets = first_inspection.Assets();
    ASSERT_EQ(first_assets.size(), 1U);
    ASSERT_EQ(static_cast<AssetType>(first_assets.front().asset_type),
      AssetType::kScript);
    const auto first_key = first_assets.front().key;

    WriteTextFile(source_path, "return 'second'");

    const auto report_second = SubmitAndWait(service,
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, false));
    ASSERT_TRUE(report_second.success);

    lc::Inspection second_inspection;
    second_inspection.LoadFromFile(cooked_root / "container.index.bin");
    const auto second_assets = second_inspection.Assets();
    ASSERT_EQ(second_assets.size(), 1U);
    ASSERT_EQ(static_cast<AssetType>(second_assets.front().asset_type),
      AssetType::kScript);
    EXPECT_EQ(second_assets.front().key, first_key);
  }

  NOLINT_TEST_F(
    ScriptImportJobTest, EmbeddedScriptImportIsDeterministicForSameInputs)
  {
    using data::AssetType;

    const auto cooked_root_a
      = MakeTempCookedRoot("script_import_determinism_a");
    const auto cooked_root_b
      = MakeTempCookedRoot("script_import_determinism_b");
    const auto source_a = cooked_root_a / "input" / "stable.luau";
    const auto source_b = cooked_root_b / "input" / "stable.luau";
    constexpr auto kSourceText = std::string_view { "return 'stable'" };
    WriteTextFile(source_a, kSourceText);
    WriteTextFile(source_b, kSourceText);

    ScopedImportService scoped_service(config_);
    auto& service = scoped_service.Service();

    const auto report_a = SubmitAndWait(service,
      MakeScriptRequest(source_a, cooked_root_a, ScriptStorageMode::kEmbedded));
    const auto report_b = SubmitAndWait(service,
      MakeScriptRequest(source_b, cooked_root_b, ScriptStorageMode::kEmbedded));
    ASSERT_TRUE(report_a.success);
    ASSERT_TRUE(report_b.success);

    lc::Inspection inspection_a;
    lc::Inspection inspection_b;
    inspection_a.LoadFromFile(cooked_root_a / "container.index.bin");
    inspection_b.LoadFromFile(cooked_root_b / "container.index.bin");

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

} // namespace

} // namespace oxygen::content::import::test
