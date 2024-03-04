// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking;

public enum AnchorPosition
{
    /// <summary>Dock on the left side of the anchor.</summary>
    Left,

    /// <summary>Dock at the top of the anchor.</summary>
    Top,

    /// <summary>Dock on the right side of the anchor.</summary>
    Right,

    /// <summary>Dock at the bottom of the anchor.</summary>
    Bottom,

    /// <summary>Dock together with the anchor.</summary>
    With,

    /// <summary>Add to the center dock.</summary>
    Center,
}

public enum DockGroupOrientation
{
    /// <summary>
    /// When a group is empty or only has one slot occupied, its orientation can
    /// still become `Horizontal` or `Vertical`. So, it is `Undetermined` for
    /// now.
    /// </summary>
    Undetermined,

    /// <summary>
    /// `Horizontal` orientation; can only add without expansion if the requested
    /// orientation is also `Horizontal`.
    /// </summary>
    Horizontal,

    /// <summary>
    /// `Vertical` orientation; can only add without expansion if the requested
    /// orientation is also `Vertical`.
    /// </summary>
    Vertical,
}

/// <summary>Defines constants that specify the flow direction of items in a
/// workspace layout.</summary>
/// <remarks>
/// The flow direction of items in a workspace layout typically follow how the
/// docking tree is constructed. Given that the docking tree is a binary tree
/// with each item having two children where the `First` child always comes
/// before the `Second` child in the layout order, the layout flow directions
/// can only be from left to right or top to bottom.
/// <para>
/// There is also a systematic mapping between the flow direction and a docking
/// group orientation. For a horizontal group, items should flow from left to
/// right, while for a vertical one, items should flow from top to bottom. If
/// the group's orientation is still undetermined, it is up to the layout engine
/// to decide which flow direction to use.
/// </para>
/// </remarks>
public enum FlowDirection
{
    /// <summary>Indicates that items should flow from left to right.</summary>
    LeftToRight,

    /// <summary>Indicates that items should flow from top to bottom.</summary>
    TopToBottom,
}

/// <summary>Describes the docking state of a dockable view.</summary>
public enum DockingState
{
    /// <summary>
    /// The dockable has just been created or closed, and is currently not
    /// docked.
    /// </summary>
    Undocked,

    /// <summary>
    /// The dockable is minimized and should not be presented. It may still be
    /// interacted with (not in its full form), to show it again or close it.
    /// </summary>
    Minimized,

    /// <summary>
    /// The dockable is presented and will always be until it is closed or
    /// minimized.
    /// </summary>
    Pinned,

    /// <summary>
    /// The dockable is presented inside a floating dock. In contrast with
    /// <see cref="Pinned" />, this state should not be interpreted as a
    /// permanent one. It is most useful when the dockable needs to be
    /// temporarily presented and then minimized/pinned/closed.
    /// </summary>
    Floating,
}

/// <summary>
/// Indicates the origin of a layout change when the
/// <see cref="IDocker.LayoutChanged">corresponding event</see> is triggered
/// by the docker.
/// </summary>
public enum LayoutChangeReason
{
    /// <summary>
    /// Indicates that the layout change is only because of docks being resized,
    /// which does not require the existing layout to be rebuilt.
    /// </summary>
    Resize,

    /// <summary>
    /// Indicates that the layout change is due to operations on docks that
    /// would require the layout to be rebuilt.
    /// </summary>
    Docking,

    /// <summary>
    /// Indicates that the layout change due to a dock being floated, which may
    /// not require the existing layout to be rebuilt.
    /// </summary>
    Floating,
}

/// <summary>
/// Indicates how new views, added to a <see cref="IDock" /> should be
/// positioned inside the dock.
/// </summary>
public enum DockablePlacement
{
    /// <summary>
    /// Insert the new dockable at the first position in the dock.
    /// </summary>
    First,

    /// <summary>
    /// Insert the new dockable at the last position in the dock.
    /// </summary>
    Last,

    /// <summary>
    /// Insert the new dockable after the currently active item in the dock. If
    /// no item is currently active, behave similarly to <see cref="First" />.
    /// </summary>
    AfterActiveItem,

    /// <summary>
    /// Insert the new dockable before the currently active item in the dock. If
    /// no item is currently active, behave similarly to <see cref="First" />.
    /// </summary>
    BeforeActiveItem,
}
