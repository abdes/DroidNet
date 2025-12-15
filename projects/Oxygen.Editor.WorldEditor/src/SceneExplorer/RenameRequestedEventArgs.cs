// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;

namespace Oxygen.Editor.WorldEditor.SceneExplorer;

/// <summary>
///     Event arguments for a rename request raised by the scene explorer.
/// </summary>
/// <param name="item">The tree item that is being requested to rename.</param>
/// <remarks>
///     This type is used with rename workflows where the UI or caller requests that the given
///     <see cref="ITreeItem"/> be renamed. Handlers can inspect <see cref="Item"/> to determine the
///     target for the rename operation.
/// </remarks>
public class RenameRequestedEventArgs(ITreeItem item) : EventArgs
{
    /// <summary>
    ///     Gets the item that is requested to be renamed.
    /// </summary>
    public ITreeItem Item { get; } = item;
}
