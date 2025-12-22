//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <memory>

#include <EditorModule/InputAccumulator.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <api_export.h>

namespace oxygen::interop::module {

  // Small writer interface that the adapter will call. This decouples the
  // accumulator from the engine's concrete writer type (InputEvents::ForWrite()).
  struct IInputWriter {
    virtual ~IInputWriter() = default;

    OXGN_EI_API virtual void WriteMouseMove(ViewId view, SubPixelMotion delta,
      SubPixelPosition position) = 0;
    OXGN_EI_API virtual void WriteMouseWheel(ViewId view, SubPixelMotion delta,
      SubPixelPosition position) = 0;
    OXGN_EI_API virtual void WriteKey(ViewId view, EditorKeyEvent ev) = 0;
    OXGN_EI_API virtual void WriteMouseButton(ViewId view, EditorButtonEvent ev) = 0;
  };

  class InputAccumulatorAdapter {
  public:
    OXGN_EI_API explicit InputAccumulatorAdapter(
      std::unique_ptr<IInputWriter> writer) noexcept;

    // Forward a previously-drained batch to the writer in proper order.
    OXGN_EI_API void DispatchForView(ViewId view, const AccumulatedInput& batch) noexcept;

  private:
    std::unique_ptr<IInputWriter> writer_;
  };

} // namespace oxygen::interop::module
