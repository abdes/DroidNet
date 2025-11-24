//===----------------------------------------------------------------------===//
// Tests for DeferredReclaimer Distributed under the 3-Clause BSD License.
//===----------------------------------------------------------------------===//

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/DeferredObjectRelease.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::graphics::detail;
using testing::Test;
namespace frame = oxygen::frame;

//===----------------------------------------------------------------------===//
// Test Fixtures and Helper Classes
//===----------------------------------------------------------------------===//

namespace {

//! Minimal test resource that exposes Release(), GetName(), and GetTypeName().
struct TestResource : std::enable_shared_from_this<TestResource> {
  TestResource(std::string n)
    : name(std::move(n))
    , released(false)
  {
  }
  auto Release() -> void { released = true; }
  auto WasReleased() const -> bool { return released; }
  auto GetName() const -> std::string_view { return name; }
  auto GetTypeName() const -> std::string_view { return "TestResource"; }
  auto GetTypeNamePretty() const -> std::string_view { return "TestResource"; }

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
    if (counter) {
      ++(*counter);
    }
  }
  std::atomic<int>* counter { nullptr };
};

//! Test fixture for DeferredReclaimer providing common setup and
//! teardown.
class DeferredReclaimerTest : public Test {
protected:
  // Called once before any test in this fixture runs
  static auto SetUpTestSuite() -> void
  {
    loguru::g_stderr_verbosity = loguru::Verbosity_2;
  }

  auto SetUp() -> void override
  {
    manager = std::make_unique<DeferredReclaimer>();
  }

  auto TearDown() -> void override { manager.reset(); }

  std::unique_ptr<DeferredReclaimer> manager;
};

} // namespace

//===----------------------------------------------------------------------===//
// Basic Resource Management Tests Tests core deferred release functionality for
// different resource types
//===----------------------------------------------------------------------===//

//! Tests that shared_ptr resources with Release() method are properly released
//! on frame cycle.
/*!
 Verifies that when a shared_ptr resource with a Release() method is registered
 for deferred release, the Release() method is called when OnBeginFrame() is
 invoked for the corresponding frame slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_SharedPtrWithRelease_CallsReleaseOnFrameCycle)
{
  // Arrange
  auto res = std::make_shared<TestResource>("res1");
  manager->RegisterDeferredRelease(res);

  // Act - simulate starting the same frame index to trigger the release
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(res->WasReleased());
}

//! Tests that DeferredObjectRelease(shared_ptr) resets the caller's pointer and
//! invokes the resource Release() on frame cycle.
NOLINT_TEST_F(DeferredReclaimerTest,
  DeferredObjectRelease_SharedPtr_RegistersAndCallsReleaseOnFrameCycle)
{
  // Arrange
  std::atomic<bool> released_flag { false };

  struct ObservedResource : TestResource {
    ObservedResource(std::atomic<bool>* flag)
      : TestResource("observed")
      , ext_flag(flag)
    {}

    auto Release() -> void {
      TestResource::Release();
      if (ext_flag) ext_flag->store(true);
    }

    std::atomic<bool>* ext_flag{nullptr};
  };

  auto res = std::make_shared<ObservedResource>(&released_flag);

  // Act
  oxygen::graphics::DeferredObjectRelease(res, *manager);

  // Original pointer should have been reset immediately
  EXPECT_EQ(res.get(), nullptr);

  // On frame cycle the deferred action should call Release()
  manager->OnBeginFrame(frame::Slot { 0 });
  EXPECT_TRUE(released_flag.load());
}

//! Tests that DeferredObjectRelease(raw pointer) schedules Release() on frame
//! cycle and sets the original pointer to nullptr.
NOLINT_TEST_F(DeferredReclaimerTest,
  DeferredObjectRelease_RawPointer_RegistersAndCallsReleaseOnFrameCycle)
{
  // Arrange
  std::atomic<bool> released_flag { false };

  struct ObservedRaw {
    ObservedRaw(std::atomic<bool>* f) : flag(f) {}
    auto Release() -> void { if (flag) flag->store(true); }
    std::atomic<bool>* flag{nullptr};
  };

  auto* raw = new ObservedRaw(&released_flag);

  // Act
  oxygen::graphics::DeferredObjectRelease(raw, *manager);

  // original pointer should be set to nullptr
  EXPECT_EQ(raw, nullptr);

  manager->OnBeginFrame(frame::Slot { 0 });
  EXPECT_TRUE(released_flag.load());

  // cleanup - release should have been invoked by reclaimer; raw may have been
  // deleted already depending on implementation, but ProcessAllDeferredReleases
  // will be invoked in teardown anyway. If not destroyed, attempt to delete.
  if (raw != nullptr) delete raw;
}

//! Tests that shared_ptr resources without Release() method have destructor
//! called on frame cycle.
/*!
 Verifies that when a shared_ptr resource without a Release() method is
 registered for deferred release, the destructor is properly invoked when
 OnBeginFrame() processes the deferred releases for that frame slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_SharedPtrWithoutRelease_CallsDestructorOnFrameCycle)
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

//! Tests that raw pointer resources with Release() method are properly released
//! on frame cycle.
/*!
 Verifies that when a raw pointer resource with a Release() method is registered
 for deferred release, the Release() method is called when OnBeginFrame()
 processes the deferred releases for that frame slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_RawPointerWithRelease_CallsReleaseOnFrameCycle)
{
  // Arrange
  auto res = new TestResource("raw");
  manager->RegisterDeferredRelease(res);

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(res->WasReleased());

  // Cleanup
  delete res;
}

//! Tests that deferred actions are executed on frame cycle.
/*!
 Verifies that lambda functions registered via RegisterDeferredAction() are
 properly executed when OnBeginFrame() processes the deferred actions for the
 corresponding frame slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredAction_LambdaFunction_ExecutesOnFrameCycle)
{
  // Arrange
  std::atomic<bool> ran { false };
  manager->RegisterDeferredAction([&]() { ran.store(true); });

  // Act
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert
  EXPECT_TRUE(ran.load());
}

//===----------------------------------------------------------------------===//
// Bulk Operations Tests Tests operations that affect multiple frame slots
//===----------------------------------------------------------------------===//

//! Tests that ProcessAllDeferredReleases releases resources across all frames.
/*!
 Verifies that calling ProcessAllDeferredReleases() processes deferred releases
 from all frame slots, not just the current one, ensuring complete cleanup
 during shutdown scenarios.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  ProcessAllDeferredReleases_MultipleFrames_ReleasesAllFrames)
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

//! Tests that OnRendererShutdown processes all deferred releases.
/*!
 Verifies that calling OnRendererShutdown() triggers processing of all deferred
 releases across all frame slots, ensuring complete cleanup when the renderer is
 being shut down.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  OnRendererShutdown_WithPendingReleases_ProcessesAllDeferredReleases)
{
  // Arrange
  auto r0 = std::make_shared<TestResource>("r0");
  manager->RegisterDeferredRelease(r0);

  // Act
  manager->OnRendererShutdown();

  // Assert
  EXPECT_TRUE(r0->WasReleased());
}

//===----------------------------------------------------------------------===//
// Edge Case Tests Tests boundary conditions and error handling scenarios
//===----------------------------------------------------------------------===//

//! Tests that registering null raw pointer is safely handled without crashes.
/*!
 Verifies that registering a nullptr raw pointer for deferred release does not
 cause crashes or undefined behavior when frame processing occurs.
*/
NOLINT_TEST_F(
  DeferredReclaimerTest, RegisterDeferredRelease_NullRawPointer_DoesNotCrash)
{
  // Arrange
  TestResource* nullres = nullptr;

  // Act & Assert - should not throw or crash
  manager->RegisterDeferredRelease(nullres);
  manager->OnBeginFrame(frame::Slot { 0 });
  SUCCEED();
}

//! Tests that multiple registrations from same frame are all executed.
/*!
 Verifies that when multiple resources are registered for deferred release
 within the same frame slot, all of them are properly processed when
 OnBeginFrame() is called for that slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_MultipleRegistrationsSameFrame_AllExecuted)
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

//===----------------------------------------------------------------------===//
// Ordering and Sequence Tests Tests execution order and sequence preservation
//===----------------------------------------------------------------------===//

//! Tests that deferred actions execute in registration order within a frame.
/*!
 Verifies that when multiple deferred actions are registered for the same frame
 slot, they are executed in the same order they were registered, ensuring
 predictable execution sequence.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredAction_MultipleActionsPerFrame_PreservesRegistrationOrder)
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

//===----------------------------------------------------------------------===//
// Custom Deleter Tests Tests specialized resource cleanup scenarios
//===----------------------------------------------------------------------===//

//! Tests that shared_ptr with custom deleter is properly invoked on frame
//! cycle.
/*!
 Verifies that when a shared_ptr with a custom deleter is registered for
 deferred release, the custom deleter is invoked when OnBeginFrame() processes
 the release for that frame slot.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_SharedPtrWithCustomDeleter_InvokesDeleterOnFrameCycle)
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

//===----------------------------------------------------------------------===//
// Concurrency Tests Tests thread safety and concurrent access patterns
//===----------------------------------------------------------------------===//

//! Tests that concurrent registrations are handled safely without crashes.
/*!
 Verifies that multiple threads can safely register deferred releases
 concurrently without causing data races or crashes. This is a basic smoke test
 for thread safety.
*/
NOLINT_TEST_F(DeferredReclaimerTest,
  RegisterDeferredRelease_ConcurrentRegistrations_HandledSafely)
{
  // Arrange
  std::vector<std::thread> threads;
  for (int i = 0; i < 32; ++i) {
    threads.emplace_back([this, i]() {
      auto r = std::make_shared<TestResource>("t" + std::to_string(i));
      manager->RegisterDeferredRelease(r);
    });
  }

  // Act
  for (auto& t : threads) {
    t.join();
  }
  manager->OnBeginFrame(frame::Slot { 0 });

  // Assert - if we reached here, it's a basic concurrency smoke test
  SUCCEED();
}

//===----------------------------------------------------------------------===//
// Death Tests Tests error conditions that should trigger assertions
//===----------------------------------------------------------------------===//

//! Tests that out-of-bounds frame slot triggers assertion in debug builds.
/*!
 Verifies that passing an out-of-bounds frame slot to OnBeginFrame() triggers
 the debug assertion check, ensuring bounds validation is properly enforced.
*/
NOLINT_TEST_F(
  DeferredReclaimerTest, OnBeginFrame_OutOfBoundsSlot_TriggersAssertion)
{
  // Arrange & Act & Assert CHECK should abort when an out-of-bounds slot is
  // provided
  NOLINT_ASSERT_DEATH(
    manager->OnBeginFrame(frame::kMaxSlot), "Frame slot out of bounds");
}
