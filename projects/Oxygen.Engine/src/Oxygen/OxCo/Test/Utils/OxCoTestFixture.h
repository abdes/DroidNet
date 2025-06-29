//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <iostream>
#include <memory>

#include <gtest/gtest.h>

#include "./TestEventLoop.h"

namespace oxygen::co::testing {

class OxCoTestFixture : public ::testing::Test {
protected:
  std::unique_ptr<TestEventLoop> el_ {};

  void SetUp() override
  {
    ::testing::internal::CaptureStderr();
    el_ = std::make_unique<TestEventLoop>();
  }

  void TearDown() override
  {
    el_.reset();
    const auto captured_stderr = ::testing::internal::GetCapturedStderr();
    std::cout << "Captured stderr:\n" << captured_stderr << '\n';
  }
};

} // namespace oxygen::co::testing
