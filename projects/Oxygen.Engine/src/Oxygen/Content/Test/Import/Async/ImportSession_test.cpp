//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Import/Async/ImportSession.h>

#include <Oxygen/Content/Import/Async/ImportEventLoop.h>
#include <Oxygen/Content/Import/Async/WindowsFileWriter.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

#include <filesystem>
#include <fstream>
#include <latch>
#include <thread>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Test fixture for ImportSession tests.
class ImportSessionTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
    test_dir_
      = std::filesystem::temp_directory_path() / "oxygen_import_session_test";
    std::filesystem::create_directories(test_dir_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
    std::error_code ec;
    std::filesystem::remove_all(test_dir_, ec);
  }

  //! Create a basic import request for testing.
  auto MakeRequest(const std::string& source_name = "test.fbx") -> ImportRequest
  {
    return ImportRequest {
      .source_path = test_dir_ / source_name,
      .cooked_root = test_dir_ / "cooked",
    };
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
  std::filesystem::path test_dir_;
};

//=== Construction Tests ===--------------------------------------------------//

//! Verify session constructs with valid request.
NOLINT_TEST_F(ImportSessionTest, Constructor_ValidRequest_Succeeds)
{
  // Arrange
  auto request = MakeRequest();

  // Act
  ImportSession session(request, *writer_);

  // Assert
  EXPECT_EQ(session.Request().source_path, request.source_path);
  EXPECT_EQ(session.CookedRoot(), request.cooked_root.value());
}

//! Verify session uses source directory when cooked_root is not set.
NOLINT_TEST_F(ImportSessionTest, Constructor_NoExplicitCookedRoot_UsesSourceDir)
{
  // Arrange
  ImportRequest request {
    .source_path = test_dir_ / "models" / "test.fbx",
  };

  // Act
  ImportSession session(request, *writer_);

  // Assert
  EXPECT_EQ(session.CookedRoot(), test_dir_ / "models");
}

//! Verify CookedWriter is accessible.
NOLINT_TEST_F(ImportSessionTest, CookedWriter_IsAccessible)
{
  // Arrange
  auto request = MakeRequest();

  // Act
  ImportSession session(request, *writer_);

  // Assert - just verify we can access it without crash
  auto& writer = session.CookedWriter();
  (void)writer;
}

//=== Diagnostics Tests ===---------------------------------------------------//

//! Verify adding a single diagnostic.
NOLINT_TEST_F(ImportSessionTest, AddDiagnostic_Single_AddsToList)
{
  // Arrange
  auto request = MakeRequest();
  ImportSession session(request, *writer_);

  // Act
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Test warning message",
  });

  // Assert
  auto diagnostics = session.Diagnostics();
  ASSERT_EQ(diagnostics.size(), 1);
  EXPECT_EQ(diagnostics[0].severity, ImportSeverity::kWarning);
  EXPECT_EQ(diagnostics[0].code, "test.warning");
  EXPECT_EQ(diagnostics[0].message, "Test warning message");
}

//! Verify adding multiple diagnostics.
NOLINT_TEST_F(ImportSessionTest, AddDiagnostic_Multiple_AllAdded)
{
  // Arrange
  auto request = MakeRequest();
  ImportSession session(request, *writer_);

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
  auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), 3);
}

//! Verify HasErrors returns false when no errors.
NOLINT_TEST_F(ImportSessionTest, HasErrors_NoErrors_ReturnsFalse)
{
  // Arrange
  auto request = MakeRequest();
  ImportSession session(request, *writer_);
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
  auto request = MakeRequest();
  ImportSession session(request, *writer_);

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
  auto request = MakeRequest();
  ImportSession session(request, *writer_);
  constexpr int kThreadCount = 4;
  constexpr int kDiagnosticsPerThread = 100;
  std::latch start_latch(kThreadCount);
  std::latch done_latch(kThreadCount);

  // Act - add diagnostics from multiple threads concurrently
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreadCount; ++t) {
    threads.emplace_back([&, t]() {
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
  auto diagnostics = session.Diagnostics();
  EXPECT_EQ(diagnostics.size(), kThreadCount * kDiagnosticsPerThread);
}

//=== Finalization Tests ===--------------------------------------------------//

//! Verify Finalize returns success when no errors.
NOLINT_TEST_F(ImportSessionTest, Finalize_NoErrors_ReturnsSuccess)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

  // Add a warning (not an error)
  session.AddDiagnostic({
    .severity = ImportSeverity::kWarning,
    .code = "test.warning",
    .message = "Just a warning",
  });

  // Act
  ImportReport report;
  co::Run(*loop_, [&]() -> Co<> { report = co_await session.Finalize(); });

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_EQ(report.cooked_root, request.cooked_root.value());
  EXPECT_EQ(report.diagnostics.size(), 1);
}

//! Verify Finalize returns failure when errors exist.
NOLINT_TEST_F(ImportSessionTest, Finalize_HasErrors_ReturnsFailure)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

  session.AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.error",
    .message = "An error occurred",
  });

  // Act
  ImportReport report;
  co::Run(*loop_, [&]() -> Co<> { report = co_await session.Finalize(); });

  // Assert
  EXPECT_FALSE(report.success);
  EXPECT_FALSE(report.diagnostics.empty());
}

//! Verify Finalize writes container index on success.
NOLINT_TEST_F(ImportSessionTest, Finalize_Success_WritesIndex)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    auto report = co_await session.Finalize();
    EXPECT_TRUE(report.success);
  });

  // Assert
  auto index_path = request.cooked_root.value() / "container.index.bin";
  EXPECT_TRUE(std::filesystem::exists(index_path));
}

//! Verify Finalize skips index write when errors exist.
NOLINT_TEST_F(ImportSessionTest, Finalize_HasErrors_SkipsIndexWrite)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

  session.AddDiagnostic({
    .severity = ImportSeverity::kError,
    .code = "test.error",
    .message = "Fatal error",
  });

  // Act
  co::Run(*loop_, [&]() -> Co<> {
    auto report = co_await session.Finalize();
    EXPECT_FALSE(report.success);
  });

  // Assert - index should not be written
  auto index_path = request.cooked_root.value() / "container.index.bin";
  EXPECT_FALSE(std::filesystem::exists(index_path));
}

//! Verify Finalize waits for pending writes.
NOLINT_TEST_F(ImportSessionTest, Finalize_PendingWrites_WaitsForCompletion)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

  // Queue some async writes
  const std::string content = "test content";
  auto data = std::as_bytes(std::span(content.data(), content.size()));
  writer_->WriteAsync(
    request.cooked_root.value() / "test1.bin", data, WriteOptions {}, nullptr);
  writer_->WriteAsync(
    request.cooked_root.value() / "test2.bin", data, WriteOptions {}, nullptr);

  // Act
  ImportReport report;
  co::Run(*loop_, [&]() -> Co<> { report = co_await session.Finalize(); });

  // Assert - pending writes should be complete
  EXPECT_EQ(writer_->PendingCount(), 0);
  EXPECT_TRUE(
    std::filesystem::exists(request.cooked_root.value() / "test1.bin"));
  EXPECT_TRUE(
    std::filesystem::exists(request.cooked_root.value() / "test2.bin"));
}

//! Verify Finalize includes diagnostics in report.
NOLINT_TEST_F(ImportSessionTest, Finalize_WithDiagnostics_IncludesInReport)
{
  // Arrange
  auto request = MakeRequest();
  std::filesystem::create_directories(request.cooked_root.value());
  ImportSession session(request, *writer_);

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
  ImportReport report;
  co::Run(*loop_, [&]() -> Co<> { report = co_await session.Finalize(); });

  // Assert
  EXPECT_EQ(report.diagnostics.size(), 2);
  EXPECT_EQ(report.diagnostics[0].code, "test.info");
  EXPECT_EQ(report.diagnostics[1].code, "test.warning");
}

} // namespace
