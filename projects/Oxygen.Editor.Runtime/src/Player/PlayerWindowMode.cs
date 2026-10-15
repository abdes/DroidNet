// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Player;

/// <summary>How the player window shows.</summary>
public enum PlayerWindowMode
{
    /// <summary>In a frame, at the launch size.</summary>
    Windowed,

    /// <summary>Over its display at the desktop resolution, without a frame.</summary>
    Borderless,
}
