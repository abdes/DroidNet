// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>The step of Play that failed.</summary>
public enum PlayStage
{
    /// <summary>Cooking the scene.</summary>
    Cook,

    /// <summary>Starting the player, or finding it in the installed SDK.</summary>
    Launch,

    /// <summary>The player loading the scene.</summary>
    Scene,

    /// <summary>The running player, which stopped unexpectedly.</summary>
    Run,
}
