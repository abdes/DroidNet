// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for <see cref="DynamicTreeViewModel.ClipboardContentChanged" /> notifications.
/// </summary>
public sealed class ClipboardContentChangedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the new clipboard state after the change.
    /// </summary>
    public ClipboardState NewState { get; init; }

    /// <summary>
    ///     Gets the items currently stored in the clipboard.
    /// </summary>
    public IReadOnlyList<ITreeItem> Items { get; init; } = [];

    /// <summary>
    ///     Gets a value indicating whether the clipboard contents are still valid in the tree.
    /// </summary>
    public bool IsValid { get; init; }
}
