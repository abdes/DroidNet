// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.LivePreview.Play;

/// <summary>Reports that the player ended.</summary>
/// <param name="expected">Whether the end was orderly: the editor quit the player, or the player said it was stopping.</param>
internal sealed class PlayerEndedEventArgs(bool expected) : EventArgs
{
    /// <summary>Gets a value indicating whether the end was orderly rather than a crash.</summary>
    public bool Expected { get; } = expected;
}
