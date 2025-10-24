// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.ComponentModel;

namespace DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>A base class for implementing tree item adapters.</summary>
public abstract partial class TreeItemAdapterBase : ObservableObject, ITreeItem
{
    private readonly Lazy<IList<TreeItemAdapterBase>> children;

    /// <summary>
    /// Initializes a new instance of the <see cref="TreeItemAdapterBase"/> class.
    /// </summary>
    protected TreeItemAdapterBase()
    {
        this.children = new Lazy<IList<TreeItemAdapterBase>>(this.PopulateChildren);
    }

    [ObservableProperty]
    public partial bool IsExpanded { get; set; }

    /// <inheritdoc/>
    public abstract bool IsRoot { get; }

    /// <inheritdoc/>
    public abstract string Label { get; }

    /// <inheritdoc/>
    public bool HasChildren => this.children.Value.Count > 0;

    /// <inheritdoc/>
    public IEnumerable<ITreeItem> Children => this.children.Value;

    /// <summary>
    /// Gets the level of the tree item.
    /// </summary>
    public required int Level { get; init; }

    /// <summary>
    /// Populates the children of the tree item.
    /// </summary>
    /// <returns>A list of child tree items.</returns>
    protected abstract IList<TreeItemAdapterBase> PopulateChildren();
}
