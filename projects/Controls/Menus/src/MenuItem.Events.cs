// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

#pragma warning disable SA1402 // File may only contain a single type

namespace DroidNet.Controls.Menus;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or cascaded menu flyouts.
/// </summary>
public partial class MenuItem
{
    /// <summary>
    ///     Occurs when the menu item is invoked (clicked or activated via keyboard).
    /// </summary>
    /// <remarks>
    ///     This event is raised after the menu item's command has been executed (if present).
    ///     It allows parent controls to handle the item selection and perform additional
    ///     actions such as closing menus or updating UI state.
    /// </remarks>
    public event EventHandler<MenuItemInvokedEventArgs>? Invoked;

#if false
    /// <summary>
    ///     Occurs when the pointer enters an active (not a separator, and not disabled) menu item area.
    /// </summary>
    /// <remarks>
    ///     This event is used by menu container controls to implement hover navigation, such as automatically expanding
    ///     submenus or highlighting menu paths. The event is only raised for enabled, non-separator items.
    /// </remarks>
    public event EventHandler<MenuItemHoverEventArgs>? HoverStarted;

    /// <summary>
    ///     Occurs when the pointer exits the area of a hovered menu item.
    /// </summary>
    /// <remarks>
    ///     This event allows menu container controls to handle hover navigation, such as clearing highlights or
    ///     starting collapse timers for submenus.
    /// </remarks>
    public event EventHandler<MenuItemHoverEventArgs>? HoverEnded;

    /// <summary>
    ///     Occurs when a submenu expansion is requested for this menu item.
    /// </summary>
    /// <remarks>
    ///     This event is raised when the user hovers over or activates a menu item that
    ///     has child items. Menu container controls handle this event to display the
    ///     appropriate submenu content.
    /// </remarks>
    public event EventHandler<MenuItemSubmenuEventArgs>? SubmenuRequested;

    /// <summary>
    ///     Occurs when selection is requested for a menu item that belongs to a radio group.
    /// </summary>
    /// <remarks>
    ///     This event is raised when a grouped menu item (with RadioGroupId) is selected.
    ///     The container control should handle this event to ensure only one item in the
    ///     group is selected at a time by clearing the IsSelected property of other items
    ///     in the same group.
    /// </remarks>
    public event EventHandler<MenuItemRadioGroupEventArgs>? RadioGroupSelectionRequested;
#endif
}

/// <summary>
///     Provides data for menu item invocation events.
/// </summary>
public class MenuItemInvokedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the navigation mode used to invoke the menu item.
    /// </summary>
    public required MenuInteractionInputSource InputSource { get; init; }

    /// <summary>
    ///     Gets the menu item that was invoked.
    /// </summary>
    public required MenuItemData ItemData { get; init; }

    /// <summary>
    ///     Gets the exception thrown by the command (<see langword="null"/> unless the command failed).
    /// </summary>
    public CommandFailedException? Exception { get; init; }

    /// <summary>
    ///     Gets a value indicating whether the command execution failed. When <see langword="true"/>,
    ///     the <see cref="Exception"/> property contains the details of the failure.
    /// </summary>
    public bool IsFailed => this.Exception is not null;
}

#if false
/// <summary>
///     Provides data for menu item hover events.
/// </summary>
public class MenuItemHoverEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the navigation mode used to invoke the menu item.
    /// </summary>
    public required MenuInteractionInputSource InputSource { get; init; }

    /// <summary>
    ///     Gets the menu item that was hovered.
    /// </summary>
    public required MenuItemData ItemData { get; init; }
}

/// <summary>
///     Provides data for submenu request events.
/// </summary>
public class MenuItemSubmenuEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the navigation mode used to invoke the menu item.
    /// </summary>
    public required MenuInteractionInputSource InputSource { get; init; }

    /// <summary>
    ///     Gets the menu item requesting submenu expansion.
    /// </summary>
    public required MenuItemData ItemData { get; init; }
}

/// <summary>
///     Provides data for radio group selection events.
/// </summary>
public class MenuItemRadioGroupEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the menu item requesting selection in a radio group.
    /// </summary>
    public required MenuItemData ItemData { get; init; }

    /// <summary>
    ///     Gets the radio group identifier for the selection.
    /// </summary>
    public required string GroupId { get; init; }
}
#endif
