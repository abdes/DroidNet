// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Scenes.Views;

/// <summary>What a viewport does with a scene camera.</summary>
public enum SceneCameraCommand
{
    /// <summary>Shows the scene through the camera.</summary>
    LookThrough,

    /// <summary>Shows the scene through the editor's own camera again.</summary>
    ReturnToEditorCamera,

    /// <summary>Moves the camera with the viewport's navigation.</summary>
    Pilot,

    /// <summary>Stops moving the camera with the viewport's navigation.</summary>
    StopPiloting,

    /// <summary>Moves the camera to the viewport's view.</summary>
    AlignToView,
}
