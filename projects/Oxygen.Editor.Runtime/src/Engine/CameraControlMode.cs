// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Managed camera control mode options, converted at the native session boundary.</summary>
public enum CameraControlMode
{
    /// <summary>Orbit Turntable.</summary>
    OrbitTurntable = 0,

    /// <summary>Orbit Trackball.</summary>
    OrbitTrackball = 1,

    /// <summary>Fly.</summary>
    Fly = 2,
}
