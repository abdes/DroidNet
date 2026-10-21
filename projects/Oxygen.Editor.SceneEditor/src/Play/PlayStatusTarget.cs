// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Play;

/// <summary>What clicking the Play status shows.</summary>
public enum PlayStatusTarget
{
    /// <summary>Nothing.</summary>
    None,

    /// <summary>The player window, brought to the front.</summary>
    Player,

    /// <summary>The Problems panel.</summary>
    Problems,

    /// <summary>The Cooking panel.</summary>
    Cooking,

    /// <summary>The Output panel, with the player's log.</summary>
    Output,
}
