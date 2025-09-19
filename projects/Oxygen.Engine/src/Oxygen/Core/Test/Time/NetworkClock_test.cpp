//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/NetworkClock.h>
#include <Oxygen/Core/Time/PhysicalClock.h>

using namespace oxygen::time;
using namespace std::chrono_literals;

namespace {

// ReSharper disable once CppRedundantQualifier
class NetworkClockTest : public ::testing::Test { };

NOLINT_TEST_F(NetworkClockTest, OffsetConversionRoundTrip)
{
  NetworkClock net;
  constexpr auto offset = CanonicalDuration { 500ms };
  net.SetPeerOffset(offset, 0.9);

  PhysicalClock phys;
  auto local = phys.Now();
  auto remote = net.LocalToRemote(local);
  auto back = net.RemoteToLocal(remote);

  // back should be equal to original local within nanosecond arithmetic
  EXPECT_EQ(back.get().time_since_epoch().count(),
    local.get().time_since_epoch().count());
}

NOLINT_TEST_F(NetworkClockTest, RTTAndSmoothing)
{
  NetworkClock net;
  net.SetRoundTripTime(CanonicalDuration { 200ms });
  EXPECT_EQ(net.GetRoundTripTime(), CanonicalDuration { 200ms });

  net.SetSmoothingFactor(0.5);
  EXPECT_DOUBLE_EQ(net.GetSmoothingFactor(), 0.5);
}

} // namespace
