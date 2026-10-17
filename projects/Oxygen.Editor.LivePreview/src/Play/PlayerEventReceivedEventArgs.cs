// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Player;

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>An event the player published.</summary>
/// <param name="received">The event.</param>
internal sealed class PlayerEventReceivedEventArgs(PlayerEvent received) : EventArgs
{
    /// <summary>Gets the event.</summary>
    public PlayerEvent Event { get; } = received;
}
