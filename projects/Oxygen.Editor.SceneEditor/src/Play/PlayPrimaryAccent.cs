// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.SceneEditor.Play;

/// <summary>How the Play button shows.</summary>
public enum PlayPrimaryAccent
{
    /// <summary>A green Play.</summary>
    Play,

    /// <summary>Restart, while this scene plays.</summary>
    Restart,

    /// <summary>An amber Restart: the scene changed since the session started.</summary>
    RestartChanged,

    /// <summary>Play cannot be pressed.</summary>
    Unavailable,
}
