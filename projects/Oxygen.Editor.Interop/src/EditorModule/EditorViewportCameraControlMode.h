//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

namespace oxygen::interop::module {

  //! Editor viewport camera orbit styles. Holding the right mouse button flies
  //! the perspective camera in either style.
  enum class EditorViewportCameraControlMode {
    //! World-up orbit around the view focus point.
    kOrbitTurntable = 0,

    //! Free trackball orbit around the view focus point.
    kOrbitTrackball,
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
