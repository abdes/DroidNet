// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Player;

/// <summary>An event the player published to a subscribed topic.</summary>
/// <param name="received">The event.</param>
public sealed class PlayerEventReceivedEventArgs(PlayerEvent received) : EventArgs
{
    /// <summary>Gets the event.</summary>
    public PlayerEvent Event { get; } = received;
}
