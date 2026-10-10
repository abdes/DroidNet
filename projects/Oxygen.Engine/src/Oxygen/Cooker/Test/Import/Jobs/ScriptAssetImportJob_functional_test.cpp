//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Jobs/ScriptAssetImportJob.cpp

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

#include "ScriptImportTestSupport.h"

#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportProgress.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::import::test {

namespace {

  auto ReadScriptDescriptor(const std::filesystem::path& descriptor_path)
    -> data::pak::scripting::ScriptAssetDesc
  {
    using data::pak::scripting::ScriptAssetDesc;

    ScriptAssetDesc desc {};
    std::ifstream in(descriptor_path, std::ios::binary);
    in.read(reinterpret_cast<char*>(&desc), sizeof(desc));
    return desc;
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
    auto bytes = ReadBytes(path);
    if (bytes.empty() || (bytes.size() % sizeof(T)) != 0U) {
      return {};
    }

    auto records = std::vector<T> {};
    records.resize(bytes.size() / sizeof(T));
    std::memcpy(records.data(), bytes.data(), bytes.size());
    return records;
  }

  class ScriptAssetImportJobTest : public ScriptingImportTestBase { };

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    SubmitRejectionReturnsNulloptForInvalidAndShutdown)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_submit_rejection";

    auto invalid_callback_invoked = std::atomic<bool> { false };
    auto invalid_request = ImportRequest {};
    invalid_request.source_path = cooked_root / "input" / "unsupported.abc";
    invalid_request.cooked_root = cooked_root;

    const auto invalid_submit
      = Service().SubmitImport(std::move(invalid_request),
        [&invalid_callback_invoked](
          const auto /*job_id*/, const ImportReport& /*report*/) -> auto {
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
        const auto /*job_id*/, const ImportReport& /*report*/) -> auto {
        shutdown_callback_invoked.store(true, std::memory_order_release);
      });
    EXPECT_FALSE(shutdown_submit.has_value());
    EXPECT_FALSE(shutdown_callback_invoked.load(std::memory_order_acquire));
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    ScriptAssetCallbacksProvideProgressAndSingleCompletion)
  {
    constexpr auto kScriptSource = std::string_view { "return 17" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_callbacks_asset";
    const auto source_path = cooked_root / "input" / "callback_asset.luau";
    WriteText(source_path, kScriptSource);

    const auto capture = SubmitAndCaptureCallbacks(Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_HAS_VALUE(capture) << "Expected capture to be present";
    EXPECT_EQ(capture->completion_calls, 1U);
    EXPECT_TRUE(capture->report.success);
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kLoading));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kWorking));
    EXPECT_TRUE(ContainsPhase(capture->phases, ImportPhase::kComplete));
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest, ScriptAssetReportCountersArePopulated)
  {
    constexpr auto kScriptSource = std::string_view { "return 5" };
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_report_counters_asset";
    const auto source_path = cooked_root / "input" / "counter_asset.luau";
    WriteText(source_path, kScriptSource);

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 1U);
    EXPECT_EQ(report.scripting_components_written, 0U);
    EXPECT_EQ(report.script_slots_written, 0U);
    EXPECT_EQ(report.script_params_written, 0U);
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    EmbeddedScriptImportWritesDescriptorAndScriptFiles)
  {
    using data::kNoResourceReference;
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptAssetFlags;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_embedded_success";
    const auto source_path = cooked_root / "input" / "hello.luau";
    WriteText(source_path, "return 7");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "hello.oscript";
    EXPECT_TRUE(std::filesystem::exists(descriptor_path));

    const auto inspection = LoadInspection(cooked_root);

    const auto files = inspection.Files();
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.kind == FileKind::kScriptsTable;
    }));
    EXPECT_TRUE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.kind == FileKind::kScriptsData;
    }));

    const auto assets = inspection.Assets();
    ASSERT_EQ(assets.size(), 1U);
    EXPECT_EQ(
      static_cast<AssetType>(assets.front().asset_type), AssetType::kScript);

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_NE(desc.source_resource_index, kNoResourceReference);
    EXPECT_EQ(desc.bytecode_resource_index, kNoResourceReference);
    EXPECT_EQ(desc.flags, ScriptAssetFlags::kNone);
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    ExternalScriptImportStoresExternalPathAndNoScriptFiles)
  {
    using data::kNoResourceReference;
    using data::loose_cooked::FileKind;
    using data::pak::scripting::ScriptAssetFlags;

    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_external_success";
    const auto source_path = cooked_root.parent_path() / "scripts"
      / "script_import_external_success" / "external.lua";
    WriteText(source_path, "print('hello')");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));

    EXPECT_TRUE(report.success);

    const auto descriptor_path = cooked_root / "Scripts" / "external.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));

    const auto inspection = LoadInspection(cooked_root);

    const auto files = inspection.Files();
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.kind == FileKind::kScriptsTable;
    }));
    EXPECT_FALSE(std::ranges::any_of(files, [](const auto& file) -> auto {
      return file.kind == FileKind::kScriptsData;
    }));

    const auto desc = ReadScriptDescriptor(descriptor_path);
    EXPECT_EQ(desc.source_resource_index, kNoResourceReference);
    EXPECT_EQ(desc.bytecode_resource_index, kNoResourceReference);
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

  NOLINT_TEST_F(ScriptAssetImportJobTest,
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
    WriteText(source_path_absolute, "print('relative')");

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

  NOLINT_TEST_F(ScriptAssetImportJobTest,
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
    WriteText(source_path_absolute, "print('relative cooked root')");

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
    ScriptAssetImportJobTest, CompileEnabledFailsWhenCompilerUnavailable)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_compile_unavailable";
    const auto source_path = cooked_root / "input" / "compile_me.luau";
    WriteText(source_path, "return 99");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.asset.compiler_unavailable"));
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    CompileEnabledEmbeddedWritesSourceAndBytecodeResources)
  {
    using data::kNoResourceReference;
    using data::loose_cooked::FileKind;
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
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_compile_success";
    const auto source_path = cooked_root / "input" / "compile_ok.luau";
    WriteText(source_path, "return 21");

    const auto report = SubmitAndWait(service.Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, true));
    ASSERT_HAS_VALUE(report);
    ASSERT_TRUE(report->success);

    const auto descriptor_path = cooked_root / "Scripts" / "compile_ok.oscript";
    ASSERT_TRUE(std::filesystem::exists(descriptor_path));
    const auto desc = ReadScriptDescriptor(descriptor_path);
    ASSERT_NE(desc.source_resource_index, kNoResourceReference);
    ASSERT_NE(desc.bytecode_resource_index, kNoResourceReference);

    const auto inspection = LoadInspection(cooked_root);
    const auto table_relpath
      = FindFileRelPathByKind(inspection, FileKind::kScriptsTable);
    ASSERT_HAS_VALUE(table_relpath) << "Expected table relpath to be present";
    const auto resources
      = ReadPackedRecords<ScriptResourceDesc>(cooked_root / *table_relpath);

    const auto& references = inspection.Assets().front().references;
    const auto source_binding = references.ResolveResource(
      desc.source_resource_index, data::ResourceKind::kScript);
    const auto bytecode_binding = references.ResolveResource(
      desc.bytecode_resource_index, data::ResourceKind::kScript);
    ASSERT_HAS_VALUE(source_binding);
    ASSERT_HAS_VALUE(*source_binding);
    ASSERT_HAS_VALUE(bytecode_binding);
    ASSERT_HAS_VALUE(*bytecode_binding);
    const auto source_index = (**source_binding).get();
    const auto bytecode_index = (**bytecode_binding).get();
    ASSERT_LT(source_index, resources.size());
    ASSERT_LT(bytecode_index, resources.size());
    EXPECT_EQ(resources.at(source_index).encoding, ScriptEncoding::kSource);
    EXPECT_EQ(resources.at(bytecode_index).encoding, ScriptEncoding::kBytecode);
  }

  NOLINT_TEST_F(
    ScriptAssetImportJobTest, CompileFailureReportsCompileDiagnostic)
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
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_compile_failure";
    const auto source_path = cooked_root / "input" / "compile_fail.luau";
    WriteText(source_path, "return 22");

    const auto report = SubmitAndWait(service.Service(),
      MakeScriptRequest(
        source_path, cooked_root, ScriptStorageMode::kEmbedded, true));
    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, "script.asset.compile_failed"));

    const auto descriptor_path
      = cooked_root / "Scripts" / "compile_fail.oscript";
    EXPECT_FALSE(std::filesystem::exists(descriptor_path));
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest, CompileWithExternalStorageIsRejected)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_invalid_combo";
    const auto source_path = cooked_root / "input" / "combo.luau";
    WriteText(source_path, "return 1");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(HasDiagnosticCode(
      report.diagnostics, "script.request.invalid_option_combo"));
  }

  NOLINT_TEST_F(
    ScriptAssetImportJobTest, ReimportOverwritesScriptDescriptorIdentity)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_reimport";
    const auto source_path = cooked_root / "input" / "reload.luau";
    WriteText(source_path, "return 'first'");

    const auto report_first = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kEmbedded, false));
    ASSERT_TRUE(report_first.success);

    const auto first_inspection = LoadInspection(cooked_root);
    const auto first_assets = first_inspection.Assets();
    ASSERT_EQ(first_assets.size(), 1U);
    ASSERT_EQ(static_cast<AssetType>(first_assets.front().asset_type),
      AssetType::kScript);
    const auto first_key = first_assets.front().key;

    WriteText(source_path, "return 'second'");

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
    ScriptAssetImportJobTest, EmbeddedScriptImportIsDeterministicForSameInputs)
  {
    const ScopedTempDir temp;
    const auto cooked_root_a = temp.Path() / "script_import_determinism_a";
    const auto cooked_root_b = temp.Path() / "script_import_determinism_b";
    const auto source_a = cooked_root_a / "input" / "stable.luau";
    const auto source_b = cooked_root_b / "input" / "stable.luau";
    constexpr auto kSourceText = std::string_view { "return 'stable'" };
    WriteText(source_a, kSourceText);
    WriteText(source_b, kSourceText);

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
      = ReadBytes(cooked_root_a / "Scripts" / "stable.oscript");
    const auto descriptor_b
      = ReadBytes(cooked_root_b / "Scripts" / "stable.oscript");
    EXPECT_EQ(descriptor_a, descriptor_b);
  }

  NOLINT_TEST_F(
    ScriptAssetImportJobTest, MissingSourceFileFailsWithAssetReadDiagnostic)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_import_missing_source_file";
    const auto missing_source = cooked_root / "input" / "does_not_exist.luau";

    const auto report = Submit(MakeScriptRequest(
      missing_source, cooked_root, ScriptStorageMode::kExternal, false));

    EXPECT_FALSE(report.success);
    EXPECT_TRUE(
      HasDiagnosticCode(report.diagnostics, "script.asset.source_read_failed"));
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest,
    DiagnosticsFieldsAreCompleteForScriptAssetFailures)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_diagnostics_fields_asset_failure";
    const auto source_path = cooked_root / "input" / "invalid_combo.luau";
    WriteText(source_path, "return 3");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));
    ASSERT_FALSE(report.success);
    ExpectDiagnosticFieldsComplete(report.diagnostics);
  }

  NOLINT_TEST_F(
    ScriptAssetImportJobTest, PackagingSummaryMatchesDiagnosticsForFailures)
  {
    const ScopedTempDir temp;
    const auto cooked_root
      = temp.Path() / "script_packaging_summary_asset_failure";
    const auto source_path = cooked_root / "input" / "summary_failure.luau";
    WriteText(source_path, "return 8");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, true));
    ASSERT_FALSE(report.success);
    ExpectPackagingSummaryMatchesDiagnostics(report);
  }

  NOLINT_TEST_F(ScriptAssetImportJobTest, ImportsStandaloneScriptWithoutSidecar)
  {
    const ScopedTempDir temp;
    const auto cooked_root = temp.Path() / "script_dispatch_script_only";
    const auto source_path = cooked_root / "input" / "dispatch_only.luau";
    WriteText(source_path, "return 11");

    const auto report = Submit(MakeScriptRequest(
      source_path, cooked_root, ScriptStorageMode::kExternal, false));
    ASSERT_TRUE(report.success);
    EXPECT_EQ(report.scripts_written, 1U);
  }

} // namespace

} // namespace oxygen::content::import::test
