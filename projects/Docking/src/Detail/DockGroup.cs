// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Collections.ObjectModel;
using System.Diagnostics;

/// <summary>
/// Represents a grouping of other groups, or <see cref="Dockable" />s.
/// </summary>
/// <remarks>
/// <para>
/// If a group has dockables, then it is a leaf group with both slots (First and
/// Second) empty. Otherwise, group composition is used to construct a tree of
/// groups based on their <see cref="Orientation" />.
/// </para>
/// <para>
/// The tree od dock groups is a binary tree, where each node only has two
/// children, and where the order of the children matters. When a group needs to
/// expand, it is split in two parts, one of them will hold the old group, and
/// the other one will hold the new group. Such structure is easy to visualize
/// using simple nesting of two-colum (Horizontal) or two-row (Vertical) grids.
/// </para>
/// </remarks>
public partial class DockGroup : IDockGroup
{
    private static int nextId = 1;
    private readonly int debugId;

    private readonly ObservableCollection<IDock> docks = [];

    private DockGroup? first;
    private DockGroup? second;

    public DockGroup()
    {
        this.debugId = Interlocked.Increment(ref nextId);
        this.Docks = new ReadOnlyObservableCollection<IDock>(this.docks);
    }

    public bool IsEmpty => this.docks.Count == 0;

    public ReadOnlyObservableCollection<IDock> Docks { get; }

    public bool IsHorizontal => this.Orientation == Orientation.Horizontal;

    public bool IsVertical => this.Orientation == Orientation.Vertical;

    public virtual Orientation Orientation { get; protected set; }

    public virtual IDockGroup? First
    {
        get => this.first;
        protected set => this.SetPart((DockGroup?)value, out this.first);
    }

    public virtual IDockGroup? Second
    {
        get => this.second;
        protected set => this.SetPart((DockGroup?)value, out this.second);
    }

    internal bool IsLeaf => this is { First: null, Second: null };

    internal DockGroup? Parent { get; private set; }

    public override string ToString()
    {
        string[] children =
        [
            this.first is null ? string.Empty : $"{this.first.debugId}",
            this.second is null ? string.Empty : $"{this.second.debugId}",
        ];
        var childrenStr = children[0] != string.Empty || children[1] != string.Empty
            ? $" {{{string.Join(',', children)}}}"
            : string.Empty;

        return $"[{this.debugId}]({string.Join(',', this.Docks)}){childrenStr}";
    }

    private void SetPart(DockGroup? value, out DockGroup? part)
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
public partial class DockGroup
{
    internal void RemoveGroup(IDockGroup group)
    {
        if (this.IsLeaf)
        {
            throw new InvalidOperationException($"group with Id={group} is not one my(Id={this.debugId}) children");
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
            throw new InvalidOperationException($"group with Id={group} is not one my(Id={this.debugId}) children");
        }

        if (this.IsLeaf)
        {
            this.Parent?.RemoveGroup(this);
        }
    }

    internal void AddGroupLast(IDockGroup group, Orientation orientation) => this.AddGroup(group, false, orientation);

    internal void AddGroupFirst(IDockGroup group, Orientation orientation) => this.AddGroup(group, true, orientation);

    internal void AddGroupAfter(IDockGroup group, IDockGroup sibling, Orientation orientation)
        => this.AddGroupRelativeTo(group, sibling, true, orientation);

    internal void AddGroupBefore(IDockGroup group, IDockGroup sibling, Orientation orientation)
        => this.AddGroupRelativeTo(group, sibling, false, orientation);

    protected virtual void MigrateDocksToGroup(DockGroup group)
    {
        foreach (var item in this.docks)
        {
            group.docks.Add(item);
            item.AsDock().Group = group;
        }

        this.docks.Clear();
    }

    private void AddGroup(IDockGroup group, bool isFirst, Orientation orientation)
    {
        if (this.Orientation == Orientation.Undetermined)
        {
            this.Orientation = orientation;
        }

        if (!this.IsEmpty)
        {
            var migrated = this.MigrateDocksToNewGroup();
            this.First = isFirst ? group : migrated;
            this.Second = isFirst ? migrated : group;
            return;
        }

        // If the group is empty and both slots are free, just use the first
        // slot.
        if (this.first is null && this.second is null)
        {
            Debug.Assert(
                this.Orientation == orientation,
                "empty group with children, should not have an orientation on its own");
            this.First = group;
            return;
        }

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
            this.First = this.first?.ExpandToAdd(group, isFirst) ?? group;
        }
        else
        {
            this.Second = this.second?.ExpandToAdd(group, isFirst) ?? group;
        }
    }

    // Expand the group by making a new group, replacing our second
    // part with it, and move our second part and the group to add to
    // it.
    private DockGroup ExpandToAdd(IDockGroup newGroup, bool newGroupFirst)
    {
        var expanded = new DockGroup
        {
            First = newGroupFirst ? newGroup : this,
            Second = newGroupFirst ? this : newGroup,
        };
        return expanded;
    }

    private DockGroup MigrateDocksToNewGroup()
    {
        var migrate = new DockGroup();

        this.MigrateDocksToGroup(migrate);

        migrate.Orientation = this.Orientation;
        return migrate;
    }

    private void SwapFirstAndSecond() => (this.First, this.Second) = (this.Second, this.First);

    private void AddGroupRelativeTo(IDockGroup group, IDockGroup sibling, bool after, Orientation orientation)
    {
        if (this.First != sibling && this.Second != sibling)
        {
            throw new InvalidOperationException($"relative group ({sibling}) is not managed by me ({this})");
        }

        // If the group has a part already that matches the relative sibling,
        // then it must be empty.
        Debug.Assert(this.IsEmpty, "a group with parts can only be empty");

        if (this.Orientation == Orientation.Undetermined)
        {
            this.Orientation = orientation;
        }

        // If both slots are full or if the requested orientation is not the
        // current group orientation, then we must expand to add.
        if (this.Orientation != orientation || (this.first != null && this.second != null))
        {
            if (this.first == sibling)
            {
                this.First = this.first.ExpandToAdd(group, !after);
                this.first.Orientation = orientation;
            }
            else if (this.second == sibling)
            {
                this.Second = this.second.ExpandToAdd(group, !after);
                this.second.Orientation = orientation;
            }

            return;
        }

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
}

/// <summary>
/// Dock management mix-in for the <see cref="DockGroup" /> class.
/// </summary>
public partial class DockGroup
{
    public void AddDock(IDock dock)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        if (!this.IsEmpty)
        {
            throw new InvalidOperationException(
                $"method {nameof(this.AddDock)} should only be used when the group is empty");
        }

        dock.AsDock().Group = this;

        // TODO(abdes): make insert position for new docks configurable
        this.docks.Add(dock);
    }

    internal void AddDock(IDock dock, Anchor anchor)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        // Find the dock by its id or fail.
        var relativeTo = Dock.FromId(anchor.DockId) ??
                         throw new ArgumentException($"invalid dock id: {anchor.DockId}", nameof(anchor));

        // Determine the required orientation based on the requested relative
        // positioning.
        var requiredOrientation = anchor.Position is AnchorPosition.Left or AnchorPosition.Right
            ? Orientation.Horizontal
            : Orientation.Vertical;

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

        hostGroup.docks.Insert(insertPosition, dock);
    }

    internal void RemoveDock(IDock dock)
    {
        Debug.Assert(this.IsLeaf, "only leaf nodes in the docking tree can have docks");

        var found = this.docks.Remove(dock);
        if (!found)
        {
            throw new InvalidOperationException(
                $"attempt to a remove dock with Id={dock.Id} from group Id={this}, but the dock was not there.");
        }

        if (this.docks.Count == 0)
        {
            this.Parent?.RemoveGroup(this);
        }
    }

    private DockGroup Repartition(IDock? relativeTo, Orientation requiredOrientation)
    {
        var items = this.docks.ToList();
        var relativeToIndex = relativeTo is null ? 0 : this.docks.IndexOf(relativeTo);

        if (relativeToIndex == -1)
        {
            throw new InvalidOperationException(
                $"Relative dock `{relativeTo}` does not belong to this dock group Id=`{this}`");
        }

        var before = items[..relativeToIndex];
        var relativeItem = items[relativeToIndex];
        var after = items[(relativeToIndex + 1)..];

        var hostGroup = new DockGroup() { Orientation = requiredOrientation };
        relativeItem.AsDock().Group = hostGroup;
        hostGroup.docks.Add(relativeItem);

        var beforeGroup = before.Count != 0 ? new DockGroup() { Orientation = this.Orientation } : null;
        if (beforeGroup != null)
        {
            foreach (var item in before)
            {
                item.AsDock().Group = beforeGroup;
                beforeGroup.docks.Add(item);
            }
        }

        var afterGroup = after.Count != 0 ? new DockGroup() { Orientation = this.Orientation } : null;
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

        this.docks.Clear();
        this.First = beforeGroup ?? hostGroup;
        this.Second = beforeGroup is null
            ? afterGroup
            : new DockGroup()
            {
                First = hostGroup,
                Second = afterGroup,
            };

        Debug.Assert(hostGroup.IsLeaf, "re-partitioning will always return a host group which can host docks");
        return hostGroup;
    }
}