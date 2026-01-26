//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <latch>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Detail/LooseCookedIndex.h>
#include <Oxygen/Content/Import/BufferImportTypes.h>
#include <Oxygen/Content/Import/IAsyncFileReader.h>
#include <Oxygen/Content/Import/IAsyncFileWriter.h>
#include <Oxygen/Content/Import/ImportDiagnostics.h>
#include <Oxygen/Content/Import/ImportReport.h>
#include <Oxygen/Content/Import/ImportRequest.h>
#include <Oxygen/Content/Import/Internal/Emitters/AssetEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/BufferEmitter.h>
#include <Oxygen/Content/Import/Internal/Emitters/TextureEmitter.h>
#include <Oxygen/Content/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Content/Import/Internal/ImportSession.h>
#include <Oxygen/Content/Import/Internal/LooseCookedIndexRegistry.h>
#include <Oxygen/Content/Import/Internal/ResourceTableRegistry.h>
#include <Oxygen/Content/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Content/Import/TextureImportTypes.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/ThreadPool.h>

namespace co = oxygen::co;
namespace import = oxygen::content::import;
using oxygen::observer_ptr;

namespace {

//! Test fixture for ImportSession tests.
class ImportSessionTest : public testing::Test {
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
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_import_session_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    thread_pool_.reset();
    table_registry_.reset();
    index_registry_.reset();
    writer_.reset();
    reader_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  //! Create a basic import request for testing.
  [[nodiscard]] auto MakeRequest(
    const std::string& source_name = "test.fbx") const -> ImportRequest
  {
    return ImportRequest {
      .source_path = test_dir_ / source_name,
      .cooked_root = test_dir_ / "cooked",
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
  std::filesystem::path test_dir_;
  // NOLINTEND(*-non-private-member-variables-in-classes)
};

//=== Construction Tests ===--------------------------------------------------//

//! Verify session constructs with valid request.
NOLINT_TEST_F(ImportSessionTest, Constructor_ValidRequest_Succeeds)
{
  // Arrange
  const auto request = MakeRequest();

  // Act
  const ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Assert
  EXPECT_EQ(session.Request().source_path, request.source_path);
  EXPECT_EQ(session.CookedRoot(), request.cooked_root.value());
}

//! Verify session uses source directory when cooked_root is not set.
NOLINT_TEST_F(ImportSessionTest, Constructor_NoExplicitCookedRoot_UsesSourceDir)
{
  // Arrange
  const ImportRequest request {
    .source_path = test_dir_ / "models" / "test.fbx",
  };

  // Act
  const ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Assert
  EXPECT_EQ(session.CookedRoot(), test_dir_ / "models");
}

//! Verify CookedWriter is accessible.
NOLINT_TEST_F(ImportSessionTest, CookedWriter_IsAccessible)
{
  // Arrange
  const auto request = MakeRequest();

  // Act
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Assert - just verify we can access it without crash
  const auto& writer = session.CookedWriter();
  (void)writer;
}

//=== Emitter Access Tests ===------------------------------------------------//

//! Verify emitter accessors create lazily and return stable instances.
NOLINT_TEST_F(ImportSessionTest, Emitters_LazyAccess_ReturnsStableInstances)
{
  // Arrange
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Act
  auto* tex_1 = &session.TextureEmitter();
  auto* tex_2 = &session.TextureEmitter();
  auto* buf_1 = &session.BufferEmitter();
  auto* buf_2 = &session.BufferEmitter();
  auto* asset_1 = &session.AssetEmitter();
  auto* asset_2 = &session.AssetEmitter();

  // Assert
  EXPECT_EQ(tex_1, tex_2);
  EXPECT_EQ(buf_1, buf_2);
  EXPECT_EQ(asset_1, asset_2);
}

//=== Diagnostics Tests ===---------------------------------------------------//

//! Verify adding a single diagnostic.
NOLINT_TEST_F(ImportSessionTest, AddDiagnostic_Single_AddsToList)
{
  // Arrange
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Act
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Test warning message",
  });

  // Assert
  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 1);
  if (diagnostics.size() != 1) {
    return;
  }
  EXPECT_EQ(diagnostics.at(0).severity, ImportSeverity::kWarning);
  EXPECT_EQ(diagnostics.at(0).code, "test.warning");
  EXPECT_EQ(diagnostics.at(0).message, "Test warning message");
}

//! Verify adding multiple diagnostics.
NOLINT_TEST_F(ImportSessionTest, AddDiagnostic_Multiple_AllAdded)
{
  // Arrange
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Act
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

  // Assert
  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 3);
}

//! Verify HasErrors returns false when no errors.
NOLINT_TEST_F(ImportSessionTest, HasErrors_NoErrors_ReturnsFalse)
{
  // Arrange
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

  // Act & Assert
  EXPECT_FALSE(session.HasErrors());
}

//! Verify HasErrors returns true when error added.
NOLINT_TEST_F(ImportSessionTest, HasErrors_ErrorAdded_ReturnsTrue)
{
  // Arrange
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));

  // Act
  session.AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.error",
    .message = "An error occurred",
  });

  // Assert
  EXPECT_TRUE(session.HasErrors());
}

//! Verify diagnostics can be added from multiple threads.
NOLINT_TEST_F(ImportSessionTest, AddDiagnostic_MultipleThreads_ThreadSafe)
{
  // Arrange
  const auto request = MakeRequest();
  ImportSession session(request, observer_ptr(reader_.get()),
    oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
    observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
    observer_ptr(index_registry_.get()));
  constexpr int kThreadCount = 4;
  constexpr int kDiagnosticsPerThread = 100;
  std::latch start_latch(kThreadCount);
  std::latch done_latch(kThreadCount);

  // Act - add diagnostics from multiple threads concurrently
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

  // Assert
  const auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), kThreadCount * kDiagnosticsPerThread);
}

//=== Finalization Tests ===--------------------------------------------------//

//! Verify Finalize returns success when no errors.
NOLINT_TEST_F(ImportSessionTest, Finalize_NoErrors_ReturnsSuccess)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
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

    // Act
    const ImportReport report = co_await session.Finalize();

    // Assert
    EXPECT_TRUE(report.success);
    EXPECT_EQ(report.cooked_root, request.cooked_root.value());
    EXPECT_EQ(report.diagnostics.size(), 1);
    co_return;
  });
}

//! Verify Finalize returns failure when errors exist.
NOLINT_TEST_F(ImportSessionTest, Finalize_HasErrors_ReturnsFailure)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
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

    // Act
    const ImportReport report = co_await session.Finalize();

    // Assert
    EXPECT_FALSE(report.success);
    EXPECT_FALSE(report.diagnostics.empty());
    co_return;
  });
}

//! Verify Finalize writes container index on success.
NOLINT_TEST_F(ImportSessionTest, Finalize_Success_WritesIndex)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    // Act
    const ImportReport report = co_await session.Finalize();

    // Assert
    EXPECT_TRUE(report.success);
    const auto index_path = request.cooked_root.value() / "container.index.bin";
    EXPECT_TRUE(std::filesystem::exists(index_path));
    co_return;
  });
}

//! Verify Finalize writes index and reports warning when errors exist.
NOLINT_TEST_F(ImportSessionTest, Finalize_HasErrors_WritesIndexWithWarning)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
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

    // Act
    const ImportReport report = co_await session.Finalize();

    // Assert
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

//! Verify Finalize waits for pending writes.
NOLINT_TEST_F(ImportSessionTest, Finalize_PendingWrites_WaitsForCompletion)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
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

    // Act
    (void)co_await session.Finalize();

    // Assert
    EXPECT_EQ(writer_->PendingCount(), 0);
    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / "test1.bin"));
    EXPECT_TRUE(
      std::filesystem::exists(request.cooked_root.value() / "test2.bin"));
    co_return;
  });
}

//! Verify Finalize includes diagnostics in report.
NOLINT_TEST_F(ImportSessionTest, Finalize_WithDiagnostics_IncludesInReport)
{
  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    const auto request = MakeRequest();
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

    // Act
    const ImportReport report = co_await session.Finalize();

    // Assert
    EXPECT_EQ(report.diagnostics.size(), 2);
    if (report.diagnostics.size() == 2) {
      EXPECT_EQ(report.diagnostics.at(0).code, "test.info");
      EXPECT_EQ(report.diagnostics.at(1).code, "test.warning");
    }
    co_return;
  });
}

//! Verify Finalize orchestrates emitters and writes a valid index.
NOLINT_TEST_F(ImportSessionTest, Finalize_WithEmitters_RegistersInIndex)
{
  using oxygen::data::loose_cooked::FileKind;

  // NOLINTNEXTLINE(*-avoid-capturing-lambda-coroutines)
  co::Run(*loop_, [&]() -> co::Co<> {
    // Arrange
    auto request = MakeRequest();
    std::filesystem::create_directories(request.cooked_root.value());
    ImportSession session(request, observer_ptr(reader_.get()),
      oxygen::observer_ptr<IAsyncFileWriter>(writer_.get()),
      observer_ptr(thread_pool_.get()), observer_ptr(table_registry_.get()),
      observer_ptr(index_registry_.get()));

    constexpr oxygen::data::AssetKey kKey {
      .guid = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 },
    };
    const auto descriptor_relpath
      = request.loose_cooked_layout.MaterialDescriptorRelPath("Wood");
    const auto virtual_path
      = request.loose_cooked_layout.MaterialVirtualPath("Wood");
    constexpr std::string_view kBytes = "abc";

    // Act
    const auto tex_idx
      = session.TextureEmitter().Emit(MakeTestTexturePayload(), "test_texture");
    const auto buf_idx
      = session.BufferEmitter().Emit(MakeTestBufferPayload(), "test_texture");
    session.AssetEmitter().Emit(kKey, oxygen::data::AssetType::kMaterial,
      virtual_path, descriptor_relpath,
      std::as_bytes(std::span(kBytes.data(), kBytes.size())));

    const auto& report = co_await session.Finalize();

    // Assert
    EXPECT_TRUE(report.success);
    EXPECT_EQ(tex_idx, 1);
    EXPECT_EQ(buf_idx, 0);

    const auto index_path = request.cooked_root.value() / "container.index.bin";
    const bool index_exists = std::filesystem::exists(index_path);
    EXPECT_TRUE(index_exists);
    if (!index_exists) {
      co_return;
    }

    const auto index
      = oxygen::content::detail::LooseCookedIndex::LoadFromFile(index_path);

    const auto textures_data = index.FindFileRelPath(FileKind::kTexturesData);
    const auto textures_table = index.FindFileRelPath(FileKind::kTexturesTable);
    EXPECT_TRUE(textures_data.has_value());
    EXPECT_TRUE(textures_table.has_value());
    if (textures_data.has_value() && textures_table.has_value()) {
      EXPECT_EQ(
        *textures_data, request.loose_cooked_layout.TexturesDataRelPath());
      EXPECT_EQ(
        *textures_table, request.loose_cooked_layout.TexturesTableRelPath());
    }

    const auto buffers_data = index.FindFileRelPath(FileKind::kBuffersData);
    const auto buffers_table = index.FindFileRelPath(FileKind::kBuffersTable);
    EXPECT_TRUE(buffers_data.has_value());
    EXPECT_TRUE(buffers_table.has_value());
    if (buffers_data.has_value() && buffers_table.has_value()) {
      EXPECT_EQ(
        *buffers_data, request.loose_cooked_layout.BuffersDataRelPath());
      EXPECT_EQ(
        *buffers_table, request.loose_cooked_layout.BuffersTableRelPath());
    }

    const auto found_rel = index.FindDescriptorRelPath(kKey);
    const auto found_path = index.FindVirtualPath(kKey);
    EXPECT_TRUE(found_rel.has_value());
    EXPECT_TRUE(found_path.has_value());
    if (found_rel.has_value() && found_path.has_value()) {
      EXPECT_EQ(*found_rel, descriptor_relpath);
      EXPECT_EQ(*found_path, virtual_path);
    }

    co_return;
  });
}

} // namespace
