//===----------------------------------------------------------------------===//
// Compile-time API tests for AsyncEngine
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>
#include <type_traits>

#include <Oxygen/Engine/AsyncEngine.h>

using namespace oxygen;
using namespace oxygen::engine;

TEST(AsyncEngineApi, SubscribeSignatureAvailable)
{
  using AE = AsyncEngine;
  using Callback = ModuleAttachedCallback;

  // Ensure the subscription alias exists and SubscribeModuleAttached returns
  // it.
  using Ret = decltype(std::declval<AE&>().SubscribeModuleAttached(
    std::declval<Callback>(), false));
  static_assert(std::is_same_v<Ret, AE::ModuleSubscription>,
    "SubscribeModuleAttached must return ModuleSubscription");

  SUCCEED(); // If compiled, API exists
}
