//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Serio/RegionStream.h, Serio/AnyStream.h over Serio/NativeFile.h and
// Serio/MappedFile.h

#include "Support/DeviceFactories.h"
#include "Support/StreamConformance.h"
#include "Support/StreamFactories.h"
#include "gtest/gtest.h"

#include <Oxygen/Testing/GTest.h>

namespace oxygen::serio::testing {
namespace {

  // Region streams over files: the file streams the engine reads and writes
  // its artifacts with.

  using InputFactories = ::testing::Types<
    test::RegionReadStreamFactory<test::NativeFileFactory>,
    test::RegionReadStreamFactory<test::MappedFileFactory>,
    test::RegionReadStreamFactory<test::RegionFactory<test::NativeFileFactory>>,
    test::AnyInputStreamFactory<
      test::RegionReadStreamFactory<test::NativeFileFactory>>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(Files, InputStreamConformance, InputFactories);

  using OutputFactories
    = ::testing::Types<test::RegionWriteStreamFactory<test::NativeFileFactory>,
      test::AnyOutputStreamFactory<
        test::RegionWriteStreamFactory<test::NativeFileFactory>>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Files, OutputStreamConformance, OutputFactories);

  using PatchableFactories
    = ::testing::Types<test::RegionWriteStreamFactory<test::NativeFileFactory>>;

  INSTANTIATE_TYPED_TEST_SUITE_P(
    Files, PatchableOutputStreamConformance, PatchableFactories);

} // namespace
} // namespace oxygen::serio::testing
