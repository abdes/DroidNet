// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.Demo.Tree;

/// <summary>
/// Event arguments for a rename request originating from the ViewModel.
/// </summary>
internal sealed class RenameRequestedEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="RenameRequestedEventArgs"/> class.
    /// </summary>
    /// <param name="item">The item to rename, may be null.</param>
    public RenameRequestedEventArgs(ITreeItem? item)
    {
        this.Item = item;
    }

    /// <summary>
    /// Gets the item that should be renamed (may be null).
    /// </summary>
    public ITreeItem? Item { get; }
}
