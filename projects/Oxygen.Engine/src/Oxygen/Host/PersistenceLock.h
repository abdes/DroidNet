//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <filesystem>
#include <memory>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Host/api_export.h>

namespace oxygen::host {

//! Exclusive claim on a folder of saved state, held across processes.
/*!
 The first instance of an application to run owns its saved state (console
 variables, console history, settings). Another instance that finds the
 claim taken runs without saving, so it cannot overwrite the owner's
 changes with its own stale copy. The operating system drops the claim when
 the owning process exits, even if it is killed.
*/
class PersistenceLock final {
public:
  //! Tries to claim `folder`; `IsOwner()` tells whether it succeeded.
  OXGN_HOST_API explicit PersistenceLock(const std::filesystem::path& folder);
  OXGN_HOST_API ~PersistenceLock();

  OXYGEN_MAKE_NON_COPYABLE(PersistenceLock)
  OXYGEN_MAKE_NON_MOVABLE(PersistenceLock)

  [[nodiscard]] auto IsOwner() const noexcept -> bool
  {
    return handle_ != nullptr;
  }

private:
  struct Handle;
  std::unique_ptr<Handle> handle_;
};

} // namespace oxygen::host
