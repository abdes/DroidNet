//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Profiling/ProfileScope.h>

namespace oxygen::profiling {

//! Optional synchronous observer of CPU scopes on the registering thread.
class CpuScopeObserver {
public:
  virtual ~CpuScopeObserver() = default;

  //! Return true to receive the corresponding end callback. Callbacks must
  //! neither throw nor create CPU profiling scopes; the descriptor is borrowed.
  virtual auto OnScopeBegin(const CpuProfileScopeDesc& desc) noexcept -> bool
    = 0;
  virtual auto OnScopeEnd() noexcept -> void = 0;
};

//! Attaches one observer to the current thread, independently of Tracy/PIX.
//! The observer must outlive registration. All observed scopes must end before
//! registration ends, on the same thread. Nested registrations are rejected.
class ScopedCpuScopeObserver final {
public:
  OXGN_PROF_API explicit ScopedCpuScopeObserver(CpuScopeObserver& observer);
  OXGN_PROF_API ~ScopedCpuScopeObserver();

  OXYGEN_MAKE_NON_COPYABLE(ScopedCpuScopeObserver)
  OXYGEN_MAKE_NON_MOVABLE(ScopedCpuScopeObserver)

private:
  CpuScopeObserver& observer_;
};

} // namespace oxygen::profiling
