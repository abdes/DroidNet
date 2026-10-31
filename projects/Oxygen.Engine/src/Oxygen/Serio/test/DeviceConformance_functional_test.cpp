//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Device.h, Serio/NativeFile.h, Serio/MappedFile.h,
// Serio/MemoryFile.h, Serio/ByteView.h, Serio/AnyDevice.h, Serio/Region.h,
// Serio/Platform/*/NativeFile.cpp, Serio/Platform/*/MappedFile.cpp,
// Serio/Testing/DeviceConformance.h, Serio/Testing/FaultInjectingDevice.h

#include "Support/DeviceFactories.h"
#include "gtest/gtest.h"

#include <Oxygen/Serio/Testing/DeviceConformance.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  // Every Serio device, and every composition of them: erased, wrapped in a
  // fault-injecting device with nothing scheduled, and viewed as a region.
  // Each capability suite takes the devices that have the capability. The
  // suites read concurrently, so they run in the functional tier.

  using Factories = ::testing::Types<test::MemoryFileFactory,
    test::ByteViewFactory, test::NativeFileFactory, test::MappedFileFactory,
    test::AnyDeviceFactory<test::MemoryFileFactory>,
    test::AnyDeviceFactory<test::NativeFileFactory>,
    test::AnyDeviceFactory<test::MappedFileFactory>,
    test::FaultInjectingFactory<test::MemoryFileFactory>,
    test::FaultInjectingFactory<test::NativeFileFactory>,
    test::RegionFactory<test::MemoryFileFactory>,
    test::RegionFactory<test::NativeFileFactory>,
    test::RegionFactory<test::MappedFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(Serio, DeviceConformance, Factories);

  using WritableFactories = ::testing::Types<test::MemoryFileFactory,
    test::NativeFileFactory, test::AnyDeviceFactory<test::MemoryFileFactory>,
    test::AnyDeviceFactory<test::NativeFileFactory>,
    test::FaultInjectingFactory<test::MemoryFileFactory>,
    test::FaultInjectingFactory<test::NativeFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Serio, WritableDeviceConformance, WritableFactories);

  using ContiguousFactories
    = ::testing::Types<test::MemoryFileFactory, test::ByteViewFactory,
      test::MappedFileFactory, test::RegionFactory<test::MemoryFileFactory>,
      test::RegionFactory<test::MappedFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Serio, ContiguousDeviceConformance, ContiguousFactories);

} // namespace
} // namespace oxygen::serio::testing
