// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Aura.Controls;

/// <summary>
///     Provides data for the <see cref="TabStrip.SelectionChanged"/> event.
/// </summary>
public sealed class TabSelectionChangedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the tab item that was previously selected, or <see langword="null"/> if no tab was selected.
    /// </summary>
    public TabItem? OldItem { get; init; }

    /// <summary>
    ///     Gets the tab item that is now selected, or <see langword="null"/> if the selection was cleared.
    /// </summary>
    public TabItem? NewItem { get; init; }

    /// <summary>
    ///     Gets the index of the previously selected tab in the <see cref="TabStrip.Items"/> collection,
    ///     or <c>-1</c> if no tab was selected.
    /// </summary>
    public int OldIndex { get; init; } = -1;

    /// <summary>
    ///     Gets the index of the newly selected tab in the <see cref="TabStrip.Items"/> collection,
    ///     or <c>-1</c> if the selection was cleared.
    /// </summary>
    public int NewIndex { get; init; } = -1;
}
