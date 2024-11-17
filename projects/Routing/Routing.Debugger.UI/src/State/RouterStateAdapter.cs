// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Routing.Debugger.UI.TreeView;

namespace DroidNet.Routing.Debugger.UI.State;

/// <summary>
/// Adapter for <see cref="IActiveRoute" /> so it can be used inside the
/// <see cref="RouterStateView" /> control.
/// </summary>
/// <param name="item">The item to be adapted.</param>
public partial class RouterStateAdapter(IActiveRoute item) : TreeItemAdapterBase, ITreeItem<IActiveRoute>
{
    /// <inheritdoc/>
    public override bool IsRoot => this.Item.Parent == null;

    /// <inheritdoc/>
    public IActiveRoute Item => item;

    /// <inheritdoc/>
    public override string Label => this.IsRoot ? "<root>" : this.Item.ToString() ?? "invalid";

    /// <summary>
    /// Gets a value indicating whether the route is for an outlet that is not primary.
    /// </summary>
    /// <value>
    /// <see langword="true"/> if the route is for a non-primary outlet; otherwise, <see langword="false"/>.
    /// </value>
    public bool IsForOutlet => this.Item.Outlet.IsNotPrimary;

    /// <summary>
    /// Gets the outlet name where the route's view model content will be loaded during activation.
    /// </summary>
    /// <value>
    /// A string representing the outlet name. If the outlet is primary, it returns "[primary]";
    /// otherwise, it returns the outlet name.
    /// </value>
    public string Outlet => $"{(item.Outlet.IsPrimary ? "[primary]" : item.Outlet)}";

    /// <summary>
    /// Gets the full path of the route.
    /// </summary>
    /// <value>
    /// A string representing the full path of the route. If the route has no parent, it returns " / ".
    /// If the path is empty, it returns "-empty-".
    /// </value>
    public string Path
    {
        get
        {
            var fullPath = this.Item.Parent is null
                ? " / "
                : string.Join('/', this.Item.Segments.Select(i => i.Path).ToList());
            return fullPath.Length == 0 ? "-empty-" : fullPath;
        }
    }

    /// <summary>
    /// Gets the name of the view model associated with the route.
    /// </summary>
    /// <value>
    /// A string representing the name of the view model type. If the view model is not set, it returns <see langword="-None-"/>.
    /// </value>
    public string ViewModel => this.Item.ViewModel?.GetType().Name ?? "-None-";

    /// <inheritdoc/>
    protected override List<TreeItemAdapterBase> PopulateChildren()
    {
        List<RouterStateAdapter> value = [];
        value.AddRange(
            this.Item.Children.Select(
                child => new RouterStateAdapter(child)
                {
                    Level = this.Level + 1,
                    IsExpanded = true,
                }));

        return value.Cast<TreeItemAdapterBase>().ToList();
    }
}
