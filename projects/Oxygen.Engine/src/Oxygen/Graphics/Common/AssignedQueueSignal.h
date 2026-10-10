//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>

namespace oxygen::graphics {

class CommandQueue;

//! The value of a submit-ordered queue signal, assigned by the queue when it
//! accepts the recording; zero until then.
/*!
 A value reserved while recording is only safe while no other signal is
 emitted before that recording submits. Values the queue assigns follow the
 submission order instead, so recordings may submit in any order.
*/
class AssignedQueueSignal final {
public:
  [[nodiscard]] auto Value() const noexcept -> uint64_t { return value_; }

private:
  friend class CommandQueue;
  uint64_t value_ { 0 };
};

} // namespace oxygen::graphics
