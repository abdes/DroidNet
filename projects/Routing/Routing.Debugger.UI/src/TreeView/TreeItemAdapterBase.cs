// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.TreeView;

using CommunityToolkit.Mvvm.ComponentModel;

/// <summary>A base class for implementing tree item adapters.</summary>
public abstract partial class TreeItemAdapterBase : ObservableObject, ITreeItem
{
    private readonly Lazy<IList<TreeItemAdapterBase>> children;

    [ObservableProperty]
    private bool isExpanded;

    protected TreeItemAdapterBase()
        => this.children = new Lazy<IList<TreeItemAdapterBase>>(this.PopulateChildren);

    public abstract bool IsRoot { get; }

    public abstract string Label { get; }

    public bool HasChildren => this.children.Value.Count > 0;

    public IEnumerable<ITreeItem> Children => this.children.Value;

    public required int Level { get; init; }

    protected abstract IList<TreeItemAdapterBase> PopulateChildren();
}
