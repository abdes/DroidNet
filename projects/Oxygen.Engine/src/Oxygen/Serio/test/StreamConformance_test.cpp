//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/Stream.h, Serio/MemoryStream.h, Serio/RegionStream.h,
// Serio/AnyStream.h, Serio/test/Conformance/StreamConformance.h

#include "Conformance/StreamConformance.h"
#include "Support/DeviceFactories.h"
#include "Support/StreamFactories.h"
#include "gtest/gtest.h"

#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  // Streams over memory. The same streams over files run in the functional
  // tier.

  using InputFactories = ::testing::Types<test::MemoryStreamInputFactory,
    test::ReadOnlyMemoryStreamFactory,
    test::RegionReadStreamFactory<test::MemoryFileFactory>,
    test::RegionReadStreamFactory<test::ByteViewFactory>,
    test::RegionReadStreamFactory<test::RegionFactory<test::MemoryFileFactory>>,
    test::RegionReadStreamFactory<
      test::AnyDeviceFactory<test::ByteViewFactory>>,
    test::ShortReadStreamFactory,
    test::AnyInputStreamFactory<
      test::RegionReadStreamFactory<test::MemoryFileFactory>>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Memory, InputStreamConformance, InputFactories);

  using OutputFactories = ::testing::Types<test::MemoryStreamOutputFactory,
    test::RegionWriteStreamFactory<test::MemoryFileFactory>,
    test::RegionWriteStreamFactory<
      test::FaultInjectingFactory<test::MemoryFileFactory>>,
    test::AnyOutputStreamFactory<
      test::RegionWriteStreamFactory<test::MemoryFileFactory>>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Memory, OutputStreamConformance, OutputFactories);

  using PatchableFactories = ::testing::Types<test::MemoryStreamOutputFactory,
    test::RegionWriteStreamFactory<test::MemoryFileFactory>,
    test::RegionWriteStreamFactory<
      test::FaultInjectingFactory<test::MemoryFileFactory>>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Memory, PatchableOutputStreamConformance, PatchableFactories);

} // namespace
} // namespace oxygen::serio::testing
