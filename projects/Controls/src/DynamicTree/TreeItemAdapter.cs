// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System;
using System.Collections.ObjectModel;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.UI.Xaml;

/// <summary>A base class for implementing tree item adapters.</summary>
public abstract partial class TreeItemAdapter : ObservableObject, ITreeItem
{
    private readonly ObservableCollection<ITreeItem> children = [];
    private readonly Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>> childrenLazy;

    [ObservableProperty]
    private bool isExpanded;

    [ObservableProperty]
    private bool isSelected;

    protected TreeItemAdapter() => this.childrenLazy
        = new Lazy<Task<ReadOnlyObservableCollection<ITreeItem>>>(this.InitializeChildrenCollectionAsync);

    public virtual bool IsRoot => this.Parent is null;

    public abstract string Label { get; }

    public ITreeItem? Parent { get; private set; }

    public Task<ReadOnlyObservableCollection<ITreeItem>> Children => this.childrenLazy.Value;

    public int Depth => this.Parent is null ? -1 : this.Parent.Depth + 1;

    public TreeItemAdapter Self => this;

    public static Visibility TreeItemHasChildren(TreeItemAdapter item)
        => item.HasChildren().GetAwaiter().GetResult() ? Visibility.Visible : Visibility.Collapsed;

    public async Task<bool> HasChildren()
    {
        var collection = await this.Children.ConfigureAwait(false);
        return collection.Count > 0;
    }

    public async Task AddChildAsync(ITreeItem child) => await this.ManipulateChildrenAsync(
            this.AddChildInternal,
            child)
        .ConfigureAwait(false);

    public async Task InsertChildAsync(int index, ITreeItem child) => await this.ManipulateChildrenAsync(
            (TreeItemAdapter item) =>
            {
                this.children.Insert(index, item);
                item.Parent = this;
            },
            child)
        .ConfigureAwait(false);

    public async Task RemoveChildAsync(ITreeItem child) => await this.ManipulateChildrenAsync(
            (TreeItemAdapter item) =>
            {
                if (this.children.Remove(item))
                {
                    item.Parent = null;
                }
            },
            child)
        .ConfigureAwait(false);

    protected abstract Task LoadChildren();

    protected void AddChildInternal(TreeItemAdapter item)
    {
        this.children.Add(item);
        item.Parent = this;
    }

    private async Task ManipulateChildrenAsync(Action<TreeItemAdapter> action, ITreeItem item)
    {
        if (item is TreeItemAdapter adapter)
        {
            _ = await this.Children.ConfigureAwait(false);
            action(adapter);
        }
        else
        {
            throw new ArgumentException(
                $"item has a type `{item.GetType()}` that not derive from `{typeof(TreeItemAdapter)}`",
                nameof(item));
        }
    }

    private async Task<ReadOnlyObservableCollection<ITreeItem>> InitializeChildrenCollectionAsync()
    {
        await this.LoadChildren().ConfigureAwait(false);
        return new ReadOnlyObservableCollection<ITreeItem>(new ObservableCollection<ITreeItem>(this.children));
    }
}
