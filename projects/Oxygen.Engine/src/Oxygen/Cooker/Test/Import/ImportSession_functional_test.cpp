//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/ImportSession.cpp

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Cooker/Import/BufferImportTypes.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportOptions.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/ImportSession.h>
#include <Oxygen/Cooker/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Cooker/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Cooker/Import/TextureImportTypes.h>
#include <Oxygen/Cooker/Loose/Inspection.h>
#include <Oxygen/Cooker/Test/Support/DescriptorFixtures.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/Testing/GTest.h>

namespace co = oxygen::co;
namespace import = oxygen::content::import;
using oxygen::observer_ptr;

namespace {

//! Test fixture for ImportSession tests.
class ImportSessionTest : public oxygen::cooker::test::TempDirTest {
protected:
  using CookedBufferPayload = import::CookedBufferPayload;
  using CookedTexturePayload = import::CookedTexturePayload;
  using FileWriter = import::WindowsFileWriter;
  using IAsyncFileReader = import::IAsyncFileReader;
  using IAsyncFileWriter = import::IAsyncFileWriter;
  using ImportDiagnostic = import::ImportDiagnostic;
  using ImportEventLoop = import::ImportEventLoop;
  using ImportReport = import::ImportReport;
  using ImportRequest = import::ImportRequest;
  using ImportSession = import::ImportSession;
  using ImportSeverity = import::ImportSeverity;
  using LooseCookedIndexRegistry = import::LooseCookedIndexRegistry;
  using ResourceTableRegistry = import::ResourceTableRegistry;
  using ThreadPool = co::ThreadPool;
  using WriteOptions = import::WriteOptions;

  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    reader_ = CreateAsyncFileReader(*loop_);
    writer_ = std::make_unique<FileWriter>(*loop_);
    table_registry_ = std::make_unique<ResourceTableRegistry>(*writer_);
    index_registry_ = std::make_unique<LooseCookedIndexRegistry>();
    thread_pool_ = std::make_unique<ThreadPool>(*loop_, 1);
  }

  auto TearDown() -> void override
  {
    thread_pool_.reset();
    table_registry_.reset();
    index_registry_.reset();
    writer_.reset();
    reader_.reset();
    loop_.reset();
  }

  //! Create a basic import request for testing.
  [[nodiscard]] auto MakeRequest(
    const std::string& source_name = "test.fbx") const -> ImportRequest
  {
    return ImportRequest {
      .source_path = TempDir() / source_name,
      .cooked_root = TempDir() / "cooked",
    };
  }

  static auto MakeTestTexturePayload() -> CookedTexturePayload
  {
    CookedTexturePayload payload;
    constexpr uint32_t kWidth = 8;
    constexpr uint32_t kHeight = 8;
    constexpr uint16_t kMipLevels = 1;
    constexpr uint16_t kDepth = 1;
    constexpr uint16_t kArrayLayers = 1;
    constexpr uint64_t kContentHash = 0x12345678ABCDEF00ULL;
    constexpr size_t kPayloadBytes = 512;
    constexpr auto kFillByte = std::byte { 0x5A };

    payload.desc.width = kWidth;
    payload.desc.height = kHeight;
    payload.desc.mip_levels = kMipLevels;
    payload.desc.depth = kDepth;
    payload.desc.array_layers = kArrayLayers;
    payload.desc.texture_type = oxygen::TextureType::kTexture2D;
    payload.desc.format = oxygen::Format::kBC7UNorm;
    payload.desc.content_hash = kContentHash;

    payload.payload.resize(kPayloadBytes, kFillByte);
    return payload;
  }

  static auto MakeTestBufferPayload() -> CookedBufferPayload
  {
    CookedBufferPayload payload;
    constexpr uint32_t kAlignment = 16;
    constexpr uint32_t kUsageFlags = 0x01;
    constexpr uint32_t kElementStride = 16;
    constexpr uint32_t kElementFormat = 0;
    constexpr uint32_t kContentHash = 0xDEADBEEF;
    constexpr size_t kBufferBytes = 256;
    constexpr auto kFillByte = std::byte { 0x3C };

    payload.alignment = kAlignment;
    payload.usage_flags = kUsageFlags;
    payload.element_stride = kElementStride;
    payload.element_format = kElementFormat;
    payload.content_hash = kContentHash;
    payload.data.resize(kBufferBytes, kFillByte);
    return payload;
  }

  // NOLINTBEGIN(*-non-private-member-variables-in-classes)
  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<IAsyncFileReader> reader_;
  std::unique_ptr<FileWriter> writer_;
  std::unique_ptr<ResourceTableRegistry> table_registry_;
  std::unique_ptr<LooseCookedIndexRegistry> index_registry_;
  std::unique_ptr<ThreadPool> thread_pool_;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

//=== Construction Tests ===--------------------------------------------------//

NOLINT_TEST_F(ImportSessionTest, ConstructorValidRequestSucceeds)
{
  const auto request = MakeRequest();

  const ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  EXPECT_EQ(session.Request().source_path, request.source_path);
  ASSERT_TRUE(request.cooked_root.has_value())
    << "The fixture requires a cooked root";
  EXPECT_EQ(session.CookedRoot(), request.cooked_root.value());
}

NOLINT_TEST_F(ImportSessionTest, ConstructorNoExplicitCookedRootUsesSourceDir)
{
  const ImportRequest request {
    .source_path = TempDir() / "models" / "test.fbx",
  };

  const ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  EXPECT_EQ(session.CookedRoot(), TempDir() / "models");
}

NOLINT_TEST_F(ImportSessionTest, CookedWriterIsAccessible)
{
  const auto request = MakeRequest();

  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Just verify we can access it without crash
  const auto& writer = session.CookedWriter();
  (void)writer;
}

//=== Emitter Access Tests ===------------------------------------------------//

NOLINT_TEST_F(ImportSessionTest, EmittersLazyAccessReturnsStableInstances)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  auto* tex_1 = &session.TextureEmitter();
  auto* tex_2 = &session.TextureEmitter();
  auto* buf_1 = &session.BufferEmitter();
  auto* buf_2 = &session.BufferEmitter();
  auto* asset_1 = &session.AssetEmitter();
  auto* asset_2 = &session.AssetEmitter();

  EXPECT_EQ(tex_1, tex_2);
  EXPECT_EQ(buf_1, buf_2);
  EXPECT_EQ(asset_1, asset_2);
}

//=== Diagnostics Tests ===---------------------------------------------------//

NOLINT_TEST_F(ImportSessionTest, AddDiagnosticSingleAddsToList)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Test warning message",
  });

  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 1);
  if (diagnostics.size() != 1) {
    return;
  }
  EXPECT_EQ(diagnostics.at(0).severity, ImportSeverity::kWarning);
  EXPECT_EQ(diagnostics.at(0).code, "test.warning");
  EXPECT_EQ(diagnostics.at(0).message, "Test warning message");
}

NOLINT_TEST_F(ImportSessionTest, AddDiagnosticMultipleAllAdded)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  session.AddDiagnostic({
    .severity = ImportSeverity::kInfo,
    .code = "test.info",
    .message = "Info message",
  });
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Warning message",
  });
  session.AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.error",
    .message = "Error message",
  });

  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 3);
}

NOLINT_TEST_F(ImportSessionTest, AddDiagnosticDuplicateSuppressed)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  const auto diagnostic = ImportDiagnostic {
    .severity = ImportSeverity::kWarning,
    .code = "test.duplicate",
    .message = "Duplicate warning",
    .source_path = "same.source",
    .object_path = "same.object",
  };

  session.AddDiagnostic(diagnostic);
  session.AddDiagnostic(diagnostic);

  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 1);
  if (diagnostics.size() != 1) {
    return;
  }
  EXPECT_EQ(diagnostics.at(0).code, "test.duplicate");
}

NOLINT_TEST_F(ImportSessionTest, HasErrorsNoErrorsReturnsFalse)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Just a warning",
  });

  EXPECT_FALSE(session.HasErrors());
}

NOLINT_TEST_F(ImportSessionTest, HasErrorsErrorAddedReturnsTrue)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  session.AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.error",
    .message = "An error occurred",
  });

  EXPECT_TRUE(session.HasErrors());
}

NOLINT_TEST_F(
  ImportSessionTest, BufferEmitterCollisionDiagnosticFlowsIntoSessionReport)
{
  auto request = MakeRequest();
  request.options.with_content_hashing = false;
  request.options.dedup_collision_policy
    = import::DedupCollisionPolicy::kWarnKeepFirst;
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  co::Run(*loop_, [&]() -> co::Co<> {
    auto first = MakeTestBufferPayload();
    auto second = MakeTestBufferPayload();
    second.content_hash = 0;
    if (!second.data.empty()) {
      second.data.at(0) ^= std::byte { 0xFF };
    }
    first.content_hash = 0;

    (void)session.BufferEmitter().Emit(std::move(first), "same_salt");
    (void)session.BufferEmitter().Emit(std::move(second), "same_salt");
    (void)co_await session.BufferEmitter().Finalize();
    co_return;
  });

  const auto diagnostics = session.Diagnostics();
  const auto has_collision
    = std::ranges::any_of(diagnostics, [](const ImportDiagnostic& d) {
        return d.code == "import.dedup_collision.buffer";
      });
  EXPECT_TRUE(has_collision);
}

NOLINT_TEST_F(
  ImportSessionTest, FinalizePackagingSummaryReportsDiagnosticsAndCollisions)
{
  auto request = MakeRequest();
  request.options.with_content_hashing = false;
  request.options.dedup_collision_policy
    = import::DedupCollisionPolicy::kWarnKeepFirst;
  ASSERT_TRUE(request.cooked_root.has_value())
    << "The fixture requires a cooked root";
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  session.AddDiagnostic({
    .severity = ImportSeverity::kInfo,
    .code = "test.info",
    .message = "info",
  });
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warn",
    .message = "warn",
  });

  co::Run(*loop_, [&]() -> co::Co<> {
    auto first = MakeTestBufferPayload();
    auto second = MakeTestBufferPayload();
    first.content_hash = 0;
    second.content_hash = 0;
    if (!second.data.empty()) {
      second.data.at(0) ^= std::byte { 0xAA };
    }
    (void)session.BufferEmitter().Emit(std::move(first), "same_salt");
    (void)session.BufferEmitter().Emit(std::move(second), "same_salt");

    const auto report = co_await session.Finalize();

    EXPECT_EQ(report.packaging.diagnostics_info, 1U);
    EXPECT_GE(report.packaging.diagnostics_warning, 1U);
    EXPECT_EQ(report.packaging.diagnostics_error, 0U);
    EXPECT_EQ(report.packaging.buffer_dedup_collisions, 1U);
    EXPECT_EQ(report.packaging.texture_dedup_collisions, 0U);
    EXPECT_TRUE(report.packaging.index_written);
    EXPECT_EQ(report.packaging.outputs_written,
      static_cast<uint32_t>(report.outputs.size()));
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, AddDiagnosticMultipleThreadsThreadSafe)
{
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));
  constexpr int kThreadCount = 4;
  constexpr int kDiagnosticsPerThread = 100;
  std::latch start_latch(kThreadCount);
  std::latch done_latch(kThreadCount);

  // Add diagnostics from multiple threads concurrently
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() -> void {
      start_latch.arrive_and_wait();
      for (int i = 0; i < kDiagnosticsPerThread; ++i) {
        session.AddDiagnostic({
          .severity = ImportSeverity::kInfo,
          .code = "thread." + std::to_string(t) + "." + std::to_string(i),
          .message = "Thread message",
        });
      }
      done_latch.count_down();
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), kThreadCount * kDiagnosticsPerThread);
}

//=== Finalization Tests ===--------------------------------------------------//

NOLINT_TEST_F(ImportSessionTest, FinalizeNoErrorsReturnsSuccess)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    session.AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code = "test.warning",
      .message = "Just a warning",
    });

    const ImportReport report = co_await session.Finalize();

    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.cooked_root, request.cooked_root.value());
    EXPECT_EQ(report.diagnostics.size(), 1);
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeHasErrorsReturnsFailure)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "test.error",
      .message = "An error occurred",
    });

    const ImportReport report = co_await session.Finalize();

    EXPECT_FALSE(report.success);
    EXPECT_FALSE(report.diagnostics.empty());
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeSuccessWritesIndex)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    const ImportReport report = co_await session.Finalize();

    EXPECT_TRUE(report.success);
    const auto index_path = request.cooked_root.value() / "container.index.bin";
    EXPECT_TRUE(std::filesystem::exists(index_path));
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeHasErrorsWritesIndexWithWarning)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    session.AddDiagnostic({
      .severity = ImportSeverity::kError,
      .code = "test.error",
      .message = "Fatal error",
    });

    const ImportReport report = co_await session.Finalize();

    EXPECT_FALSE(report.success);
    const auto has_index_warning = std::ranges::any_of(
      report.diagnostics, [](const ImportDiagnostic& diagnostic) -> bool {
        return diagnostic.code == "import.index_written_with_errors";
      });
    EXPECT_TRUE(has_index_warning);
    const auto index_path = request.cooked_root.value() / "container.index.bin";
    EXPECT_TRUE(std::filesystem::exists(index_path));
    co_return;
  });
}

//! Participant diagnostics stay local while successful publication is shared.
NOLINT_TEST_F(ImportSessionTest, CohortPublicationPreservesParticipantFailures)
{
  const auto request = MakeRequest();
  ASSERT_TRUE(request.cooked_root.has_value())
    << "The fixture requires a cooked root";
  std::filesystem::create_directories(request.cooked_root.value());
  const auto make_session = [&] {
    return std::make_unique<ImportSession>(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));
  };
  const std::array sessions { make_session(), make_session(), make_session() };
  const std::array names { "first", "failed", "last" };
  oxygen::data::pak::render::MaterialAssetDesc descriptor {};
  descriptor.header.asset_type
    = static_cast<uint8_t>(oxygen::data::AssetType::kMaterial);
  descriptor.header.version = oxygen::data::pak::render::kMaterialAssetVersion;
  for (size_t index = 0; index < sessions.size(); ++index) {
    const auto relative = std::string(names.at(index)) + ".omat";
    const auto virtual_path = std::string("/Content/") + relative;
    sessions.at(index)->AssetEmitter().Emit(
      oxygen::data::AssetKey::FromVirtualPath(virtual_path),
      oxygen::data::AssetType::kMaterial, virtual_path, relative,
      std::as_bytes(std::span { &descriptor, 1 }), {});
  }
  sessions.at(1)->AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.participant_failed",
    .message = "A local failure does not invalidate other producers",
    .source_path = {},
    .object_path = {},
  });
  std::array<ImportReport, 3> reports {};
  co::Run(*loop_, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(tasks)
    {
      for (size_t index = 0; index < sessions.size(); ++index) {
        tasks.Start([&, index]() -> co::Co<> {
          reports.at(index) = co_await sessions.at(index)->Finalize();
          EXPECT_TRUE(std::filesystem::exists(
            request.cooked_root.value() / "container.index.bin"));
        });
      }
      co_return co::kJoin;
    };
  });
  EXPECT_TRUE(reports.at(0).success);
  EXPECT_FALSE(reports.at(1).success);
  EXPECT_TRUE(reports.at(2).success);
  EXPECT_EQ(reports.at(0).source_key, reports.at(1).source_key);
  EXPECT_EQ(reports.at(0).source_key, reports.at(2).source_key);
  EXPECT_EQ(
    std::ranges::count_if(reports,
      [](const auto& report) { return report.packaging.index_written; }),
    1);
}

NOLINT_TEST_F(ImportSessionTest, FinalizePendingWritesWaitsForCompletion)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    const std::string content = "test content";
    const auto data = std::as_bytes(std::span(content.data(), content.size()));
    writer_->WriteAsync(request.cooked_root.value() / "test1.bin", data,
      WriteOptions {}, nullptr);
    writer_->WriteAsync(request.cooked_root.value() / "test2.bin", data,
      WriteOptions {}, nullptr);

    (void)co_await session.Finalize();

    EXPECT_EQ(writer_->PendingCount(), 0);
    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / "test1.bin"));
    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / "test2.bin"));
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeWithDiagnosticsIncludesInReport)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    const auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    session.AddDiagnostic({
      .severity = ImportSeverity::kInfo,
      .code = "test.info",
      .message = "Info 1",
    });
    session.AddDiagnostic({
      .severity = ImportSeverity::kWarning,
      .code = "test.warning",
      .message = "Warning 1",
    });

    const ImportReport report = co_await session.Finalize();

    EXPECT_EQ(report.diagnostics.size(), 2);
    if (report.diagnostics.size() == 2) {
      EXPECT_EQ(report.diagnostics.at(0).code, "test.info");
      EXPECT_EQ(report.diagnostics.at(1).code, "test.warning");
    }
    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeWithEmittersRegistersInIndex)
{
  using oxygen::data::loose_cooked::FileKind;

  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    const auto kKey = oxygen::data::AssetKey::FromBytes(
      std::array<std::uint8_t, oxygen::data::AssetKey::kSizeBytes> {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 });
    const auto descriptor_relpath
      = request.loose_cooked_layout.MaterialDescriptorRelPath("Wood");
    const auto virtual_path
      = request.loose_cooked_layout.MaterialVirtualPath("Wood");
    const auto descriptor = oxygen::content::test::MaterialDescriptor("Wood");

    const auto tex_idx
      = session.TextureEmitter().Emit(MakeTestTexturePayload(), "test_texture");
    const auto buf_idx
      = session.BufferEmitter().Emit(MakeTestBufferPayload(), "test_texture");
    session.AssetEmitter().Emit(kKey, oxygen::data::AssetType::kMaterial,
      virtual_path, descriptor_relpath, descriptor.bytes,
      descriptor.references);

    const auto& report = co_await session.Finalize();

    EXPECT_TRUE(report.success);
    EXPECT_EQ(tex_idx, 1);
    EXPECT_EQ(buf_idx, 1);

    const auto index_path = request.cooked_root.value() / "container.index.bin";
    const bool index_exists = std::filesystem::exists(index_path);
    EXPECT_TRUE(index_exists);
    if (!index_exists) {
      co_return;
    }

    oxygen::content::lc::Inspection inspection;
    inspection.LoadFromFile(index_path);

    const auto find_file_relpath
      = [&inspection](const FileKind kind) -> std::optional<std::string> {
      const auto files = inspection.Files();
      const auto it
        = std::ranges::find(files, kind, &oxygen::content::lc::FileEntry::kind);
      if (it == files.end()) {
        return std::nullopt;
      }
      return it->relpath;
    };

    const auto textures_data = find_file_relpath(FileKind::kTexturesData);
    const auto textures_table = find_file_relpath(FileKind::kTexturesTable);
    EXPECT_TRUE(textures_data.has_value());
    EXPECT_TRUE(textures_table.has_value());
    if (textures_data.has_value() && textures_table.has_value()) {
      EXPECT_EQ(
        *textures_data, request.loose_cooked_layout.TexturesDataRelPath());
      EXPECT_EQ(
        *textures_table, request.loose_cooked_layout.TexturesTableRelPath());
    }

    const auto buffers_data = find_file_relpath(FileKind::kBuffersData);
    const auto buffers_table = find_file_relpath(FileKind::kBuffersTable);
    EXPECT_TRUE(buffers_data.has_value());
    EXPECT_TRUE(buffers_table.has_value());
    if (buffers_data.has_value() && buffers_table.has_value()) {
      EXPECT_EQ(
        *buffers_data, request.loose_cooked_layout.BuffersDataRelPath());
      EXPECT_EQ(
        *buffers_table, request.loose_cooked_layout.BuffersTableRelPath());
    }

    const auto assets = inspection.Assets();
    const auto asset
      = std::ranges::find(assets, kKey, &oxygen::content::lc::AssetEntry::key);
    EXPECT_TRUE(asset != assets.end());
    if (asset != assets.end()) {
      EXPECT_EQ(asset->descriptor_relpath, descriptor_relpath);
      EXPECT_EQ(asset->virtual_path, virtual_path);
    }

    co_return;
  });
}

NOLINT_TEST_F(ImportSessionTest, FinalizeIncludesResourceSidecarOutputs)
{
  co::Run(*loop_, [&]() -> co::Co<> {
    auto request = MakeRequest();
    if (!request.cooked_root.has_value()) {
      ADD_FAILURE() << "The fixture requires a cooked root";
      co_return;
    }
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    const auto texture_index
      = session.TextureEmitter().Emit(MakeTestTexturePayload(), "wood_albedo");
    const auto texture_desc
      = session.TextureEmitter().TryGetDescriptor(texture_index);
    EXPECT_TRUE(texture_desc.has_value());
    if (!texture_desc.has_value()) {
      co_return;
    }

    const auto buffer_index
      = session.BufferEmitter().Emit(MakeTestBufferPayload(), "mesh_vb");
    const auto buffer_desc
      = session.BufferEmitter().TryGetDescriptor(buffer_index);
    EXPECT_TRUE(buffer_desc.has_value());
    if (!buffer_desc.has_value()) {
      co_return;
    }

    const auto texture_rel = session.ResourceDescriptorEmitter().EmitTexture(
      "wood_albedo", "wood_albedo",
      oxygen::data::pak::core::ResourceIndexT { texture_index }, *texture_desc);
    const auto buffer_rel
      = session.ResourceDescriptorEmitter().EmitBuffer("mesh_vb", "mesh_vb",
        oxygen::data::pak::core::ResourceIndexT { buffer_index }, *buffer_desc);

    const auto report = co_await session.Finalize();
    EXPECT_TRUE(report.success);

    const auto has_output = [&](std::string_view relpath) {
      return std::ranges::any_of(
        report.outputs, [&](const import::ImportOutputRecord& output) {
          return output.path == relpath;
        });
    };
    EXPECT_TRUE(has_output(texture_rel));
    EXPECT_TRUE(has_output(buffer_rel));

    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / texture_rel));
    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / buffer_rel));
    co_return;
  });
}

} // namespace
