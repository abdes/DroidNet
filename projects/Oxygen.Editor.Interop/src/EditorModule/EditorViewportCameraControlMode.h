//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

namespace oxygen::interop::module {

  //! Editor viewport camera navigation modes.
  enum class EditorViewportCameraControlMode {
    //! World-up orbit around the view focus point.
    kOrbitTurntable = 0,

    //! Free trackball orbit around the view focus point.
    kOrbitTrackball,

    //! Right-mouse free-fly camera movement.
    kFly,
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
