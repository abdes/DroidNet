//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::graphics::testing {

//! Executes a test operation and exposes its result after successful
//! submission.
template <typename Operation>
auto SubmitCommands(Graphics& graphics, const std::string_view name,
  Operation&& operation) -> std::invoke_result_t<Operation, CommandRecorder&>
{
  using Result = std::invoke_result_t<Operation, CommandRecorder&>;
  auto recording = graphics.AcquireCommandRecorder(
    graphics.QueueKeyFor(QueueRole::kGraphics), name,
    SubmissionPolicy::kExplicit);
  if constexpr (std::is_void_v<Result>) {
    ASSERT_TRUE(recording);
    std::invoke(std::forward<Operation>(operation), *recording);
    EXPECT_TRUE(recording.Submit());
  } else {
    if (!recording)
      return Result {};
    auto result = std::invoke(std::forward<Operation>(operation), *recording);
    if (!recording.Submit())
      return Result {};
    return result;
  }
}

} // namespace oxygen::graphics::testing
