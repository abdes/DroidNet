// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
/// Managed camera orbit styles, converted at the native session boundary. Holding the right mouse
/// button flies the perspective camera in either style.
/// </summary>
public enum CameraControlMode
{
    /// <summary>Orbit Turntable.</summary>
    OrbitTurntable = 0,

    /// <summary>Orbit Trackball.</summary>
    OrbitTrackball = 1,
}
