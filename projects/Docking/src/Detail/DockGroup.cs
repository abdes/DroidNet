// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using System.Diagnostics;
using DroidNet.Docking.Utils;

/// <summary>
/// Represents a grouping of other groups, or <see cref="Dockable" />s.
/// </summary>
/// <remarks>
/// <para>
/// If a group has dockables, then it is a leaf group with both slots (First and
/// Second) empty. Otherwise, group composition is used to construct a tree of
/// groups based on their <see cref="DockGroupOrientation" />.
/// </para>
/// <para>
/// The tree od dock groups is a binary tree, where each node only has two
/// children, and where the order of the children matters. When a group needs to
/// expand, it is split in two parts, one of them will hold the old group, and
/// the other one will hold the new group. Such structure is easy to visualize
/// using simple nesting of two-colum (Horizontal) or two-row (Vertical) grids.
/// </para>
/// </remarks>
internal partial class DockGroup : DockGroupBase
{
    private readonly ObservableCollection<IDock> docks = [];

    private bool disposed;
    private DockGroupBase? first;
    private DockGroupBase? second;

    internal DockGroup(IDocker docker)
        : base(docker) => this.Docks = new ReadOnlyObservableCollection<IDock>(this.docks);

    // TODO: rename this to something less confusing such as HasNoDocks
    public override bool IsEmpty => this.docks.Count == 0;

    public override ReadOnlyObservableCollection<IDock> Docks { get; }

    public override IDockGroup? First
    {
        get => this.first;
        protected set => this.SetPart((DockGroupBase?)value, out this.first);
    }

    public override IDockGroup? Second
    {
        get => this.second;
        protected set => this.SetPart((DockGroupBase?)value, out this.second);
    }

    public IDockGroup? Sibling
        => this.Parent is null ? null : this.Parent.First == this ? this.Parent.Second : this.Parent.First;

    internal bool IsLeaf => this is { First: null, Second: null };

    public override string ToString()
    {
        string[] children =
        [
            this.first is null ? string.Empty : $"{this.first.DebugId}",
            this.second is null ? string.Empty : $"{this.second.DebugId}",
        ];
        var childrenStr = children[0] != string.Empty || children[1] != string.Empty
            ? $" {{{string.Join(',', children)}}}"
            : string.Empty;

        var orientation = this.Orientation.ToSymbol();

        return
            $"{orientation}{(this.IsCenter ? " \u25cb" : string.Empty)} {this.DebugId} ({string.Join(',', this.Docks)}){childrenStr}";
    }

    public override void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        base.Dispose();
        this.first = null;
        this.second = null;

        this.ClearDocks();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    public void AssimilateChild(DockGroup child)
    {
        Debug.Assert(this.First is null || this.Second is null, "only a lone child can be assimilated by its parent");
        Debug.Assert(child is { IsCenter: false, IsEdge: false }, "root groups cannot be assimilated");

        if (child.IsLeaf)
        {
            child.MigrateDocksToGroup(this);
            this.First = this.Second = null;
        }
        else
        {
            this.First = child.First;
            this.Second = child.Second;
        }

        this.Orientation = child.Orientation;

        child.Parent = null;
    }

    internal void MergeLeafParts()
    {
        var firstChild = this.First?.AsDockGroup();
        var secondChild = this.Second?.AsDockGroup();

        if (firstChild is null || secondChild is null)
        {
            throw new InvalidOperationException("cannot merge null parts");
        }

        if (!firstChild.IsLeaf || !secondChild.IsLeaf)
        {
            throw new InvalidOperationException("can only merge parts if both parts are leaves");
        }

        if (firstChild.IsCenter || secondChild.IsCenter)
        {
            throw new InvalidOperationException(
                $"cannot merge parts when one of them is the center group: {(firstChild.IsCenter ? firstChild : secondChild)}");
        }

        secondChild.MigrateDocksToGroup(firstChild);
        this.RemoveGroup(secondChild);
    }

    private void ClearDocks()
    {
        foreach (var dock in this.docks)
        {
            if (dock is IDisposable resource)
            {
                resource.Dispose();
            }
        }

        this.docks.Clear();
    }

    private void SetPart(DockGroupBase? value, out DockGroupBase? part)
    {
        // Avoid bugs with infinite recursion in the tree if the part is self.
        if (value == this)
        {
            throw new InvalidOperationException("cannot add self as a part");
        }

        if (value != null && !this.IsEmpty)
        {
            throw new InvalidOperationException("cannot add parts to a non-empty group");
        }

        part = value;
        if (part != null)
        {
            part.Parent = this;
        }
    }
}

/// <summary>
/// Group management mix-in for the <see cref="DockGroup" /> class.
/// </summary>
internal partial class DockGroup
{
    internal virtual void RemoveGroup(IDockGroup group)
    {
        if (group.IsCenter)
        {
            // We cannot remove the center group, so just return.
            return;
        }

        var mine = false;
        if (this.First == group)
        {
            mine = true;
            this.First = null;
        }
        else if (this.Second == group)
        {
            mine = true;
            this.Second = null;
        }

        if (!mine)
        {
            throw new InvalidOperationException($"group with Id={group} is not one of my(Id={this.DebugId}) children");
        }

        this.ConsolidateUp();
    }

    internal void AddGroupLast(IDockGroup group, DockGroupOrientation orientation)
        => this.AddGroup(group, false, orientation);

    internal void AddGroupFirst(IDockGroup group, DockGroupOrientation orientation)
        => this.AddGroup(group, true, orientation);

    internal void AddGroupAfter(IDockGroup group, IDockGroup sibling, DockGroupOrientation orientation)
        => this.AddGroupRelativeTo(group, sibling, true, orientation);

    internal void AddGroupBefore(IDockGroup group, IDockGroup sibling, DockGroupOrientation orientation)
        => this.AddGroupRelativeTo(group, sibling, false, orientation);

    protected virtual void MigrateDocksToGroup(DockGroup group)
    {
        foreach (var item in this.docks)
        {
            group.docks.Add(item);
            item.AsDock().Group = group;
        }

        // Do not dispose of the docks here as we just moved them somewhere else
        this.docks.Clear();
    }

    private void AddGroup(IDockGroup group, bool isFirst, DockGroupOrientation orientation)
    {
        if (this.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Orientation = orientation;
        }

        if (!this.IsEmpty)
        {
            var migrated = this.MigrateDocksToNewGroup();
            this.Orientation = orientation;
            this.First = isFirst ? group : migrated;
            this.Second = isFirst ? migrated : group;
        }
        else if (this.first is null && this.second is null)
        {
            // If the group is empty and both slots are free, just use the first
            // slot.
            Debug.Assert(
                this.Orientation == orientation,
                "empty group with children, should not have an orientation on its own");
            this.First = group;
        }
        else
        {
            // If only one of the slots is full, we may need to swap them so that we
            // make the free one at the position we need to add to.
            if ((this.First is not null && this.Second is null && isFirst) ||
                (this.Second is not null && this.First is null && !isFirst))
            {
                this.SwapFirstAndSecond();
            }

            // At this point we either have the slot we need empty, and we can add
            // in it immediately, or full, and we need to expand it.
            if (isFirst)
            {
                var firstImpl = this.first as DockGroup;
                this.First = firstImpl?.ExpandToAdd(group, orientation, isFirst) ?? group;
            }
            else
            {
                var secondImpl = this.second as DockGroup;
                this.Second = secondImpl?.ExpandToAdd(group, orientation, isFirst) ?? group;
            }
        }

        this.ConsolidateUp();
    }

    // Expand the group by making a new group, replacing our second
    // part with it, and move our second part and the group to add to
    // it.
    private DockGroup ExpandToAdd(IDockGroup newGroup, DockGroupOrientation orientation, bool newGroupFirst)
    {
        var expanded = new DockGroup(this.Docker)
        {
            First = newGroupFirst ? newGroup : this,
            Second = newGroupFirst ? this : newGroup,
            Orientation = this.Orientation == DockGroupOrientation.Undetermined ? orientation : this.Orientation,
        };
        newGroup.AsDockGroup().Orientation = orientation;

        return expanded;
    }

    private DockGroup MigrateDocksToNewGroup()
    {
        var migrate = new DockGroup(this.Docker);

        this.MigrateDocksToGroup(migrate);
        migrate.Orientation = migrate.Docks.Count > 1 ? this.Orientation : DockGroupOrientation.Undetermined;

        return migrate;
    }

    private void SwapFirstAndSecond() => (this.First, this.Second) = (this.Second, this.First);

    private void AddGroupRelativeTo(IDockGroup group, IDockGroup sibling, bool after, DockGroupOrientation orientation)
    {
        if (this.First != sibling && this.Second != sibling)
        {
            throw new InvalidOperationException($"relative group ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling,
        // then it must be empty.
        Debug.Assert(this.IsEmpty, "a group with parts can only be empty");

        if (this.Orientation == DockGroupOrientation.Undetermined)
        {
            this.Orientation = orientation;
        }

        // If both slots are full or if the requested orientation is not the
        // current group orientation, then we must expand to add.
        if (this.Orientation != orientation || (this.first != null && this.second != null))
        {
            if (this.first == sibling)
            {
                var firstImpl = this.first as DockGroup;
                Debug.Assert(
                    firstImpl != null,
                    $"expecting {this.first} to be of a concrete implementation {nameof(DockGroup)}");
                this.First = firstImpl.ExpandToAdd(group, orientation, !after);
            }
            else if (this.second == sibling)
            {
                var secondImpl = this.second as DockGroup;
                Debug.Assert(
                    secondImpl != null,
                    $"expecting {this.first} to be of a concrete implementation {nameof(DockGroup)}");
                this.Second = secondImpl.ExpandToAdd(group, orientation, !after);
            }
        }
        else
        {
            if ((this.first is null && after) || (this.second is null && !after))
            {
                this.SwapFirstAndSecond();
            }

            if (this.first is null)
            {
                this.First = group;
            }
            else if (this.second is null)
            {
                this.Second = group;
            }
        }

        this.ConsolidateUp();
    }
}

/// <summary>
/// Dock management mix-in for the <see cref="DockGroup" /> class.
/// </summary>
internal partial class DockGroup
{
    /// <summary>
    /// Add the first dock to an empty group. Can only be used if the group is
    /// empty; otherwise, use the version with anchor.
    /// </summary>
    /// <param name="dock">The dock to be added to this group.</param>
    /// <exception cref="InvalidOperationException">
    /// If the group is not empty.
    /// </exception>
    internal virtual void AddDock(IDock dock)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        if (!this.IsEmpty)
        {
            throw new InvalidOperationException(
                $"method {nameof(this.AddDock)} with no anchor should only be used when the group is empty");
        }

        dock.AsDock().Group = this;

        // TODO(abdes): make insert position for new docks configurable
        this.docks.Add(dock);
    }

    internal virtual void AddDock(IDock dock, Anchor anchor)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        // Check that the anchor dock belongs to this group.
        var anchorDockId = anchor.RelativeTo?.Owner?.Id ?? throw new ArgumentException(
            $"invalid anchor for relative docking: {anchor}",
            nameof(anchor));
        var relativeTo = this.Docks.FirstOrDefault(d => d.Id.Equals(anchorDockId)) ??
                         throw new ArgumentException(
                             $"dock with id={anchorDockId} does not belong to me: {this}",
                             nameof(anchor));

        // Determine the required orientation based on the requested relative
        // positioning.
        var requiredOrientation = anchor.Position is AnchorPosition.Left or AnchorPosition.Right
            ? DockGroupOrientation.Horizontal
            : DockGroupOrientation.Vertical;

        // If the group is empty or has a single item, its orientation can
        // still be redefined.
        if (this.docks.Count <= 1)
        {
            this.Orientation = requiredOrientation;
        }

        // If the required orientation is different from our orientation, we
        // need to repartition by splitting it around the relativeTo dock.
        // Otherwise, we just add the new dock to the group's dock list.
        var hostGroup = this.Orientation != requiredOrientation
            ? this.Repartition(relativeTo, requiredOrientation)
            : this;

        dock.AsDock().Group = hostGroup;

        var insertPosition = 0;
        if (!hostGroup.IsEmpty)
        {
            var relativeToPosition = hostGroup.Docks.IndexOf(relativeTo);
            insertPosition = anchor.Position is AnchorPosition.Left or AnchorPosition.Top
                ? int.Max(0, relativeToPosition - 1)
                : relativeToPosition + 1;
        }

        if (hostGroup.IsVertical)
        {
            relativeTo.AsDock().Height = new Height(relativeTo.Height.Half());
            dock.AsDock().Height = new Height("1*");
        }
        else
        {
            relativeTo.AsDock().Width = new Width(relativeTo.Width.Half());
            dock.AsDock().Width = new Width("1*");
        }

        hostGroup.docks.Insert(insertPosition, dock);
    }

    internal virtual void RemoveDock(IDock dock)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        var found = this.docks.Remove(dock);
        if (!found)
        {
            throw new InvalidOperationException(
                $"attempt to a remove dock with Id={dock.Id} from group Id={this}, but the dock was not there.");
        }

        if (this.Docks.Count > 0)
        {
            if (this.IsVertical)
            {
                this.Docks.Last().AsDock().Height = new Height("1*");
            }
            else
            {
                this.Docks.Last().AsDock().Width = new Width("1*");
            }
        }

        if (this.docks.Count < 2)
        {
            this.Orientation = DockGroupOrientation.Undetermined;
        }

        this.ConsolidateUp();
    }

    private void ConsolidateUp()
    {
        if (this.Docker is IOptimizingDocker optimize)
        {
            optimize.ConsolidateUp(this);
        }
    }

    private DockGroup Repartition(IDock? relativeTo, DockGroupOrientation requiredOrientation)
    {
        var items = this.docks.ToList();
        var relativeToIndex = relativeTo is null ? 0 : this.docks.IndexOf(relativeTo);
        Debug.Assert(
            relativeToIndex != -1,
            $"Relative dock `{relativeTo}` does not belong to this dock group Id=`{this}`");

        var before = items[..relativeToIndex];
        var relativeItem = items[relativeToIndex];
        var after = items[(relativeToIndex + 1)..];

        var hostGroup = new DockGroup(this.Docker)
        {
            Orientation = requiredOrientation,
        };
        relativeItem.AsDock().Group = hostGroup;
        hostGroup.docks.Add(relativeItem);

        var beforeGroup = before.Count != 0
            ? new DockGroup(this.Docker)
            {
                Orientation = this.Orientation,
            }
            : null;
        if (beforeGroup != null)
        {
            foreach (var item in before)
            {
                item.AsDock().Group = beforeGroup;
                beforeGroup.docks.Add(item);
            }
        }

        var afterGroup = after.Count != 0
            ? new DockGroup(this.Docker)
            {
                Orientation = this.Orientation,
            }
            : null;
        if (afterGroup != null)
        {
            foreach (var item in after)
            {
                item.AsDock().Group = afterGroup;
                afterGroup.docks.Add(item);
            }
        }

        /*
         * Clear the current docks and set up the new parts.
         *
         * We need a new partition if there docks before the relative dock.The
         * new partition will contain the host group because it's orientation
         * is different,and a group for the docks after the relative dock.
         */

        // Do not dispose of the docks here as we are moving them around
        this.docks.Clear();

        this.First = beforeGroup ?? hostGroup;
        this.Second = beforeGroup is null
            ? afterGroup
            : afterGroup is null
                ? hostGroup
                : new DockGroup(this.Docker)
                {
                    First = hostGroup,
                    Second = afterGroup,
                };

        Debug.Assert(hostGroup.IsLeaf, "re-partitioning will always return a host group which can host docks");
        return hostGroup;
    }
}

internal abstract class DockGroupBase(IDocker docker) : IDockGroup
{
    private static int nextId = 1;

    private bool disposed;

    public abstract ReadOnlyObservableCollection<IDock> Docks { get; }

    public IDocker Docker { get; } = docker;

    public bool IsCenter { get; protected internal init; }

    public bool IsEdge { get; protected internal init; }

    public bool IsHorizontal => this.Orientation == DockGroupOrientation.Horizontal;

    public bool IsVertical => this.Orientation == DockGroupOrientation.Vertical;

    public virtual DockGroupOrientation Orientation { get; protected set; }

    public abstract bool IsEmpty { get; }

    public abstract IDockGroup? First { get; protected set; }

    public abstract IDockGroup? Second { get; protected set; }

    public int DebugId { get; } = Interlocked.Increment(ref nextId);

    internal DockGroupBase? Parent { get; set; }

    public virtual void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.First?.Dispose();
        this.Second?.Dispose();

        this.disposed = true;
        GC.SuppressFinalize(this);
    }
}
