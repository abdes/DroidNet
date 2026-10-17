// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>Reports a session preference the player did not apply.</summary>
/// <param name="message">Which preference, and why the player refused it.</param>
internal sealed class PlayerSettingRefusedEventArgs(string message) : EventArgs
{
    /// <summary>Gets which preference was refused, and why.</summary>
    public string Message { get; } = message;
}
