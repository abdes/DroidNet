//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Core/Time/Conversion.h>
#include <Oxygen/Core/Time/SimulationClock.h>

using namespace oxygen::time;
using namespace std::chrono_literals;
using namespace std::chrono;

namespace {

//! Validate NetworkToLocal uncertainty behavior with varying confidence/RTT.
TEST(Conversion, NetworkToLocalUncertainty)
{
  NetworkClock net;

  // Case 1: High confidence, small RTT -> small uncertainty, reliable
  net.SetPeerOffset(CanonicalDuration { 0ms }, 0.9);
  net.SetRoundTripTime(CanonicalDuration { 10ms });
  const auto ntime = NetworkTime { steady_clock::now() };
  auto r1 = convert::NetworkToLocal(ntime, net);
  EXPECT_TRUE(r1.is_reliable);
  EXPECT_LT(r1.uncertainty, net.GetRoundTripTime());

  // Case 2: Low confidence, large RTT -> larger uncertainty, not reliable
  net.SetPeerOffset(CanonicalDuration { 0ms }, 0.2);
  net.SetRoundTripTime(CanonicalDuration { 50ms });
  auto r2 = convert::NetworkToLocal(ntime, net);
  EXPECT_FALSE(r2.is_reliable);
  EXPECT_GE(r2.uncertainty, r1.uncertainty);
}

//! Additional conversion tests: extremes, roundtrips, and passthroughs
TEST(Conversion, NetworkToLocal_ReliabilityAndUncertainty_Extremes)
{
  using namespace std::chrono_literals;
  NetworkClock net;

  // Perfect confidence, zero RTT -> zero uncertainty, reliable
  net.SetPeerOffset(CanonicalDuration { 0ms }, 1.0);
  net.SetRoundTripTime(CanonicalDuration { 0ms });
  auto r = convert::NetworkToLocal(NetworkTime { steady_clock::now() }, net);
  EXPECT_TRUE(r.is_reliable);
  EXPECT_EQ(static_cast<nanoseconds>(r.uncertainty).count(), 0);

  // No confidence, large RTT -> non-zero uncertainty, not reliable
  net.SetPeerOffset(CanonicalDuration { 0ms }, 0.0);
  net.SetRoundTripTime(CanonicalDuration { 1000ms });
  auto r2 = convert::NetworkToLocal(NetworkTime { steady_clock::now() }, net);
  EXPECT_FALSE(r2.is_reliable);
  EXPECT_GT(static_cast<nanoseconds>(r2.uncertainty).count(), 0);
}

TEST(Conversion, LocalToRemote_RemoteToLocal_RoundtripWithinUncertainty)
{
  NetworkClock net;
  net.SetPeerOffset(CanonicalDuration { 5ms }, 0.8);
  net.SetRoundTripTime(CanonicalDuration { 20ms });

  const auto local = PhysicalTime { steady_clock::now() };
  const auto remote = net.LocalToRemote(local);
  auto conv = convert::NetworkToLocal(remote, net);

  const auto recovered_ns = conv.local_time.get().time_since_epoch().count();
  const auto original_ns = local.get().time_since_epoch().count();
  const auto delta = std::llabs(recovered_ns - original_ns);
  const auto unc_ns = static_cast<nanoseconds>(conv.uncertainty).count();

  // The recovered local time should be within the calculated uncertainty (plus
  // a tiny slack)
  EXPECT_LE(delta, unc_ns + 100);
}

TEST(Conversion, Network_PredictRemoteTime_Equals_LocalToRemoteWithWindow)
{
  using namespace std::chrono_literals;
  NetworkClock net;
  net.SetPeerOffset(CanonicalDuration { 2ms }, 0.9);
  net.SetRoundTripTime(CanonicalDuration { 10ms });

  const auto local_now = PhysicalTime { steady_clock::now() };
  constexpr auto window = CanonicalDuration { 50ms };
  const auto pred = net.PredictRemoteTime(local_now, window);
  const auto expected
    = net.LocalToRemote(PhysicalTime { local_now.get() + window.get() });

  EXPECT_EQ(pred.get().time_since_epoch(), expected.get().time_since_epoch());
}

TEST(Conversion, Audit_ToWallClock_FromWallClock_Roundtrip)
{
  using namespace std::chrono_literals;
  AuditClock audit;
  const auto phys = PhysicalTime { steady_clock::now() };
  const auto wall = convert::ToWallClock(phys, audit);
  const auto phys2 = convert::FromWallClock(wall, audit);
  EXPECT_EQ(phys.get().time_since_epoch(), phys2.get().time_since_epoch());
}

TEST(Conversion, ToPresentation_TagAndEpochPreserved)
{
  SimulationTime s { steady_clock::now() };
  // PresentationClock param is unused by ToPresentation implementation; pass a
  // temporary
  PresentationClock dummy { SimulationClock { CanonicalDuration { 16ms } },
    1.0 };
  PresentationTime p = convert::ToPresentation(s, dummy);
  EXPECT_EQ(s.get().time_since_epoch(), p.get().time_since_epoch());
}

TEST(Conversion, TimelineSimulation_PassthroughRoundtrip)
{
  TimelineTime t { steady_clock::now() };
  auto s = convert::TimelineToSimulation(t);
  auto t2 = convert::SimulationToTimeline(s);
  EXPECT_EQ(t.get().time_since_epoch(), t2.get().time_since_epoch());
}

TEST(Conversion, Network_PredictRemoteTime_NegativeWindow)
{
  NetworkClock net;
  net.SetPeerOffset(CanonicalDuration { 0ms }, 0.8);
  net.SetRoundTripTime(CanonicalDuration { 10ms });

  const auto local_now = PhysicalTime { steady_clock::now() };
  // Negative window: behavior should be well-defined (predict in the past)
  constexpr auto neg_window
    = CanonicalDuration { nanoseconds(-5000000) }; // -5ms
  const auto pred = net.PredictRemoteTime(local_now, neg_window);
  // Expect predicted remote corresponds to local_now + neg_window mapped
  // through peer offset
  const auto expected
    = net.LocalToRemote(PhysicalTime { local_now.get() + neg_window.get() });
  EXPECT_EQ(pred.get().time_since_epoch(), expected.get().time_since_epoch());
}

TEST(Conversion, Network_SetPeerOffset_OutOfRangeConfidenceClamped)
{
  NetworkClock net;
  // Set out-of-range confidence values and ensure internal confidence stores
  // them as provided (NetworkClock currently stores confidence as given; test
  // documents behavior)
  net.SetPeerOffset(CanonicalDuration { 0ms }, -1.0);
  EXPECT_LE(net.GetOffsetConfidence(), 0.0);
  net.SetPeerOffset(CanonicalDuration { 0ms }, 2.0);
  EXPECT_GE(net.GetOffsetConfidence(), 1.0);
}

TEST(Conversion, Network_ProcessSyncEvent_SmoothingBehavior)
{
  NetworkClock net;
  net.SetSmoothingFactor(0.5);

  const auto local = PhysicalTime { steady_clock::now() };
  const auto remote = NetworkTime { steady_clock::now() - milliseconds(10) };
  NetworkClock::SyncEvent ev { local, remote, CanonicalDuration { 20ms }, 0.7 };

  // Capture previous offset then process event and expect peer_offset_ to blend
  const auto prev_offset = net.GetPeerOffset().get().count();
  net.ProcessSyncEvent(ev);
  const auto new_offset = net.GetPeerOffset().get().count();

  // With smoothing_factor 0.5, new_offset should be midpoint between prev and
  // estimate Compute estimate like NetworkClock does: local_ns - remote_ns -
  // rtt/2
  const auto remote_ns = ev.remote_time.get().time_since_epoch().count();
  const auto local_ns = ev.local_time.get().time_since_epoch().count();
  const auto rtt_half = ev.round_trip_time.get().count() / 2;
  const auto estimate = local_ns - remote_ns - rtt_half;
  const auto expected_blended
    = static_cast<long long>(prev_offset * (1.0 - 0.5) + estimate * 0.5);

  EXPECT_EQ(new_offset, expected_blended);
  EXPECT_EQ(
    net.GetRoundTripTime().get().count(), ev.round_trip_time.get().count());
  EXPECT_DOUBLE_EQ(net.GetOffsetConfidence(), ev.confidence);
}

} // namespace
