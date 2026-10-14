// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// Event arguments for a rename request originating from the ViewModel.
/// </summary>
/// <param name="item">The item to rename, may be null.</param>
internal sealed class RenameRequestedEventArgs(ITreeItem? item) : EventArgs
{
    /// <summary>
    /// Gets the item that should be renamed (may be null).
    /// </summary>
    public ITreeItem? Item { get; } = item;
}
