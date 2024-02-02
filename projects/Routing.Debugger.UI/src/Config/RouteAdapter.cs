// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Config;

using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Adapter for a <see cref="Route" /> so it can be used inside the <see cref="RoutesView" /> control.
/// </summary>
public partial class RouteAdapter : ObservableObject, ITreeItem<Route>
{
    public const string RootPath = "__root";

    private readonly Lazy<List<RouteAdapter>> children;

    [ObservableProperty]
    private bool isExpanded;

    public RouteAdapter(Route item)
    {
        this.Item = item;
        this.children = new Lazy<List<RouteAdapter>>(
            () =>
            {
                List<RouteAdapter> value = [];
                if (this.Item.Children is not null)
                {
                    value.AddRange(
                        this.Item.Children.Select(
                            child => new RouteAdapter(child)
                            {
                                Level = this.Level + 1,
                            }));
                }

                return value;
            });
    }

    public bool IsRoot => this.Item.Path == RootPath;

    public required int Level { get; init; }

    public Route Item { get; }

    public string Label
        => this.IsRoot
            ? $"Config Root ({this.CountRoutesRecursive()})"
            : $"{this.Item.Path ?? "(Matcher)" ?? "Invalid"}";

    public string Outlet => this.Item.Outlet;

    public string ExtendedLabel => this.Item.ToString();

    public bool IsForOutlet => this.Item.Outlet != UrlSegmentGroup.PrimaryOutlet;

    public bool HasChildren => this.ChildrenCount > 0;

    public int ChildrenCount => this.Item.Children?.Count ?? 0;

    public string ViewModelType => this.Item.ViewModelType?.Name ?? "None";

    IEnumerable<ITreeItem<Route>> ITreeItem<Route>.Children => this.children.Value;

    IEnumerable<ITreeItem> ITreeItem.Children => this.children.Value;

    private int CountRoutesRecursive()
        => 0 + this.children.Value.Aggregate(0, (count, node) => count + node.CountRoutesRecursive());
}
