// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Detail;

using System.Diagnostics;

/// <summary>Represents the docking workspace root.</summary>
/// <remarks>
/// The workspace root is a special <see cref="DockGroup" />, which maintains 5 special anchor points: left, right, top, bottom
/// and center, that can be used to place dockables relative to the workspace. From a docking tree perspective, each one of these
/// anchor points corresponds to a <see cref="DockGroup" />. The position in the docking tree, of the <see cref="DockGroup" /> for
/// one of these anchor points, is decided by when was the first time a dock was anchored to the corresponding anchor point.
/// <para>
/// From a certain point of view, the center group represents the space in the workspace left without docks in it. That is why,
/// for simplification, the center group can only have one dock. Any further placement of docks inside the center group must be
/// done through relative anchoring to its single dock.
/// </para>
/// <para>
/// Each of the left, right, top and bottom groups contains a <see cref="TrayGroup" />, used to host minimized docks. The tray
/// group only tracks minimized docks and does not in any way participate in the docking tree hierarchy.
/// </para>
/// <example>
/// In the following example of a workspace tree, group `4` is the left group, with a vertical tray that has `0` elements. Group
/// `7` is the top group, with a horizontal tray that has `1` element. Group `2` is the center group.
/// <code>
/// ↔ 3 () {4,10}
///    ↔ 4 () {6,5}
///       ↕ 6 Left TrayGroup (0)
///       ↕ 5 (2,7)
///    ↕ 10 () {7,14}
///       ↕ 7 () {9,8}
///          ↔ 9 Top TrayGroup (1)
///          ↕ 8 () {20,19}
///             ↕ 20 (3)
///             ? 19 (6)
///       ↔ 14 () {18,11}
///          ↕ 18 () {2,15}
///             ○ 2 (1)
///             ↕ 15 () {16,17}
///                ? 16 (5)
///                ↔ 17 Bottom TrayGroup (1)
///          ↔ 11 () {12,13}
///             ? 12 (4)
///             ↕ 13 Right TrayGroup (1)
/// </code>
/// </example>
/// </remarks>
internal sealed class RootDockGroup : DockGroup
{
    private readonly DockGroup center = new() { IsCenter = true };
    private DockGroup? left;
    private DockGroup? top;
    private DockGroup? right;
    private DockGroup? bottom;

    /// <summary>Initializes a new instance of the <see cref="RootDockGroup" /> class.</summary>
    public RootDockGroup() => this.AddGroupFirst(this.center, DockGroupOrientation.Undetermined);

    /// <summary>Place a <see cref="IDock">dock</see> at the center of the workspace.</summary>
    /// <remarks>
    /// For simplification, only one dock can be placed at the center. Use relative docking to that dock to place
    /// additional docks around it if needed.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    public void DockCenter(IDock dock)
    {
        if (!this.center.IsEmpty)
        {
            throw new InvalidOperationException(
                "the root center group is already populated, dock relative to its content");
        }

        this.center.AddDock(dock);
    }

    /// <summary>Place a <see cref="IDock">dock</see> on the left side of the workspace.</summary>
    /// <remarks>
    /// The first time a dock is placed at one side of the workspace, the workspace will create a <see cref="TrayGroup" /> for
    /// that side to hold minimized docks. The left tray will always appear in the docking tree <b>before</b> any other group on
    /// that side.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    public void DockLeft(IDock dock)
    {
        if (this.left == null)
        {
            this.left = NewDockGroupWithTray(dock, AnchorPosition.Left);
            this.AddBeforeCenter(this.left, DockGroupOrientation.Horizontal);
            return;
        }

        AppendToEdge(this.left, NewDockGroup(dock));
    }

    /// <summary>Place a <see cref="IDock">dock</see> at the top side of the workspace.</summary>
    /// <remarks>
    /// The first time a dock is placed at one side of the workspace, the workspace will create a <see cref="TrayGroup" /> for
    /// that side to hold minimized docks. The top tray will always appear in the docking tree <b>before</b> any other group on
    /// that side.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    public void DockTop(IDock dock)
    {
        if (this.top == null)
        {
            this.top = NewDockGroupWithTray(dock, AnchorPosition.Top);
            this.AddBeforeCenter(this.top, DockGroupOrientation.Vertical);
            return;
        }

        AppendToEdge(this.top, NewDockGroup(dock));
    }

    /// <summary>Place a <see cref="IDock">dock</see> on the right side of the workspace.</summary>
    /// <remarks>
    /// The first time a dock is placed at one side of the workspace, the workspace will create a <see cref="TrayGroup" /> for
    /// that side to hold minimized docks. The right tray will always appear in the docking tree <b>after</b> any other group on
    /// that side.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    public void DockRight(IDock dock)
    {
        if (this.right == null)
        {
            this.right = NewDockGroupWithTray(dock, AnchorPosition.Right);
            this.AddAfterCenter(this.right, DockGroupOrientation.Horizontal);
            return;
        }

        this.PrependToEdge(this.right, NewDockGroup(dock));
    }

    /// <summary>Place a <see cref="IDock">dock</see> at the bottom side of the workspace.</summary>
    /// <remarks>
    /// The first time a dock is placed at one side of the workspace, the workspace will create a <see cref="TrayGroup" /> for
    /// that side to hold minimized docks. The bottom tray will always appear in the docking tree <b>after</b> any other group on
    /// that side.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    public void DockBottom(IDock dock)
    {
        if (this.bottom == null)
        {
            this.bottom = NewDockGroupWithTray(dock, AnchorPosition.Bottom);
            this.AddAfterCenter(this.bottom, DockGroupOrientation.Vertical);
            return;
        }

        this.PrependToEdge(this.bottom, NewDockGroup(dock));
    }

    /// <inheritdoc />
    /// <remarks>
    /// The left, right, top, bottom and center groups, along with their tray groups are protected, and cannot be removed.
    /// </remarks>
    internal override void RemoveGroup(IDockGroup group)
    {
        // The root groups are protected and cannot be removed.
        if (group == this.center || group == this.left || group == this.right || group == this.bottom ||
            group == this.top || group is IDockTray)
        {
            return;
        }

        base.RemoveGroup(group);
    }

    /// <summary>Creates a new DockGroup and adds the specified dock to it.</summary>
    /// <param name="dock">The dock to be added to the new DockGroup.</param>
    /// <returns>A new DockGroup containing the specified dock.</returns>
    private static DockGroup NewDockGroup(IDock dock)
    {
        var newGroup = new DockGroup();
        newGroup.AddDock(dock);
        return newGroup;
    }

    /// <summary>Creates a new DockGroup with a Tray, and adds the specified dock to it.</summary>
    /// <param name="dock">The dock to be added to the new DockGroup.</param>
    /// <param name="position">The position at which the specific dock is going to be anchored. This will determine the
    /// orientation of the tray (vertical for left and right, horizontal otherwise), and where it will be placed relative to the
    /// dock (before for left and top, after otherwise). </param>
    /// <returns>A new DockGroup with a tray with the proper orientation and placement relative to the dock.</returns>
    private static DockGroup NewDockGroupWithTray(IDock dock, AnchorPosition position)
    {
        var trayHolder = new DockGroup();
        var newGroup = new DockGroup();
        newGroup.AddDock(dock);

        var orientation
            = position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Horizontal
                : DockGroupOrientation.Vertical;

        if (position is AnchorPosition.Left or AnchorPosition.Top)
        {
            trayHolder.AddGroupFirst(new TrayGroup(position), orientation);
            trayHolder.AddGroupLast(newGroup, orientation);
        }
        else
        {
            trayHolder.AddGroupFirst(newGroup, orientation);
            trayHolder.AddGroupLast(new TrayGroup(position), orientation);
        }

        return trayHolder;
    }

    /// <summary>
    /// Adds the specified <paramref name="group" />, as the last group, to the docking tree branch starting at the specified
    /// <paramref name="edge" /> group.
    /// </summary>
    /// <param name="edge">
    /// The edge group (left, top), which determines the docking tree branch to which the <paramref name="group" /> will be
    /// appended.
    /// </param>
    /// <param name="group">The group to be appended at the specified <paramref name="edge" />.</param>
    private static void AppendToEdge(DockGroup edge, IDockGroup group)
    {
        var parent = LastAvailableSlot(edge).AsDockGroup();
        parent.AddGroupLast(group, edge.Orientation);
    }

    /// <summary>
    /// Finds the last available slot in the docking tree branch starting at the specified <paramref name="root" />.
    /// </summary>
    /// <param name="root">The root dock group to start the search from.</param>
    /// <returns>The last available slot in the branch starting at <paramref name="root" />.</returns>
    private static IDockGroup LastAvailableSlot(IDockGroup root)
    {
        IDockGroup? result = null;
        IDockGroup? furthestNode = null;
        var maxDepth = -1;
        var furthestDepth = -1;

        DepthFirstSearch(root, 0, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);

        result ??= furthestNode;
        Debug.Assert(result is not null, "we always have a node to add to");
        return result;
    }

    /// <summary>
    /// Performs a depth-first search on a docking tree branch, looking for the furthest node with a free slot (First or Second).
    /// </summary>
    /// <param name="node">The starting node in the tree structure.</param>
    /// <param name="depth">The current depth in the tree structure.</param>
    /// <param name="result">The deepest node with either a free slot (First or Second child is null).</param>
    /// <param name="maxDepth">The maximum depth reached in the tree structure with a node that has a free slot.</param>
    /// <param name="furthestNode">The deepest node reached in the tree structure.</param>
    /// <param name="furthestDepth">The maximum depth reached in the tree structure.</param>
    private static void DepthFirstSearch(
        IDockGroup? node,
        int depth,
        ref IDockGroup? result,
        ref int maxDepth,
        ref IDockGroup? furthestNode,
        ref int furthestDepth)
    {
        if (node == null)
        {
            return;
        }

        if ((node.First == null || node.Second == null) && depth > maxDepth)
        {
            result = node;
            maxDepth = depth;
        }

        if (depth > furthestDepth)
        {
            furthestNode = node;
            furthestDepth = depth;
        }

        DepthFirstSearch(node.Second, depth + 1, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);
        DepthFirstSearch(node.First, depth + 1, ref result, ref maxDepth, ref furthestNode, ref furthestDepth);
    }

    /// <summary>
    /// Adds the specified <paramref name="group" />, as the <b>first</b> group, to the docking tree branch starting at the
    /// specified <paramref name="edge" /> group.
    /// </summary>
    /// <param name="edge">
    /// The edge group (right, bottom), which determines the docking tree branch to which the <paramref name="group" /> will be
    /// prepended.
    /// </param>
    /// <param name="group">The group to be prepended at the specified <paramref name="edge" />.</param>
    private void PrependToEdge(DockGroup edge, IDockGroup group)
    {
        Debug.Assert(edge == this.right || edge == this.bottom, "only prepend to the right or bottom edge");

        // When we are prepending to right or bottom, we are sure that the
        // center is in a separate group before the edge group.
        Debug.Assert(edge.Parent!.First!.IsCenter, "the center group must be the first sibling of right or bottom");

        edge.AddGroupFirst(group, edge.Orientation);
    }

    /// <summary>
    /// Adds the specified <paramref name="group" />, ensuring it is placed <b>before</b> the center dock group, and in a group
    /// with the specified <paramref name="orientation" />.
    /// </summary>
    /// <remarks>
    /// This methods will expand the tree, if needed, to add the group and ensure its parent as well as itself are properly
    /// oriented. It also guarantees that the center group will always come after the newly added group in a left-to-right
    /// traversal of the docking tree.
    /// </remarks>
    /// <param name="group">The dock group to add.</param>
    /// <param name="orientation">The orientation of the group to which the new group will be added.</param>
    private void AddBeforeCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent as DockGroup;
        Debug.Assert(parent is not null, $"center always has a parent of type {nameof(DockGroup)}");
        parent.AddGroupBefore(group, this.center, orientation);
    }

    /// <summary>
    /// Adds the specified <paramref name="group" />, ensuring it is placed <b>after</b> the center dock group, and in a group
    /// with the specified <paramref name="orientation" />.
    /// </summary>
    /// <remarks>
    /// This methods will expand the tree, if needed, to add the group and ensure its parent as well as itself are properly
    /// oriented. It also guarantees that the center group will always come before the newly added group in a left-to-right
    /// traversal of the docking tree.
    /// </remarks>
    /// <param name="group">The dock group to add.</param>
    /// <param name="orientation">The orientation of the group to which the new group will be added.</param>
    private void AddAfterCenter(IDockGroup group, DockGroupOrientation orientation)
    {
        var parent = this.center.Parent as DockGroup;
        Debug.Assert(parent is not null, $"center always has a parent of type {nameof(DockGroup)}");
        parent.AddGroupAfter(group, this.center, orientation);
    }
}
