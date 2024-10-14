// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.State;

using System.Collections.Generic;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Adapter for <see cref="IActiveRoute" /> so it can be used inside the
/// <see cref="RouterStateView" /> control.
/// </summary>
/// <param name="item">The item to be adapted.</param>
public partial class RouterStateAdapter(IActiveRoute item) : TreeItemAdapterBase, ITreeItem<IActiveRoute>
{
    public override bool IsRoot => this.Item.Parent == null;

    public IActiveRoute Item => item;

    public override string Label => this.IsRoot ? "<root>" : this.Item.ToString() ?? "invalid";

    public bool IsForOutlet => this.Item.Outlet.IsNotPrimary;

    public string Outlet => $"{(item.Outlet.IsPrimary ? "[primary]" : item.Outlet)}";

    public string Path
    {
        get
        {
            var fullPath = this.Item.Parent is null
                ? " / "
                : string.Join('/', this.Item.UrlSegments.Select(i => i.Path).ToList());
            return fullPath.Length == 0 ? "-empty-" : fullPath;
        }
    }

    public string ViewModel => this.Item.ViewModel?.GetType().Name ?? "-None-";

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
