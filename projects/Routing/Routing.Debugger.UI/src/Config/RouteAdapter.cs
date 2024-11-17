// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Routing.Debugger.UI.TreeView;

namespace DroidNet.Routing.Debugger.UI.Config;

/// <summary>
/// Adapter for a <see cref="IRoute" /> so it can be used inside the <see cref="RoutesView" /> control.
/// </summary>
/// <param name="item">The route item to wrap.</param>
public partial class RouteAdapter(IRoute item) : TreeItemAdapterBase, ITreeItem<IRoute>
{
    /// <summary>
    /// The root path constant used to identify the root route.
    /// </summary>
    public const string RootPath = "__root";

    /// <inheritdoc/>
    public override bool IsRoot => string.Equals(this.Item.Path, RootPath, StringComparison.Ordinal);

    /// <inheritdoc/>
    public override string Label
        => this.IsRoot
            ? $"Config Root ({this.CountRoutesRecursive().ToString(CultureInfo.InvariantCulture)})"
            : $"{this.Item.Path ?? "(Matcher)"}";

    /// <inheritdoc/>
    public IRoute Item => item;

    /// <summary>
    /// Gets the outlet for which this route is specified.
    /// </summary>
    public string Outlet => this.Item.Outlet;

    /// <summary>
    /// Gets a value indicating whether this route is for a non-primary outlet.
    /// </summary>
    public bool IsForOutlet => this.Item.Outlet.IsNotPrimary;

    /// <summary>
    /// Gets the type name of the view model for this route.
    /// </summary>
    public string ViewModelType => this.Item.ViewModelType?.Name ?? "None";

    /// <inheritdoc/>
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

    /// <summary>
    /// Recursively counts the number of routes.
    /// </summary>
    /// <returns>The total number of routes including child routes.</returns>
    private int CountRoutesRecursive()
        => 0 + this.Children.Aggregate(0, (count, node) => count + ((RouteAdapter)node).CountRoutesRecursive());
}
