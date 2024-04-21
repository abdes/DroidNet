// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Workspace;

using System.Diagnostics;
using DroidNet.Docking.Detail;

/// <summary>Represents a grouping of other groups, or <see cref="Dockable" />s.</summary>
/// <param name="docker">The <see cref="IDocker" /> managing this group.</param>
/// <param name="orientation">The desired orientation of this group.</param>
/// <remarks>
/// <para>
/// If a group has dockables, then it is a leaf group with both slots (First and Second) empty. Otherwise, group composition is
/// used to construct a tree of groups based on their <see cref="DockGroupOrientation" />.
/// </para>
/// <para>
/// The tree od dock groups is a binary tree, where each node only has two children, and where the order of the children matters.
/// When a group needs to expand, it is split in two parts, one of them will hold the old group, and the other one will hold the
/// new group. Such structure is easy to visualize using simple nesting of two-colum (Horizontal) or two-row (Vertical) grids.
/// </para>
/// </remarks>
internal sealed partial class LayoutDockGroup(
    IDocker docker,
    DockGroupOrientation orientation = DockGroupOrientation.Undetermined) : DockGroup(docker, orientation), IDisposable
{
    private bool disposed;

    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        // LayoutDockGroup owns the docks and should dispose of them.
        this.ClearDocks(dispose: true);

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    internal void ClearDocks(bool dispose)
    {
        if (dispose)
        {
            foreach (var dock in this.Docks)
            {
                if (dock is IDisposable resource)
                {
                    resource.Dispose();
                }
            }
        }

        this.ClearDocks();
    }

    internal void MigrateDocks(LayoutDockGroup toGroup)
    {
        foreach (var dock in this.Docks)
        {
            toGroup.AddDock(dock);
        }

        this.ClearDocks(dispose: false);
    }
}

/// <summary>
/// Dock management mix-in for the <see cref="LayoutDockGroup" /> class.
/// </summary>
internal partial class LayoutDockGroup
{
    /// <summary>Adds a dock to the group at the last position. Use the version with an <see cref="Anchor" /> to position the dock
    /// relatively to an existing dock in the group.</summary>
    /// <param name="dock">The dock to be added to this group.</param>
    internal override void AddDock(IDock dock)
    {
        base.AddDock(dock);

        // The LayoutDockGroup owns its docks, so we set the Group property of the dock.
        dock.AsDock().Group = this;
    }

    internal override void AddDock(IDock dock, Anchor anchor)
    {
        // Check that the anchor dock belongs to this group.
        var anchorDockId = anchor.RelativeTo?.Owner?.Id ?? throw new ArgumentException(
            $"invalid anchor for relative docking: {anchor}",
            nameof(anchor));

        var relativeTo = this.Docks.FirstOrDefault(d => d.Id.Equals(anchorDockId)) ??
                         throw new ArgumentException(
                             $"dock with id={anchorDockId} does not belong to me: {this}",
                             nameof(anchor));

        // If the group is empty or has a single item, its orientation can still be redefined. We determine the required
        // orientation based on the requested relative positioning.
        var requiredOrientation = anchor.Position is AnchorPosition.Left or AnchorPosition.Right
            ? DockGroupOrientation.Horizontal
            : DockGroupOrientation.Vertical;

        if (this.Docks.Count <= 1)
        {
            this.Orientation = requiredOrientation;
        }

        Debug.Assert(
            this.Orientation == requiredOrientation,
            $"unexpected orientation mismatch for group {this} when addding a dock");

        var insertPosition = 0;
        if (this.Docks.Count != 0)
        {
            var relativeToPosition = this.Docks.IndexOf(relativeTo);
            insertPosition = anchor.Position is AnchorPosition.Left or AnchorPosition.Top
                ? int.Max(0, relativeToPosition - 1)
                : relativeToPosition + 1;
        }

        if (this.Orientation == DockGroupOrientation.Vertical)
        {
            var height = new Height(relativeTo.Height.Half());
            relativeTo.AsDock().Height = height;
            dock.AsDock().Height = height;
        }
        else
        {
            var width = new Width(relativeTo.Width.Half());
            relativeTo.AsDock().Width = width;
            dock.AsDock().Width = width;
        }

        this.InsertDock(insertPosition, dock);

        // The LayoutDockGroup owns its docks, so we set the Group property of the dock.
        dock.AsDock().Group = this;
    }

    internal override bool RemoveDock(IDock dock)
    {
        var found = base.RemoveDock(dock);
        if (!found)
        {
            throw new InvalidOperationException(
                $"attempt to a remove dock with Id={dock.Id} from group Id={this}, but the dock was not there.");
        }

        if (this.Docks.Count < 2)
        {
            this.Orientation = DockGroupOrientation.Undetermined;
        }

        // The LayoutDockGroup no longer owns this dock, so we clear the Group property of the dock.
        dock.AsDock().Group = null;

        return true;
    }

    internal (LayoutDockGroup? beforeGroup, LayoutDockGroup hostGroup, LayoutDockGroup? afterGroup) Split(
        IDock? relativeTo,
        DockGroupOrientation requiredOrientation)
    {
        var items = this.Docks.ToList();
        var relativeToIndex = relativeTo is null ? 0 : this.Docks.IndexOf(relativeTo);
        if (relativeToIndex == -1)
        {
            throw new ArgumentException(
                $"Relative dock `{relativeTo}` does not belong to this dock group Id=`{this}`",
                nameof(relativeTo));
        }

        var before = items[..relativeToIndex];
        var relativeItem = items[relativeToIndex];
        var after = items[(relativeToIndex + 1)..];

        var hostGroup = new LayoutDockGroup(this.Docker, requiredOrientation);
        hostGroup.AddDock(relativeItem);

        var beforeGroup = before.Count != 0
            ? new LayoutDockGroup(this.Docker, before.Count > 1 ? this.Orientation : DockGroupOrientation.Undetermined)
            : null;
        if (beforeGroup != null)
        {
            foreach (var item in before)
            {
                item.AsDock().Group = beforeGroup;
                beforeGroup.AddDock(item);
            }
        }

        var afterGroup = after.Count != 0
            ? new LayoutDockGroup(this.Docker, after.Count > 1 ? this.Orientation : DockGroupOrientation.Undetermined)
            : null;
        if (afterGroup != null)
        {
            foreach (var item in after)
            {
                item.AsDock().Group = afterGroup;
                afterGroup.AddDock(item);
            }
        }

        // Do not dispose of the docks in this group as they have been reused
        // and moved around to form the split result.
        this.ClearDocks(dispose: false);

        return (beforeGroup, hostGroup, afterGroup);
    }
}
