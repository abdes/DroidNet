//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>

namespace oxygen::engine {

struct RenderContext;

namespace detail {

  //! RAII helper for render context management.
  class ContextScope {
  public:
    ContextScope(const RenderContext*& context_ptr, const RenderContext& ctx)
      : context_ptr_(&context_ptr)
    {
      *context_ptr_ = &ctx;
    }

    ~ContextScope() { context_ptr_ = nullptr; }

    OXYGEN_MAKE_NON_COPYABLE(ContextScope)
    OXYGEN_DEFAULT_MOVABLE(ContextScope)

  private:
    const RenderContext** context_ptr_;
  };

} // namespace detail
} // namespace oxygen::engine
