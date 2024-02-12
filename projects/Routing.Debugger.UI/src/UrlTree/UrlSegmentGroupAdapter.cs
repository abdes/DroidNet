// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.UrlTree;

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Routing.Debugger.UI.TreeView;

/// <summary>
/// Adapter for a <see cref="IUrlSegmentGroup" /> so it can be used inside the
/// <see cref="UrlTreeView" /> control.
/// </summary>
public partial class UrlSegmentGroupAdapter : ObservableObject, ITreeItem<IUrlSegmentGroup>
{
    private readonly Lazy<List<UrlSegmentGroupAdapter>> children;

    private readonly OutletName outlet;

    [ObservableProperty]
    private bool isExpanded;

    public UrlSegmentGroupAdapter(IUrlSegmentGroup item)
    {
        this.Item = item;
        this.children = new Lazy<List<UrlSegmentGroupAdapter>>(
            () =>
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
                        });
                    ++childIndexInItems;
                }

                return value;
            });
    }

    public ReadOnlyCollection<IUrlSegment> Segments => this.Item.Segments;

    public required int IndexInItems { get; init; }

    public required int Level { get; init; }

    public required string Outlet
    {
        get => $"[{(this.outlet.IsPrimary ? "primary" : this.outlet)}]";
        [MemberNotNull(nameof(outlet))]
        init => this.outlet = value;
    }

    public IUrlSegmentGroup Item { get; }

    public string Label => this.Item.Parent is null ? " / " : this.Item.ToString() ?? string.Empty;

    public bool HasChildren => this.ChildrenCount > 0;

    public int ChildrenCount => this.Item.Children.Count;

    public bool IsRoot => this.Item.Parent is null;

    public List<UrlSegmentGroupAdapter> Children => this.children.Value;

    IEnumerable<ITreeItem<IUrlSegmentGroup>> ITreeItem<IUrlSegmentGroup>.Children => this.children.Value;

    IEnumerable<ITreeItem> ITreeItem.Children => this.children.Value;
}
