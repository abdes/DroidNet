//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Operation.h, Serio/AsyncDevice.h, Serio/AsyncFile.h,
// Serio/AnyDevice.h (AnyAsyncDevice), Serio/Region.h,
// Serio/Testing/AsyncDeviceConformance.h

#include "Support/DeviceFactories.h"
#include "gtest/gtest.h"

#include <Oxygen/Serio/Testing/AsyncDeviceConformance.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  // Every asynchronous Serio device and its compositions: memory, whose
  // operations complete at once, and files opened through a driver; the
  // writable suite takes those that write. Files make the suites functional.

  using Factories = ::testing::Types<test::MemoryFileFactory,
    test::ByteViewFactory, test::AsyncFileFactory,
    test::AnyAsyncDeviceFactory<test::ByteViewFactory>,
    test::AnyAsyncDeviceFactory<test::MemoryFileFactory>,
    test::AnyAsyncDeviceFactory<test::AsyncFileFactory>,
    test::FaultInjectingFactory<test::MemoryFileFactory>,
    test::FaultInjectingFactory<test::AsyncFileFactory>,
    test::RegionFactory<test::MemoryFileFactory>,
    test::RegionFactory<test::AsyncFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(Serio, AsyncDeviceConformance, Factories);

  using WritableFactories
    = ::testing::Types<test::MemoryFileFactory, test::AsyncFileFactory,
      test::AnyAsyncDeviceFactory<test::MemoryFileFactory>,
      test::AnyAsyncDeviceFactory<test::AsyncFileFactory>,
      test::FaultInjectingFactory<test::MemoryFileFactory>,
      test::FaultInjectingFactory<test::AsyncFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Serio, AsyncWritableDeviceConformance, WritableFactories);

} // namespace
} // namespace oxygen::serio::testing
