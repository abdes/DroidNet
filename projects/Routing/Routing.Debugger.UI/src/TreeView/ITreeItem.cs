// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Represents a tree item in the UI.
/// </summary>
public interface ITreeItem
{
    /// <summary>
    /// Gets or sets a value indicating whether the tree item is expanded.
    /// </summary>
    public bool IsExpanded { get; set; }

    /// <summary>
    /// Gets a value indicating whether the tree item is the root.
    /// </summary>
    public bool IsRoot { get; }

    /// <summary>
    /// Gets the children of the tree item.
    /// </summary>
    public IEnumerable<ITreeItem> Children { get; }

    /// <summary>
    /// Gets the label of the tree item.
    /// </summary>
    public string Label { get; }

    /// <summary>
    /// Gets a value indicating whether the tree item has children.
    /// </summary>
    public bool HasChildren { get; }
}

/// <summary>
/// Represents a tree item in the UI with a specific type.
/// </summary>
/// <typeparam name="T">The type of the item.</typeparam>
public interface ITreeItem<out T> : ITreeItem
{
    /// <summary>
    /// Gets the item of the tree item.
    /// </summary>
    public T Item { get; }
}
