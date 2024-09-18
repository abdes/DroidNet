// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.TreeView;

using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>A base class for implementing tree item adapters.</summary>
public abstract partial class TreeItemAdapter : ObservableObject, ITreeItem
{
    private readonly Lazy<IList<TreeItemAdapter>> children;

    [ObservableProperty]
    private bool isExpanded;

    protected TreeItemAdapter()
        => this.children = new Lazy<IList<TreeItemAdapter>>(this.PopulateChildren);

    public abstract bool IsRoot { get; }

    public abstract string Label { get; }

    public bool HasChildren => this.children.Value.Count > 0;

    public IEnumerable<ITreeItem> Children => this.children.Value;

    public required int Level { get; init; }

    protected abstract IList<TreeItemAdapter> PopulateChildren();
}
