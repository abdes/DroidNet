//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed

namespace Oxygen::Interop {

  /// <summary>
  /// Pose that places a scene node at a view's editor camera, in the node's
  /// parent space. Rotation uses the editor's Euler-degree convention.
  /// </summary>
  public
  ref class ViewCameraPoseManaged sealed {
  public:
    property System::Numerics::Vector3 Position;
    property System::Numerics::Vector3 RotationDegrees;
    property System::Numerics::Vector3 Scale;

    /// <summary>
    /// Orthographic half-height for an orthographic camera node, or no value
    /// when the size should stay as authored.
    /// </summary>
    property System::Nullable<float> OrthographicSize;

    /// <summary>
    /// Vertical field of view, in degrees, for a perspective camera node when
    /// the view is perspective, or no value when it should stay as authored.
    /// </summary>
    property System::Nullable<float> FieldOfViewDegrees;
  };

} // namespace Oxygen::Interop
