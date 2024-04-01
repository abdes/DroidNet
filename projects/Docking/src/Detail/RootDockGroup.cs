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
    private readonly DockGroup center;

    private DockGroup? left;
    private DockGroup? top;
    private DockGroup? right;
    private DockGroup? bottom;

    public RootDockGroup(IDocker docker)
        : base(docker)
    {
        this.center = new DockGroup(docker)
        {
            IsCenter = true,
        };
        this.AddGroupFirst(this.center, DockGroupOrientation.Undetermined);
    }

    /// <summary>Place a <see cref="IDock">dock</see> at the center of the workspace.</summary>
    /// <remarks>
    /// For simplification, only one dock can be placed at the center. Use relative docking to that dock to place
    /// additional docks around it if needed.
    /// </remarks>
    /// <param name="dock">The dock to be placed.</param>
    /// <exception cref="InvalidOperationException">If the center group already has a dock.</exception>
    public void DockCenter(IDock dock)
    {
        if (!this.center.HasNoDocks)
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
            this.left = this.NewEdgeGroup(dock, AnchorPosition.Left);
            this.AddBeforeCenter(this.left, DockGroupOrientation.Horizontal);
            return;
        }

        AppendToEdge(this.left, this.NewDockGroup(dock));
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
            this.top = this.NewEdgeGroup(dock, AnchorPosition.Top);
            this.AddBeforeCenter(this.top, DockGroupOrientation.Vertical);
            return;
        }

        AppendToEdge(this.top, this.NewDockGroup(dock));
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
            this.right = this.NewEdgeGroup(dock, AnchorPosition.Right);
            this.AddAfterCenter(this.right, DockGroupOrientation.Horizontal);
            return;
        }

        this.PrependToEdge(this.right, this.NewDockGroup(dock));
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
            this.bottom = this.NewEdgeGroup(dock, AnchorPosition.Bottom);
            this.AddAfterCenter(this.bottom, DockGroupOrientation.Vertical);
            return;
        }

        this.PrependToEdge(this.bottom, this.NewDockGroup(dock));
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
        var parent = edge.Second is null ? edge : edge.Second.AsDockGroup();
        parent.AddGroupLast(group, edge.Orientation);
    }

    /// <summary>Creates a new DockGroup with a Tray, and adds the specified dock to it.</summary>
    /// <param name="dock">The dock to be added to the new DockGroup.</param>
    /// <param name="position">The position at which the specific dock is going to be anchored. This will determine the
    /// orientation of the tray (vertical for left and right, horizontal otherwise), and where it will be placed relative to the
    /// dock (before for left and top, after otherwise). </param>
    /// <returns>A new DockGroup with a tray with the proper orientation and placement relative to the dock.</returns>
    private DockGroup NewEdgeGroup(IDock dock, AnchorPosition position)
    {
        var edgeGroup = new DockGroup(this.Docker) { IsEdge = true };
        var newGroup = new DockGroup(this.Docker);
        newGroup.AddDock(dock);

        var orientation
            = position is AnchorPosition.Left or AnchorPosition.Right
                ? DockGroupOrientation.Horizontal
                : DockGroupOrientation.Vertical;

        if (position is AnchorPosition.Left or AnchorPosition.Top)
        {
            edgeGroup.AddGroupFirst(new TrayGroup(position, this.Docker), orientation);
            edgeGroup.AddGroupLast(newGroup, orientation);
        }
        else
        {
            edgeGroup.AddGroupFirst(newGroup, orientation);
            edgeGroup.AddGroupLast(new TrayGroup(position, this.Docker), orientation);
        }

        return edgeGroup;
    }

    /// <summary>Creates a new DockGroup and adds the specified dock to it.</summary>
    /// <param name="dock">The dock to be added to the new DockGroup.</param>
    /// <returns>A new DockGroup containing the specified dock.</returns>
    private DockGroup NewDockGroup(IDock dock)
    {
        var newGroup = new DockGroup(this.Docker);
        newGroup.AddDock(dock);
        return newGroup;
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
