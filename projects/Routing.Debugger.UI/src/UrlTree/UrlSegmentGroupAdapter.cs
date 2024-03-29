// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>Adapter for a <see cref="IUrlSegmentGroup" /> to be used inside a <see cref="UrlTreeView" /> control.</summary>
/// <param name="item">The item to wrap.</param>
public class UrlSegmentGroupAdapter(IUrlSegmentGroup item) : TreeItemAdapterBase, ITreeItem<IUrlSegmentGroup>
{
    private readonly OutletName outlet;

    public ReadOnlyCollection<IUrlSegment> Segments => this.Item.Segments;

    public required int IndexInItems { get; init; }

    public required string Outlet
    {
        get => $"{(this.outlet.IsPrimary ? "[primary]" : this.outlet)}";
        [MemberNotNull(nameof(outlet))]
        init => this.outlet = value;
    }

    public IUrlSegmentGroup Item => item;

    public override string Label
    {
        get
        {
            var fullPath = this.Item.Parent is null
                ? " / "
                : string.Join('/', this.Item.Segments.Select(i => i.Path).ToList());
            return fullPath.Length == 0 ? "-empty-" : fullPath;
        }
    }

    public override bool IsRoot => this.Item.Parent is null;

    protected override List<TreeItemAdapterBase> PopulateChildren()
    {
        List<UrlSegmentGroupAdapter> value = [];
        var childIndexInItems = this.IndexInItems + 1;
        foreach (var child in this.Item.Children)
        {
            value.Add(
                new UrlSegmentGroupAdapter(child.Value)
                {
                    IndexInItems = childIndexInItems,
                    Level = this.Level + 1,
                    Outlet = child.Key,
                    IsExpanded = true,
                });
            ++childIndexInItems;
        }

        return value.Cast<TreeItemAdapterBase>().ToList();
    }
}
