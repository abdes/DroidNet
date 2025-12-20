// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
/// Defines the type of camera projection or orientation.
/// </summary>
public enum CameraType
{
    /// <summary>Perspective projection.</summary>
    Perspective,

    /// <summary>Top orthographic view.</summary>
    Top,

    /// <summary>Bottom orthographic view.</summary>
    Bottom,

    /// <summary>Left orthographic view.</summary>
    Left,

    /// <summary>Right orthographic view.</summary>
    Right,

    /// <summary>Front orthographic view.</summary>
    Front,

    /// <summary>Back orthographic view.</summary>
    Back,
}
