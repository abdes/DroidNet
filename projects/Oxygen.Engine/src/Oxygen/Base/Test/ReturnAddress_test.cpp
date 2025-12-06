//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/ReturnAddress.h>

namespace {

auto TestReturnAddress() -> void* { return oxygen::ReturnAddress<>(); }

//! Tests that `oxygen::ReturnAddress` returns a non-null address.
NOLINT_TEST(ReturnAddressTest, ReturnsNonNullAddress)
{
  void* address = TestReturnAddress();
  EXPECT_NE(address, nullptr);
}

} // namespace
