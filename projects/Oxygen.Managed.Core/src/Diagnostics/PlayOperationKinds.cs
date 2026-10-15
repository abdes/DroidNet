// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Operation kinds of play sessions.
/// </summary>
public static class PlayOperationKinds
{
    /// <summary>
    /// Play or Restart: save, cook, start or reuse the player and begin a session.
    /// </summary>
    public const string Play = "Play.Start";

    /// <summary>
    /// The player ended while it was in use.
    /// </summary>
    public const string PlayerExited = "Play.PlayerExited";
}
