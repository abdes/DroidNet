// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.Config;

using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Adapter for a <see cref="IRoute" /> so it can be used inside the <see cref="RoutesView" /> control.
/// </summary>
public class RouteAdapter(IRoute item) : TreeItemAdapterBase, ITreeItem<IRoute>
{
    public const string RootPath = "__root";

    public override bool IsRoot => this.Item.Path == RootPath;

    public override string Label
        => this.IsRoot
            ? $"Config Root ({this.CountRoutesRecursive()})"
            : $"{this.Item.Path ?? "(Matcher)"}";

    public IRoute Item => item;

    public string Outlet => this.Item.Outlet;

    public bool IsForOutlet => this.Item.Outlet.IsNotPrimary;

    public string ViewModelType => this.Item.ViewModelType?.Name ?? "None";

    protected override List<TreeItemAdapterBase> PopulateChildren()
    {
        List<RouteAdapter> value = [];
        if (this.Item.Children is not null)
        {
            value.AddRange(
                this.Item.Children.Select(
                    child => new RouteAdapter(child)
                    {
                        Level = this.Level + 1,
                        IsExpanded = true,
                    }));
        }

        return value.Cast<TreeItemAdapterBase>().ToList();
    }

    private int CountRoutesRecursive()
        => 0 + this.Children.Aggregate(0, (count, node) => count + ((RouteAdapter)node).CountRoutesRecursive());
}
