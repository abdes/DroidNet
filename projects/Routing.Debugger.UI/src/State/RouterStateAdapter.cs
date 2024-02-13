// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.State;

using System.Collections.Generic;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Adapter for <see cref="IActiveRoute" /> so it can be used inside the
/// <see cref="RouterStateView" /> control.
/// </summary>
public partial class RouterStateAdapter : ObservableObject, ITreeItem<IActiveRoute>
{
    private readonly Lazy<List<RouterStateAdapter>> children;

    [ObservableProperty]
    private bool isExpanded;

    public RouterStateAdapter(IActiveRoute item)
    {
        this.Item = item;
        this.children = new Lazy<List<RouterStateAdapter>>(
            () =>
            {
                List<RouterStateAdapter> value = [];
                value.AddRange(
                    this.Item.Children.Select(
                        child => new RouterStateAdapter(child)
                        {
                            Level = this.Level + 1,
                            IsExpanded = true,
                        }));

                return value;
            });
    }

    public bool IsRoot => this.Item.Parent == null;

    public required int Level { get; init; }

    public IActiveRoute Item { get; }

    public string Label => this.IsRoot ? "<root>" : this.Item.ToString() ?? "invalid";

    public bool IsForOutlet => this.Item.Outlet.IsNotPrimary;

    public bool HasChildren => this.ChildrenCount > 0;

    public int ChildrenCount => this.Item.Children.Count;

    public string ViewModel => $"ViewModel = {this.Item.ViewModel?.GetType().Name}";

    IEnumerable<ITreeItem> ITreeItem.Children => this.children.Value;
}
