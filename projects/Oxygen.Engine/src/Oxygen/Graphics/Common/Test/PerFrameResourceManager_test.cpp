//===----------------------------------------------------------------------===//
// Tests for PerFrameResourceManager
// Distributed under the 3-Clause BSD License.
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <string>

#include <Oxygen/Graphics/Common/Detail/PerFrameResourceManager.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::graphics::detail;
using ::testing::Test;
namespace frame = oxygen::frame;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

namespace {

//! Minimal test resource that exposes Release(), GetName(), and GetTypeName().
struct TestResource : public std::enable_shared_from_this<TestResource> {
  TestResource(std::string n)
    : name(std::move(n))
    , released(false)
  {
  }
  void Release() { released = true; }
  bool WasReleased() const { return released; }
  std::string_view GetName() const { return name; }
  std::string_view GetTypeName() const { return "TestResource"; }

  std::string name;
  std::atomic<bool> released { false };
};

//! Resource without Release() for shared_ptr path that relies on destructor.
struct NoReleaseResource {
  NoReleaseResource(std::atomic<int>* ctr)
    : counter(ctr)
  {
  }
  ~NoReleaseResource()
  {
    if (counter)
      ++(*counter);
  }
  std::atomic<int>* counter { nullptr };
};

} // anonymous namespace

// -----------------------------------------------------------------------------
// Fixture
// -----------------------------------------------------------------------------

namespace {

//! Fixture for PerFrameResourceManager tests.
class PerFrameResourceManagerTest : public Test {
protected:
  // Called once before any test in this fixture runs
  static void SetUpTestSuite()
  {
    loguru::g_stderr_verbosity = loguru::Verbosity_2;
  }

  void SetUp() override
  {
    manager = std::make_unique<PerFrameResourceManager>();
  }
  void TearDown() override { manager.reset(); }

  std::unique_ptr<PerFrameResourceManager> manager;
};

} // anonymous namespace

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

//! Verify that shared_ptr resources with a Release() method are invoked when
//! the frame slot cycles (OnBeginFrame called for that index).
TEST_F(PerFrameResourceManagerTest, SharedPtrWithRelease_IsReleasedOnFrameCycle)
{
  // Arrange
  auto res = std::make_shared<TestResource>("res1");
  manager->RegisterDeferredRelease(res);

  // Act - simulate starting the same frame index to trigger the release
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(res->WasReleased());
}

//! Verify that shared_ptr resources without Release() are reset and destructor
//! runs when the frame slot cycles.
TEST_F(PerFrameResourceManagerTest,
  SharedPtrWithoutRelease_DestructorRunsOnFrameCycle)
{
  // Arrange
  std::atomic<int> destructor_count { 0 };
  {
    auto res = std::make_shared<NoReleaseResource>(&destructor_count);
    manager->RegisterDeferredRelease(res);
  }

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_EQ(destructor_count.load(), 1);
}

//! Verify that raw pointer resources with Release() have Release invoked.
TEST_F(
  PerFrameResourceManagerTest, RawPointerWithRelease_IsReleasedOnFrameCycle)
{
  // Arrange
  auto res = new TestResource("raw");
  manager->RegisterDeferredRelease(res);

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(res->WasReleased());

  // cleanup
  delete res;
}

//! Verify RegisterDeferredAction is executed when the frame slot cycles.
TEST_F(PerFrameResourceManagerTest, RegisterDeferredAction_ExecutesOnFrameCycle)
{
  // Arrange
  std::atomic<bool> ran { false };
  manager->RegisterDeferredAction([&]() { ran.store(true); });

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(ran.load());
}

//! Verify ProcessAllDeferredReleases releases everything across all frames.
TEST_F(
  PerFrameResourceManagerTest, ProcessAllDeferredReleases_ReleasesAllFrames)
{
  // Arrange
  auto r0 = std::make_shared<TestResource>("r0");
  auto r1 = std::make_shared<TestResource>("r1");

  manager->RegisterDeferredRelease(r0);
  // Simulate frame switch
  manager->OnBeginFrame(frame::Slot { 1 });
  manager->RegisterDeferredRelease(r1);

  // Act
  manager->ProcessAllDeferredReleases();

  // Assert - both should be released
  EXPECT_TRUE(r0->WasReleased());
  EXPECT_TRUE(r1->WasReleased());
}

//! Verify OnRendererShutdown processes all deferred releases.
TEST_F(
  PerFrameResourceManagerTest, OnRendererShutdown_ProcessesAllDeferredReleases)
{
  // Arrange
  auto r0 = std::make_shared<TestResource>("r0");
  manager->RegisterDeferredRelease(r0);

  // Act
  manager->OnRendererShutdown();

  // Assert
  EXPECT_TRUE(r0->WasReleased());
}

//! Edge case: registering nullptr raw pointer should be safe (no crash).
TEST_F(PerFrameResourceManagerTest, RegisterNullRawPointer_DoesNotCrash)
{
  // Arrange
  TestResource* nullres = nullptr;

  // Act / Assert - should not throw or crash
  manager->RegisterDeferredRelease(nullres);
  manager->OnBeginFrame(frame::Slot { 0 });
  SUCCEED();
}

//! Edge case: multiple registrations from same frame are all executed.
TEST_F(PerFrameResourceManagerTest, MultipleRegistrations_AllExecuted)
{
  // Arrange
  auto a = std::make_shared<TestResource>("a");
  auto b = std::make_shared<TestResource>("b");
  manager->RegisterDeferredRelease(a);
  manager->RegisterDeferredRelease(b);

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(a->WasReleased());
  EXPECT_TRUE(b->WasReleased());
}

//! Verify that registered callbacks execute in the same order they were
//! enqueued for a single frame bucket.
TEST_F(PerFrameResourceManagerTest, ReleaseOrder_IsPreservedPerFrame)
{
  // Arrange
  std::vector<int> order;
  manager->RegisterDeferredAction([&]() { order.push_back(1); });
  manager->RegisterDeferredAction([&]() { order.push_back(2); });
  manager->RegisterDeferredAction([&]() { order.push_back(3); });

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], 1);
  EXPECT_EQ(order[1], 2);
  EXPECT_EQ(order[2], 3);
}

//! Verify that a shared_ptr with a custom deleter is invoked when the frame
//! slot cycles (custom deleter receives ownership and runs on release).
TEST_F(
  PerFrameResourceManagerTest, SharedPtrWithCustomDeleter_IsInvokedOnFrameCycle)
{
  // Arrange
  std::atomic<bool> deleter_ran { false };
  auto ptr = std::shared_ptr<int>(new int(42), [&](int* p) {
    delete p;
    deleter_ran.store(true);
  });

  manager->RegisterDeferredRelease(std::move(ptr));

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(deleter_ran.load());
}

//! Edge case: concurrent registrations do not crash (basic smoke test).
TEST_F(PerFrameResourceManagerTest, ConcurrentRegistrations_Smoke)
{
  // Arrange
  std::vector<std::thread> threads;
  for (int i = 0; i < 32; ++i) {
    threads.emplace_back([this, i]() {
      auto r = std::make_shared<TestResource>("t" + std::to_string(i));
      manager->RegisterDeferredRelease(r);
    });
  }

  for (auto& t : threads) {
    t.join();
  }

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert - if we reached here, it's a basic concurrency smoke test.
  SUCCEED();
}
