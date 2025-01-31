//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include "Oxygen/OxCo/Detail/ProxyFrame.h"

namespace oxygen::co::detail {

//! A ProxyFrame that corresponds to an underlying task invocation, used in
//! the implementation of Task. In addition to the CoroutineFrame, it stores a
//! program counter value representing the point at which the task will
//! resume execution.
class TaskFrame : public ProxyFrame {
public:
    static constexpr uintptr_t kTag = frame_tags::kTask | ProxyFrame::kTag;
    TaskFrame() { TagWith(kTag); }

    void ProgramCounter(const uintptr_t pc) { pc_ = pc; }
    [[nodiscard]] auto ProgramCounter() const { return pc_; }

private:
    uintptr_t pc_ = 0;
};

} // namespace oxygen::co::detail
