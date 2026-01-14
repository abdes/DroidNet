# Async Import Pipeline - Test Plan

**Status:** Approved
**Date:** 2026-01-14

---

## Overview

This document specifies test cases for the async import pipeline. Tests are
organized by phase and component, with clear naming conventions and coverage
requirements.

---

## Test File Naming Convention

```
{Component}_{TestType}_test.cpp

Examples:
  ImportEventLoop_basic_test.cpp
  AsyncImportService_threading_test.cpp
  TexturePipeline_integration_test.cpp
```

---

## Test Fixtures

### Base Fixtures

```cpp
// Common base for async tests
class AsyncTestFixture : public ::testing::Test {
protected:
  void SetUp() override {
    event_loop_ = std::make_unique<ImportEventLoop>();
    thread_pool_ = std::make_shared<co::ThreadPool>(*event_loop_, 4);
  }

  void TearDown() override {
    thread_pool_.reset();
    event_loop_.reset();
  }

  template <typename Awaitable>
  auto RunAsync(Awaitable&& awaitable) -> decltype(auto) {
    return co::Run(*event_loop_, std::forward<Awaitable>(awaitable));
  }

  std::unique_ptr<ImportEventLoop> event_loop_;
  std::shared_ptr<co::ThreadPool> thread_pool_;
};

// Fixture with temporary directory for file I/O tests
class FileIOTestFixture : public AsyncTestFixture {
protected:
  void SetUp() override {
    AsyncTestFixture::SetUp();
    temp_dir_ = std::filesystem::temp_directory_path() / "oxygen_test_XXXXXX";
    std::filesystem::create_directories(temp_dir_);
  }

  void TearDown() override {
    std::filesystem::remove_all(temp_dir_);
    AsyncTestFixture::TearDown();
  }

  auto CreateTestFile(std::string_view name, std::span<const std::byte> data)
    -> std::filesystem::path;

  std::filesystem::path temp_dir_;
};
```

---

## Phase 1: Foundation Tests

### ImportEventLoop_basic_test.cpp

```cpp
namespace {

class ImportEventLoopBasicTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify event loop runs and stops correctly.
TEST_F(ImportEventLoopBasicTest, RunAndStop_Succeeds) {
  // Arrange
  std::atomic<bool> callback_ran{false};

  // Act
  loop_.Post([&]() {
    callback_ran = true;
    loop_.Stop();
  });
  loop_.Run();

  // Assert
  EXPECT_TRUE(callback_ran);
}

//! Verify multiple callbacks execute in order.
TEST_F(ImportEventLoopBasicTest, Post_MultipleCallbacks_ExecuteInOrder) {
  // Arrange
  std::vector<int> order;

  // Act
  loop_.Post([&]() { order.push_back(1); });
  loop_.Post([&]() { order.push_back(2); });
  loop_.Post([&]() { order.push_back(3); loop_.Stop(); });
  loop_.Run();

  // Assert
  EXPECT_THAT(order, ::testing::ElementsAre(1, 2, 3));
}

//! Verify Stop() can be called from non-event-loop thread.
TEST_F(ImportEventLoopBasicTest, Stop_FromOtherThread_Succeeds) {
  // Arrange
  std::thread stopper([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    loop_.Stop();
  });

  // Act & Assert (should not hang)
  loop_.Run();
  stopper.join();
}

} // namespace
```

### ThreadNotification_basic_test.cpp

```cpp
namespace {

class ThreadNotificationTest : public ::testing::Test {
protected:
  ImportEventLoop loop_;
};

//! Verify ThreadNotification posts callback to event loop.
TEST_F(ThreadNotificationTest, Post_FromWorkerThread_ExecutesOnEventLoop) {
  // Arrange
  co::ThreadPool pool(loop_, 2);
  std::atomic<std::thread::id> callback_thread_id;
  std::atomic<bool> done{false};

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    co_await pool.Run([&]() {
      // This runs on worker thread
    });
    // This runs on event loop thread
    callback_thread_id = std::this_thread::get_id();
    done = true;
    co_return;
  }());

  // Assert
  EXPECT_TRUE(done);
  // Callback ran on the main thread that called Run()
}

//! Verify ThreadPool result is delivered correctly.
TEST_F(ThreadNotificationTest, ThreadPool_ReturnsResult_ViaNotification) {
  // Arrange
  co::ThreadPool pool(loop_, 2);
  int result = 0;

  // Act
  co::Run(loop_, [&]() -> co::Co<> {
    result = co_await pool.Run([]() { return 42 * 2; });
    co_return;
  }());

  // Assert
  EXPECT_EQ(result, 84);
}

//! Verify ThreadPool exception is propagated correctly.
TEST_F(ThreadNotificationTest, ThreadPool_ThrowsException_PropagatesViaNotification) {
  // Arrange
  co::ThreadPool pool(loop_, 2);

  // Act & Assert
  EXPECT_THROW(
    co::Run(loop_, [&]() -> co::Co<> {
      co_await pool.Run([]() -> int {
        throw std::runtime_error("test error");
      });
      co_return;
    }()),
    std::runtime_error
  );
}

} // namespace
```

---

## Phase 2: Async File I/O Tests

### ThreadPoolFileReader_basic_test.cpp

```cpp
namespace {

class ThreadPoolFileReaderBasicTest : public FileIOTestFixture {
protected:
  void SetUp() override {
    FileIOTestFixture::SetUp();
    reader_ = io::CreateAsyncFileReader({.thread_pool = thread_pool_});
  }

  std::unique_ptr<io::IAsyncFileReader> reader_;
};

//! Read existing file returns contents.
TEST_F(ThreadPoolFileReaderBasicTest, ReadFile_ExistingFile_ReturnsContents) {
  // Arrange
  const std::string content = "Hello, async I/O!";
  auto path = CreateTestFile("test.txt", as_bytes(span(content)));

  // Act
  auto result = RunAsync([&]() -> co::Co<io::Result<std::vector<std::byte>, io::FileErrorInfo>> {
    co_return co_await reader_->ReadFile(path);
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), content.size());
  EXPECT_EQ(std::memcmp(result->data(), content.data(), content.size()), 0);
}

//! Read non-existent file returns kNotFound.
TEST_F(ThreadPoolFileReaderBasicTest, ReadFile_NonExistent_ReturnsNotFound) {
  // Arrange
  auto path = temp_dir_ / "does_not_exist.txt";

  // Act
  auto result = RunAsync([&]() {
    return reader_->ReadFile(path);
  }());

  // Assert
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, io::FileError::kNotFound);
}

//! Read with offset skips bytes.
TEST_F(ThreadPoolFileReaderBasicTest, ReadFile_WithOffset_SkipsBytes) {
  // Arrange
  const std::string content = "0123456789";
  auto path = CreateTestFile("test.txt", as_bytes(span(content)));

  // Act
  auto result = RunAsync([&]() {
    return reader_->ReadFile(path, {.offset = 5});
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 5);
  std::string got(reinterpret_cast<const char*>(result->data()), result->size());
  EXPECT_EQ(got, "56789");
}

//! Read with max_bytes limits size.
TEST_F(ThreadPoolFileReaderBasicTest, ReadFile_WithMaxBytes_LimitsSize) {
  // Arrange
  const std::string content = "0123456789";
  auto path = CreateTestFile("test.txt", as_bytes(span(content)));

  // Act
  auto result = RunAsync([&]() {
    return reader_->ReadFile(path, {.max_bytes = 3});
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size(), 3);
  std::string got(reinterpret_cast<const char*>(result->data()), result->size());
  EXPECT_EQ(got, "012");
}

//! GetFileInfo returns correct size.
TEST_F(ThreadPoolFileReaderBasicTest, GetFileInfo_ExistingFile_ReturnsSize) {
  // Arrange
  const std::string content = "Hello, World!";
  auto path = CreateTestFile("test.txt", as_bytes(span(content)));

  // Act
  auto result = RunAsync([&]() {
    return reader_->GetFileInfo(path);
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->size, content.size());
  EXPECT_FALSE(result->is_directory);
}

//! Exists returns true for existing file.
TEST_F(ThreadPoolFileReaderBasicTest, Exists_ExistingFile_ReturnsTrue) {
  // Arrange
  auto path = CreateTestFile("test.txt", {});

  // Act
  auto result = RunAsync([&]() {
    return reader_->Exists(path);
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_TRUE(*result);
}

//! Exists returns false for non-existent file.
TEST_F(ThreadPoolFileReaderBasicTest, Exists_NonExistent_ReturnsFalse) {
  // Arrange
  auto path = temp_dir_ / "nope.txt";

  // Act
  auto result = RunAsync([&]() {
    return reader_->Exists(path);
  }());

  // Assert
  ASSERT_TRUE(result.has_value());
  EXPECT_FALSE(*result);
}

} // namespace
```

### ThreadPoolFileReader_cancellation_test.cpp

```cpp
namespace {

class ThreadPoolFileReaderCancellationTest : public FileIOTestFixture {
protected:
  // Large file to ensure cancellation can occur during read
  static constexpr size_t kLargeFileSize = 100 * 1024 * 1024;  // 100 MB
};

//! Cancellation during read returns kCancelled.
TEST_F(ThreadPoolFileReaderCancellationTest, ReadFile_Cancelled_ReturnsKCancelled) {
  // Arrange
  auto path = CreateLargeTestFile("large.bin", kLargeFileSize);
  auto reader = io::CreateAsyncFileReader({.thread_pool = thread_pool_});
  io::Result<std::vector<std::byte>, io::FileErrorInfo> read_result;

  // Act
  co::Run(*event_loop_, [&]() -> co::Co<> {
    OXCO_WITH_NURSERY(nursery) {
      // Start read task
      nursery.Start([&]() -> co::Co<> {
        read_result = co_await reader->ReadFile(path);
        co_return;
      });

      // Cancel after brief delay
      co_await SleepFor(*event_loop_, std::chrono::milliseconds(10));
      co_return co::kCancel;
    };
    co_return;
  }());

  // Assert - either completed or cancelled
  if (!read_result.has_value()) {
    EXPECT_EQ(read_result.error().code, io::FileError::kCancelled);
  }
  // If it completed before cancel, that's also acceptable
}

} // namespace
```

---

## Phase 3: AsyncImportService Tests

### AsyncImportService_lifecycle_test.cpp

```cpp
namespace {

//! Verify service starts and stops cleanly.
TEST(AsyncImportServiceLifecycleTest, ConstructDestruct_NoJobs_Succeeds) {
  // Arrange & Act
  auto service = std::make_unique<AsyncImportService>();

  // Assert - destructor should not hang or crash
  service.reset();
}

//! Verify shutdown with pending jobs completes.
TEST(AsyncImportServiceLifecycleTest, Shutdown_WithPendingJobs_Completes) {
  // Arrange
  AsyncImportService service;
  std::atomic<int> callbacks{0};

  for (int i = 0; i < 10; ++i) {
    service.SubmitImport(
      ImportRequest{.source_path = "fake.fbx"},
      [&](auto, auto) { ++callbacks; }
    );
  }

  // Act
  service.RequestShutdown();
  // Destructor blocks

  // Assert - either completed or cancelled, but all callbacks fired
  EXPECT_EQ(callbacks, 10);
}

} // namespace
```

### AsyncImportService_threading_test.cpp

```cpp
namespace {

//! Verify concurrent submission from multiple threads.
TEST(AsyncImportServiceThreadingTest, SubmitImport_ConcurrentThreads_AllSucceed) {
  // Arrange
  AsyncImportService service;
  std::atomic<int> submitted{0};
  std::atomic<int> completed{0};
  constexpr int kThreads = 8;
  constexpr int kJobsPerThread = 100;

  // Act
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t) {
    threads.emplace_back([&]() {
      for (int j = 0; j < kJobsPerThread; ++j) {
        service.SubmitImport(
          ImportRequest{.source_path = "fake.fbx"},
          [&](auto, auto) { ++completed; }
        );
        ++submitted;
      }
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Wait for completion
  service.RequestShutdown();

  // Assert
  EXPECT_EQ(submitted, kThreads * kJobsPerThread);
  EXPECT_EQ(completed, kThreads * kJobsPerThread);
}

//! Verify callback invoked on submitter thread (when possible).
TEST(AsyncImportServiceThreadingTest, Callback_InvokedOnSubmitterThread) {
  // This test is more complex and requires caller thread tracking
  // Simplified version: just verify callback fires
  AsyncImportService service;
  std::promise<bool> done;

  service.SubmitImport(
    ImportRequest{.source_path = "fake.fbx"},
    [&](auto, auto report) {
      done.set_value(true);
    }
  );

  EXPECT_TRUE(done.get_future().wait_for(std::chrono::seconds(5))
              != std::future_status::timeout);
}

} // namespace
```

### AsyncImportService_cancellation_test.cpp

```cpp
namespace {

//! Cancel specific job returns true if pending.
TEST(AsyncImportServiceCancellationTest, CancelJob_PendingJob_ReturnsTrue) {
  // Arrange
  AsyncImportService service;
  std::atomic<bool> cancelled{false};

  auto id = service.SubmitImport(
    ImportRequest{.source_path = "large_model.fbx"},  // Would take time
    [&](auto, auto report) { cancelled = report.cancelled; },
    [&](auto) { cancelled = true; }  // Cancel callback
  );

  // Act
  bool result = service.CancelJob(id);

  // Assert
  EXPECT_TRUE(result);
  // Wait for cancel callback
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  EXPECT_TRUE(cancelled);
}

//! CancelAll cancels all pending jobs.
TEST(AsyncImportServiceCancellationTest, CancelAll_MultiplePending_AllCancelled) {
  // Arrange
  AsyncImportService service;
  std::atomic<int> cancelled_count{0};

  for (int i = 0; i < 10; ++i) {
    service.SubmitImport(
      ImportRequest{.source_path = "model.fbx"},
      [&](auto, auto report) { if (report.cancelled) ++cancelled_count; }
    );
  }

  // Act
  service.CancelAll();
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Assert
  EXPECT_EQ(cancelled_count, 10);
}

} // namespace
```

---

## Phase 4: AsyncImporter Tests

### AsyncImporter_lifecycle_test.cpp

```cpp
namespace {

class AsyncImporterLifecycleTest : public AsyncTestFixture {
protected:
  void SetUp() override {
    AsyncTestFixture::SetUp();
    file_reader_ = io::CreateAsyncFileReader({.thread_pool = thread_pool_});
    importer_ = std::make_unique<detail::AsyncImporter>(
      thread_pool_, file_reader_);
  }

  std::shared_ptr<io::IAsyncFileReader> file_reader_;
  std::unique_ptr<detail::AsyncImporter> importer_;
};

//! Activate and stop without jobs succeeds.
TEST_F(AsyncImporterLifecycleTest, ActivateStop_NoJobs_Succeeds) {
  // Arrange & Act
  RunAsync([&]() -> co::Co<> {
    OXCO_WITH_NURSERY(nursery) {
      co_await nursery.Start(&detail::AsyncImporter::ActivateAsync, importer_.get());
      importer_->Run();

      // Stop immediately
      importer_->Stop();
      co_return co::kJoin;
    };
    co_return;
  }());

  // Assert - no crash, clean exit
  EXPECT_FALSE(importer_->IsRunning());
}

} // namespace
```

### AsyncImporter_job_test.cpp

```cpp
namespace {

class AsyncImporterJobTest : public AsyncTestFixture {
  // Similar setup with test FBX files
};

//! Single job processes successfully.
TEST_F(AsyncImporterJobTest, SubmitJob_ValidFbx_ReturnsSuccess) {
  // Arrange
  auto request = ImportRequest{
    .source_path = GetTestAssetPath("cube.fbx"),
    .cooked_root = temp_dir_,
  };

  ImportReport report;

  // Act
  RunAsync([&]() -> co::Co<> {
    OXCO_WITH_NURSERY(nursery) {
      co_await nursery.Start(&detail::AsyncImporter::ActivateAsync, importer_.get());
      importer_->Run();

      detail::AsyncImporter::JobEntry job{
        .id = 1,
        .request = request,
      };

      report = co_await importer_->SubmitJob(std::move(job));
      importer_->Stop();
      co_return co::kJoin;
    };
    co_return;
  }());

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_GT(report.geometry_written, 0);
}

//! Per-job cancellation stops processing.
TEST_F(AsyncImporterJobTest, SubmitJob_Cancelled_ReturnsCancelled) {
  // Similar test triggering cancel event mid-job
}

} // namespace
```

---

## Phase 5: Texture Pipeline Tests

### TexturePipeline_worker_test.cpp

```cpp
namespace {

class TexturePipelineWorkerTest : public AsyncTestFixture {
protected:
  void SetUp() override {
    AsyncTestFixture::SetUp();
    file_reader_ = io::CreateAsyncFileReader({.thread_pool = thread_pool_});
    pipeline_ = std::make_unique<TexturePipeline>(
      thread_pool_, file_reader_,
      TexturePipeline::Config{.worker_count = 2});
  }

  std::shared_ptr<io::IAsyncFileReader> file_reader_;
  std::unique_ptr<TexturePipeline> pipeline_;
};

//! Single texture request produces result.
TEST_F(TexturePipelineWorkerTest, Enqueue_SingleTexture_ProducesResult) {
  // Arrange
  co::Channel<TextureWorkResult> results;

  // Act
  RunAsync([&]() -> co::Co<> {
    OXCO_WITH_NURSERY(nursery) {
      pipeline_->Activate(&nursery);
      pipeline_->Run();

      TextureWorkRequest request{
        .job_id = 1,
        .request_id = 0,
        .results_writer = &results.GetWriter(),
        .source_file_path = GetTestAssetPath("texture.png"),
        .config = emit::CookerConfig{},
      };

      co_await pipeline_->Enqueue(std::move(request));

      auto result = co_await results.Receive();
      EXPECT_TRUE(result.has_value());
      EXPECT_EQ(result->status, TextureWorkStatus::kSuccess);

      pipeline_->Stop();
      co_return co::kJoin;
    };
    co_return;
  }());
}

//! Multiple textures process in parallel.
TEST_F(TexturePipelineWorkerTest, Enqueue_MultipleTextures_ProcessInParallel) {
  // Measure timing to verify parallelism
  // N textures should take ~N/workers * single_texture_time
}

} // namespace
```

### TexturePipeline_backpressure_test.cpp

```cpp
namespace {

//! Bounded queue causes backpressure.
TEST_F(TexturePipelineWorkerTest, Enqueue_QueueFull_SendSuspends) {
  // Configure with small queue
  pipeline_ = std::make_unique<TexturePipeline>(
    thread_pool_, file_reader_,
    TexturePipeline::Config{
      .worker_count = 1,
      .work_queue_capacity = 2,
    });

  // Submit more than capacity
  // Verify Send() suspends until workers drain
}

} // namespace
```

### TextureCommit_dedup_test.cpp

```cpp
namespace {

//! Duplicate textures reuse existing index.
TEST(TextureCommitDedupTest, Commit_DuplicateSignature_ReusesIndex) {
  // Arrange
  emit::TextureEmissionState state;
  auto cooked1 = MakeTestCookedTexture("tex1", kTestPayload);
  auto cooked2 = MakeTestCookedTexture("tex2", kTestPayload);  // Same content

  // Act
  uint32_t idx1 = emit::CommitTexture(state, cooked1);
  uint32_t idx2 = emit::CommitTexture(state, cooked2);

  // Assert
  EXPECT_EQ(idx1, idx2);
  EXPECT_EQ(state.table.size(), 1);  // Only one entry
}

//! Different content creates new entry.
TEST(TextureCommitDedupTest, Commit_DifferentContent_CreatesNewEntry) {
  // Arrange
  emit::TextureEmissionState state;
  auto cooked1 = MakeTestCookedTexture("tex1", kPayload1);
  auto cooked2 = MakeTestCookedTexture("tex2", kPayload2);

  // Act
  uint32_t idx1 = emit::CommitTexture(state, cooked1);
  uint32_t idx2 = emit::CommitTexture(state, cooked2);

  // Assert
  EXPECT_NE(idx1, idx2);
  EXPECT_EQ(state.table.size(), 2);
}

} // namespace
```

---

## Phase 6: Integration Tests

### AsyncImport_fbx_integration_test.cpp

```cpp
namespace {

//! Import FBX with multiple textures.
TEST(AsyncImportFbxIntegrationTest, Import_SponzaLike_Succeeds) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> promise;

  // Act
  service.SubmitImport(
    ImportRequest{
      .source_path = GetTestAssetPath("sponza_subset.fbx"),
      .cooked_root = temp_dir_,
    },
    [&](auto, auto report) { promise.set_value(report); }
  );

  auto report = promise.get_future().get();

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_GT(report.textures_written, 5);
  EXPECT_GT(report.materials_written, 1);
  EXPECT_GT(report.geometry_written, 1);

  // Verify output is loadable
  auto pak = LoadPakFile(report.cooked_root / "index.pak");
  EXPECT_TRUE(pak.IsValid());
}

//! Import matches sync path output.
TEST(AsyncImportFbxIntegrationTest, Import_MatchesSyncOutput) {
  // Arrange
  auto request = ImportRequest{
    .source_path = GetTestAssetPath("cube_textured.fbx"),
  };

  // Act - run both paths
  AssetImporter sync_importer;
  auto sync_report = sync_importer.ImportToLooseCooked(request);

  AsyncImportService async_service;
  std::promise<ImportReport> promise;
  async_service.SubmitImport(request, [&](auto, auto r) { promise.set_value(r); });
  auto async_report = promise.get_future().get();

  // Assert - outputs match
  EXPECT_EQ(async_report.textures_written, sync_report.textures_written);
  EXPECT_EQ(async_report.materials_written, sync_report.materials_written);
  EXPECT_EQ(async_report.geometry_written, sync_report.geometry_written);

  // Binary comparison of key files
  EXPECT_TRUE(FilesIdentical(
    sync_report.cooked_root / "textures.data",
    async_report.cooked_root / "textures.data"
  ));
}

} // namespace
```

### AsyncImport_standalone_texture_test.cpp

```cpp
namespace {

//! Import standalone JPG texture.
TEST(AsyncImportStandaloneTextureTest, Import_JpgTexture_Succeeds) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> promise;

  // Act
  service.SubmitTextureImport(
    TextureImportRequest{
      .source_path = GetTestAssetPath("test_diffuse.jpg"),
      .output_path = temp_dir_ / "test_diffuse.cooked",
      .options = { .generate_mips = true, .compression = kBC7 },
    },
    [&](auto, auto report) { promise.set_value(report); }
  );

  auto report = promise.get_future().get();

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_TRUE(std::filesystem::exists(temp_dir_ / "test_diffuse.cooked"));
}

//! Import standalone HDRi environment map.
TEST(AsyncImportStandaloneTextureTest, Import_HdriTexture_Succeeds) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> promise;

  // Act
  service.SubmitTextureImport(
    TextureImportRequest{
      .source_path = GetTestAssetPath("environment.hdr"),
      .output_path = temp_dir_ / "environment.cooked",
      .options = { .is_hdri = true, .generate_mips = true },
    },
    [&](auto, auto report) { promise.set_value(report); }
  );

  auto report = promise.get_future().get();

  // Assert
  EXPECT_TRUE(report.success);
}

//! Import non-existent texture reports error.
TEST(AsyncImportStandaloneTextureTest, Import_NonExistent_ReportsError) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> promise;

  // Act
  service.SubmitTextureImport(
    TextureImportRequest{
      .source_path = "does_not_exist.png",
      .output_path = temp_dir_ / "out.cooked",
    },
    [&](auto, auto report) { promise.set_value(report); }
  );

  auto report = promise.get_future().get();

  // Assert
  EXPECT_FALSE(report.success);
  EXPECT_FALSE(report.diagnostics.empty());
  EXPECT_EQ(report.diagnostics[0].severity, ImportSeverity::kError);
}

} // namespace
```

### AsyncImport_progress_test.cpp

```cpp
namespace {

//! Progress callback fires with correct phase and counts.
TEST(AsyncImportProgressTest, ProgressCallback_FiresDuringTextures) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> completion;
  std::vector<ImportProgress> progress_updates;
  std::mutex progress_mutex;

  // Act
  service.SubmitImport(
    ImportRequest{
      .source_path = GetTestAssetPath("multi_texture.fbx"), // 5 textures
    },
    [&](auto, auto report) { completion.set_value(report); },
    [&](const ImportProgress& p) {
      std::lock_guard lock(progress_mutex);
      progress_updates.push_back(p);
    }
  );

  auto report = completion.get_future().get();

  // Assert
  EXPECT_TRUE(report.success);
  EXPECT_FALSE(progress_updates.empty());

  // Verify texture phase had multiple updates
  auto texture_updates = std::count_if(
    progress_updates.begin(), progress_updates.end(),
    [](const auto& p) { return p.phase == ImportProgress::Phase::kTextures; });
  EXPECT_GE(texture_updates, 3);

  // Verify items_completed increments
  uint32_t max_completed = 0;
  for (const auto& p : progress_updates) {
    if (p.phase == ImportProgress::Phase::kTextures) {
      EXPECT_GE(p.items_completed, max_completed);
      max_completed = p.items_completed;
    }
  }
  EXPECT_EQ(max_completed, 5); // All 5 textures completed
}

//! Progress reports incremental diagnostics.
TEST(AsyncImportProgressTest, ProgressCallback_IncludesDiagnostics) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> completion;
  std::vector<ImportDiagnostic> incremental_diagnostics;

  // Act
  service.SubmitImport(
    ImportRequest{
      .source_path = GetTestAssetPath("missing_textures.fbx"), // References non-existent textures
    },
    [&](auto, auto report) { completion.set_value(report); },
    [&](const ImportProgress& p) {
      for (const auto& d : p.new_diagnostics) {
        incremental_diagnostics.push_back(d);
      }
    }
  );

  auto report = completion.get_future().get();

  // Assert - diagnostics appeared during progress, not just at end
  EXPECT_FALSE(incremental_diagnostics.empty());
  auto warnings = std::count_if(
    incremental_diagnostics.begin(), incremental_diagnostics.end(),
    [](const auto& d) { return d.severity == ImportSeverity::kWarning; });
  EXPECT_GT(warnings, 0);
}

} // namespace
```

### AsyncImport_cancellation_ui_test.cpp

```cpp
namespace {

//! Cancel job during texture phase, callback receives cancelled report.
TEST(AsyncImportCancellationTest, CancelJob_DuringTextures_ReportsCancelled) {
  // Arrange
  AsyncImportService service;
  std::promise<ImportReport> completion;
  std::promise<void> texture_started;
  bool texture_started_set = false;

  // Act
  auto job_id = service.SubmitImport(
    ImportRequest{
      .source_path = GetTestAssetPath("large_scene.fbx"), // Many textures
    },
    [&](auto, auto report) { completion.set_value(report); },
    [&](const ImportProgress& p) {
      // Cancel once textures start
      if (p.phase == ImportProgress::Phase::kTextures &&
          p.items_completed >= 1 && !texture_started_set) {
        texture_started_set = true;
        texture_started.set_value();
      }
    }
  );

  // Wait for textures to start, then cancel
  texture_started.get_future().wait();
  bool cancelled = service.CancelJob(job_id);

  auto report = completion.get_future().get();

  // Assert
  EXPECT_TRUE(cancelled);
  EXPECT_FALSE(report.success);
  EXPECT_TRUE(report.cancelled);
}

//! Cancel all jobs during multi-job import.
TEST(AsyncImportCancellationTest, CancelAll_MultipleJobs_AllReportCancelled) {
  // Arrange
  AsyncImportService service;
  std::atomic<int> completed_count{0};
  std::atomic<int> cancelled_count{0};

  // Submit multiple jobs
  for (int i = 0; i < 5; ++i) {
    service.SubmitImport(
      ImportRequest{
        .source_path = GetTestAssetPath(fmt::format("scene_{}.fbx", i)),
      },
      [&](auto, auto report) {
        ++completed_count;
        if (report.cancelled) ++cancelled_count;
      }
    );
  }

  // Act - cancel all after brief delay
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  service.CancelAll();

  // Wait for all completions
  while (completed_count < 5) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // Assert - some jobs were cancelled
  EXPECT_GT(cancelled_count, 0);
}

} // namespace
```

---

## Test Coverage Requirements

| Component               | Line Coverage | Branch Coverage |
|-------------------------|---------------|-----------------|
| ImportEventLoop         | 90%           | 85%             |
| ThreadPoolFileReader    | 90%           | 85%             |
| AsyncImportService      | 85%           | 80%             |
| AsyncImporter           | 85%           | 80%             |
| TexturePipeline         | 85%           | 80%             |
| CommitTexture           | 95%           | 90%             |

---

## Performance Benchmarks

### Texture Parallelism Benchmark

```cpp
BENCHMARK(TexturePipeline_Sequential) {
  // Process N textures with 1 worker
}

BENCHMARK(TexturePipeline_Parallel_4Workers) {
  // Process N textures with 4 workers
}

// Expected: 4 workers ~3.5x faster than 1 (not linear due to overhead)
```

### Import Throughput Benchmark

```cpp
BENCHMARK(AsyncImport_SmallFbx) {
  // 1 mesh, 2 textures
}

BENCHMARK(AsyncImport_LargeFbx) {
  // 100 meshes, 50 textures
}

// Track import time / texture count ratio
```

---

## See Also

- [async_import_implementation_plan.md](async_import_implementation_plan.md)
- [async_import_pipeline_v2.md](async_import_pipeline_v2.md)
