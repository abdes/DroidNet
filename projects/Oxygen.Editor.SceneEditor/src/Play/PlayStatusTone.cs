// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Play;

/// <summary>How the Play status shows.</summary>
public enum PlayStatusTone
{
    /// <summary>A grey dot: ready, paused, or a player without a session.</summary>
    Neutral,

    /// <summary>A progress ring: Play is on its way.</summary>
    Busy,

    /// <summary>A green dot: playing.</summary>
    Success,

    /// <summary>An amber dot: playing a scene that changed since.</summary>
    Caution,

    /// <summary>An error badge: the last Play failed.</summary>
    Critical,
}
