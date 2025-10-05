// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Represents an individual menu item control, used within a <see cref="MenuBar"/> or <see cref="MenuFlyout"/>.
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

    /// <summary>
    ///     Occurs when executing the bound command for this menu item throws an exception.
    /// </summary>
    /// <remarks>
    ///     This event is raised when <see cref="MenuItemData.Command"/> throws during execution.
    ///     Containers can listen to this event to show error UI, log telemetry, or recover.
    /// </remarks>
    public event EventHandler<MenuItemCommandFailedEventArgs>? CommandExecutionFailed;
}

#pragma warning disable SA1402 // File may only contain a single type

/// <summary>
///     Provides data for menu item invocation events.
/// </summary>
public class MenuItemInvokedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the menu item that was invoked.
    /// </summary>
    public required MenuItemData ItemData { get; init; }
}

/// <summary>
///     Provides data for menu item hover events.
/// </summary>
public class MenuItemHoverEventArgs : EventArgs
{
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

/// <summary>
///     Provides data when a menu item's command execution fails with an exception.
/// </summary>
public class MenuItemCommandFailedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the menu item whose command failed.
    /// </summary>
    public required MenuItemData ItemData { get; init; }

    /// <summary>
    ///     Gets the exception thrown by the command.
    /// </summary>
    public required Exception Exception { get; init; }
}

#pragma warning restore SA1402 // File may only contain a single type
