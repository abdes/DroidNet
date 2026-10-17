// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>What Play is doing.</summary>
public enum PlayActivity
{
    /// <summary>No player runs.</summary>
    Idle,

    /// <summary>Saving the open documents before a session.</summary>
    Saving,

    /// <summary>Cooking the scene before a session.</summary>
    Cooking,

    /// <summary>Starting the player.</summary>
    StartingPlayer,

    /// <summary>The player loads the scene.</summary>
    LoadingScene,

    /// <summary>A session runs.</summary>
    Playing,

    /// <summary>A session runs, paused.</summary>
    Paused,

    /// <summary>The player runs without a session, after one was ended from the player itself.</summary>
    PlayerIdle,

    /// <summary>The last Play failed; no player runs.</summary>
    Failed,
}
