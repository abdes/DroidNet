// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeItem.Expand" /> and <see cref="DynamicTreeItem.Collapse" /> events.
/// </summary>
public class DynamicTreeEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the tree item associated with the event.
    /// </summary>
    /// <remarks>
    ///     The referenced <see cref="ITreeItem"/> identifies the logical node in the tree. It
    ///     should not be confused with a particular position in <see cref="DynamicTreeViewModel.ShownItems"/>,
    ///     which depends on expansion/collapse state.
    /// </remarks>
    public required ITreeItem TreeItem { get; init; }
}
