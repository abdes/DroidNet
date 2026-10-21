// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>The player's output, as the editor receives it.</summary>
/// <remarks>Its events are raised on the thread that owns the play session, the UI thread.</remarks>
public interface IPlayerLog
{
    /// <summary>Occurs for each line of the player's output, and with a clear when a session starts.</summary>
    public event EventHandler<PlayerLogEventArgs>? LogReceived;
}
