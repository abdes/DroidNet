//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma unmanaged

#include "pch.h"

#include <EditorModule/InputAccumulatorAdapter.h>

namespace oxygen::interop::module {

  InputAccumulatorAdapter::InputAccumulatorAdapter(
    std::unique_ptr<IInputWriter> writer) noexcept
    : writer_(std::move(writer)) {
  }

  void InputAccumulatorAdapter::DispatchForView(
    ViewId view, const AccumulatedInput& batch) noexcept {
    for (auto const& k : batch.key_events) {
      writer_->WriteKey(view, k);
    }

    for (auto const& b : batch.button_events) {
      writer_->WriteMouseButton(view, b);
    }

    // Dispatch transient motion/wheel events last so their per-frame values
    // are not immediately overwritten by subsequent key/button micro-updates
    // in InputSystem.
    if (!(batch.mouse_delta.dx == 0.0F && batch.mouse_delta.dy == 0.0F)) {
      writer_->WriteMouseMove(view, batch.mouse_delta, batch.last_position);
    }

    if (!(batch.scroll_delta.dx == 0.0F && batch.scroll_delta.dy == 0.0F)) {
      writer_->WriteMouseWheel(view, batch.scroll_delta, batch.last_position);
    }
  }

} // namespace oxygen::interop::module
