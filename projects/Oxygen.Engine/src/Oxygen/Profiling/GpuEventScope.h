//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <source_location>
#include <utility>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Graphics/Common/CommandRecorder.h>
#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::graphics {

class GpuEventScope {
public:
  explicit GpuEventScope(CommandRecorder& recorder,
    const profiling::GpuProfileScopeDesc& desc,
    const std::source_location callsite = std::source_location::current())
    : recorder_(&recorder)
    , token_(recorder_->BeginProfileScope(desc, callsite))
  {
  }

  explicit GpuEventScope(CommandRecorder& recorder, std::string_view label,
    const profiling::ProfileGranularity granularity,
    const profiling::ProfileCategory category
    = profiling::ProfileCategory::kGeneral,
    profiling::ScopeVariables variables = {},
    const profiling::ProfileColor color = {},
    const std::source_location callsite = std::source_location::current())
    : GpuEventScope(recorder,
        profiling::GpuProfileScopeDesc {
          .label = std::string(label),
          .variables = std::move(variables),
          .granularity = granularity,
          .category = category,
          .color = color,
        },
        callsite)
  {
  }

  ~GpuEventScope()
  {
    if (recorder_ != nullptr) {
      recorder_->EndProfileScope(token_);
    }
  }

  OXYGEN_MAKE_NON_COPYABLE(GpuEventScope)
  OXYGEN_MAKE_NON_MOVABLE(GpuEventScope)

private:
  CommandRecorder* recorder_ { nullptr };
  GpuProfileScopeToken token_ {};
};

} // namespace oxygen::graphics
