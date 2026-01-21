//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>

namespace oxygen::graphics {

//! RAII helper for a GPU debug event scope.
class GpuEventScope {
public:
  explicit GpuEventScope(CommandRecorder& recorder, std::string_view name)
    : recorder_(&recorder)
  {
    recorder_->BeginEvent(name);
  }

  ~GpuEventScope()
  {
    if (recorder_ != nullptr) {
      recorder_->EndEvent();
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(GpuEventScope)
  OXYGEN_MAKE_NON_MOVABLE(GpuEventScope)

private:
  CommandRecorder* recorder_;
};

} // namespace oxygen::graphics
