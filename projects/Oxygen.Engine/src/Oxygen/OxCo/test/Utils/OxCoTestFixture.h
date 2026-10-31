//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include "./TestEventLoop.h"
#include <gtest/gtest.h>

namespace oxygen::co::testing {

class OxCoTestFixture : public ::testing::Test {
protected:
  std::unique_ptr<TestEventLoop> el_ {};

  void SetUp() override { el_ = std::make_unique<TestEventLoop>(); }

  void TearDown() override { el_.reset(); }
};

} // namespace oxygen::co::testing
