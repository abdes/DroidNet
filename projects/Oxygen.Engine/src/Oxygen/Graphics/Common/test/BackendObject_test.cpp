//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at <https://opensource.org/licenses/BSD-3-Clause>.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <future>
#include <thread>

#include <Oxygen/Graphics/Common/BackendLifetime.h>
#include <Oxygen/Graphics/Common/BackendObject.h>
#include <Oxygen/Testing/GTest.h>

namespace {
struct CleanupProbe {
  std::weak_ptr<void> lifetime;
  bool* retained_during_cleanup;
  int* destroyed;
};

NOLINT_TEST(
  BackendObjectTest, RetainsLifetimeThroughCleanupButNotThroughWeakObserver)
{
  auto lifetime = std::make_shared<int>(42);
  const std::weak_ptr<void> weak_lifetime = lifetime;
  bool retained = false;
  int destroyed = 0;
  auto owner = oxygen::graphics::AdoptBackendObject(
    new CleanupProbe { lifetime, &retained, &destroyed },
    [](void* object) noexcept {
      auto* probe = static_cast<CleanupProbe*>(object);
      *probe->retained_during_cleanup = !probe->lifetime.expired();
      ++*probe->destroyed;
      delete probe;
    },
    lifetime);
  std::weak_ptr<void> weak_owner = owner;
  lifetime.reset();
  EXPECT_FALSE(weak_lifetime.expired());
  auto second_owner = owner;
  owner.reset();
  EXPECT_EQ(destroyed, 0);
  second_owner.reset();
  EXPECT_TRUE(retained);
  EXPECT_EQ(destroyed, 1);
  EXPECT_TRUE(weak_owner.expired());
  EXPECT_TRUE(weak_lifetime.expired());
}
} // namespace

NOLINT_TEST(BackendLifetimeTest, NestedFactoriesRemainAdmittedWhileCloseWaits)
{
  oxygen::graphics::BackendLifetime lifetime;
  std::promise<void> closing;
  std::promise<void> closed;
  auto closed_result = closed.get_future();
  std::jthread closer;
  {
    const auto outer = lifetime.AcquireOperation();
    closer = std::jthread([&] {
      closing.set_value();
      (void)lifetime.BeginClose();
      lifetime.FinishClose();
      closed.set_value();
    });
    closing.get_future().wait();
    const auto nested = lifetime.AcquireOperation();
    EXPECT_EQ(closed_result.wait_for(std::chrono::milliseconds { 0 }),
      std::future_status::timeout);
  }
  closer.join();
  EXPECT_EQ(lifetime.State(), oxygen::graphics::BackendLifecycle::kRetiring);
  EXPECT_THROW((void)lifetime.AcquireOperation(), std::logic_error);
}
