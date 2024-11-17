// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Routing.Debugger.UI.TreeView;

namespace DroidNet.Routing.Debugger.UI.UrlTree;

/// <summary>
/// Adapter for a <see cref="IUrlSegmentGroup" /> to be used inside a <see cref="UrlTreeView" /> control.
/// </summary>
/// <param name="item">The item to wrap.</param>
public partial class UrlSegmentGroupAdapter(IUrlSegmentGroup item) : TreeItemAdapterBase, ITreeItem<IUrlSegmentGroup>
{
    private readonly OutletName outlet;

    /// <summary>
    /// Gets the segments belonging to this group's primary navigation path.
    /// </summary>
    /// <remarks>
    /// These segments represent the sequential parts of the URL path for this group. For instance,
    /// in a URL path "users/123", a segment group would contain two segments. The segments maintain
    /// their original order, which is crucial for proper route matching and parameter extraction.
    /// </remarks>
    public ReadOnlyCollection<IUrlSegment> Segments => this.Item.Segments;

    /// <summary>
    /// Gets the index of this group in the collection of items.
    /// </summary>
    /// <value>
    /// The zero-based index of this group in the collection of items.
    /// </value>
    public required int IndexInItems { get; init; }

    /// <summary>
    /// Gets the outlet name for this URL segment group.
    /// </summary>
    /// <value>
    /// A string representing the outlet name. If the outlet is primary, it returns "[primary]".
    /// </value>
    /// <exception cref="ArgumentNullException">
    /// Thrown when the outlet name is set to null.
    /// </exception>
    public required string Outlet
    {
        get => $"{(this.outlet.IsPrimary ? "[primary]" : this.outlet)}";
        [MemberNotNull(nameof(outlet))]
        init => this.outlet = value;
    }

    /// <inheritdoc/>
    public IUrlSegmentGroup Item => item;

    /// <inheritdoc/>
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

    /// <inheritdoc/>
    public override bool IsRoot => this.Item.Parent is null;

    /// <inheritdoc/>
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
