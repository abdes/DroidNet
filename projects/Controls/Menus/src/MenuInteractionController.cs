// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Manages user interactions and navigation flow for hierarchical menu systems.
/// </summary>
/// <remarks>
///     <para>This controller coordinates menu behavior across pointer and keyboard input modalities, automatically
///     switching navigation modes based on user interaction patterns. All interaction methods update the navigation
///     mode to match their input source, ensuring consistent behavior throughout the menu session.</para>
///
///     <para>Focus management is handled transparently - the controller captures the original focus owner when menu
///     interaction begins and restores it when the menu system is dismissed. This ensures users return to their
///     previous context without manual intervention.</para>
///
///     <para>Submenu expansion follows a single-branch policy per hierarchical level where only one submenu remains
///     open among siblings at each level. When focus or hover moves to a different menu item, any previously expanded
///     sibling is automatically collapsed to maintain visual clarity and consistent navigation patterns.</para>
/// </remarks>
/// <param name="services">The menu services instance for handling selection and group operations.</param>
public sealed partial class MenuInteractionController(MenuServices services)
{
    private WeakReference<UIElement>? focusReturnTarget;
    private bool hasMenuFocus;
    private PendingDismissal? pendingDismissal;

    /// <summary>
    ///     Gets the active navigation method being used for menu interactions.
    /// </summary>
    /// <value>
    ///     <see cref="MenuNavigationMode.PointerInput"/> for mouse/touch interactions, or
    ///     <see cref="MenuNavigationMode.KeyboardInput"/> for keyboard navigation.
    /// </value>
    public MenuNavigationMode NavigationMode { get; private set; } = MenuNavigationMode.PointerInput;

    /// <summary>
    ///     Initiates hover-based menu navigation when the pointer enters a menu item.
    /// </summary>
    /// <param name="context">The menu surface context where the hover event occurred.</param>
    /// <param name="menuItemData">The menu item being hovered.</param>
    /// <remarks>
    ///     Expansion behavior differs between root and submenu contexts. In root menus, items are only auto-expanded
    ///     when switching between already-expanded siblings. In submenus, items with children will auto-expand on hover
    ///     even when no siblings are currently expanded. When switching between expanded items, the previous item is
    ///     collapsed before expanding the hovered item. If focus is currently within the menu system, focus will
    ///     transfer to the hovered item.
    /// </remarks>
    public void OnItemHoverStarted(MenuInteractionContext context, MenuItemData menuItemData)
    {
        this.OnNavigationSourceChanged(MenuInteractionInputSource.PointerInput);
        this.CaptureFocusOwner(context.RootSurface);

        var expandedItem = GetExpandedItemForContext(context);
        if (expandedItem is { } && ReferenceEquals(expandedItem, menuItemData))
        {
            return;
        }

        this.LogHoverStarted(menuItemData, expandedItem is not null, context);

        // Handle item entry logic - shared between hover and focus events
        if (expandedItem is { } current)
        {
            if (!menuItemData.HasChildren)
            {
                this.TryCollapseItem(context, current);
            }
            else if (!this.TrySwitchRootExpansion(context, menuItemData))
            {
                this.TryCollapseItem(context, current);
                this.TryExpandItem(context, menuItemData, this.NavigationMode);
            }

            // If the focus was inside the menu system, give focus to the new item.
            if (this.hasMenuFocus)
            {
                switch (context)
                {
                    case { Kind: MenuInteractionContextKind.Root, RootSurface: { } rootSurface }:
                        _ = rootSurface.FocusItem(menuItemData, MenuNavigationMode.Programmatic);
                        break;
                    case { Kind: MenuInteractionContextKind.Column, ColumnSurface: { } columnSurface }:
                        _ = columnSurface.FocusItem(context.ColumnLevel, menuItemData, MenuNavigationMode.Programmatic);
                        break;
                }
            }
        }
        else
        {
            if (context.Kind != MenuInteractionContextKind.Root)
            {
                this.TryExpandItem(context, menuItemData, this.NavigationMode);
            }
        }
    }

    /// <summary>
    ///     Processes the end of a pointer hover interaction over a menu item.
    /// </summary>
    /// <param name="context">The context defining which menu surface lost the hover event.</param>
    /// <param name="menuItemData">The menu item no longer being hovered over.</param>
    public void OnItemHoverEnded(MenuInteractionContext context, MenuItemData menuItemData)
    {
        this.OnNavigationSourceChanged(MenuInteractionInputSource.PointerInput);

        this.LogHoverEnded(menuItemData, context);
    }

    /// <summary>
    ///     Processes the start of a focus interaction on a menu item.
    /// </summary>
    /// <param name="context">The context defining which menu surface is gaining focus.</param>
    /// <param name="oldFocus">The element that previously had focus.</param>
    public void OnGettingFocus(MenuInteractionContext context, DependencyObject oldFocus)
    {
        _ = context;

        if (this.focusReturnTarget is null && oldFocus is UIElement { } oldElement)
        {
            this.focusReturnTarget = new(oldElement);
            this.LogCapturedFocusOwner();
        }
    }

    /// <summary>
    ///     Handles focus arrival on a menu item during keyboard or programmatic navigation.
    /// </summary>
    /// <param name="context">The menu surface context containing the focused item.</param>
    /// <param name="menuItemData">The menu item that received focus.</param>
    /// <param name="source">The input method that triggered the focus change.</param>
    /// <remarks>
    ///     When focus moves to a different menu item and there is already an expanded item in the same context, the
    ///     expanded item will be collapsed and the focused item will be expanded if it has children. In column contexts
    ///     with no expanded item at the current level, items with children will be expanded only if there is an
    ///     expanded item at the current level. This maintains the single-branch policy per hierarchical level.
    /// </remarks>
    public void OnItemGotFocus(MenuInteractionContext context, MenuItemData menuItemData, MenuInteractionInputSource source)
    {
        this.LogItemGotFocus(menuItemData, source, context);

        this.OnNavigationSourceChanged(source);
        this.CaptureFocusOwner(context.RootSurface);
        this.hasMenuFocus = true;

        var expandedItem = GetExpandedItemForContext(context);
        if (expandedItem is { } && ReferenceEquals(expandedItem, menuItemData))
        {
            return;
        }

        // Handle item entry logic - same as OnItemHoverStarted but without focus transfer since focus is already here
        if (expandedItem is { } current)
        {
            if (!menuItemData.HasChildren)
            {
                this.TryCollapseItem(context, current);
            }
            else if (!this.TrySwitchRootExpansion(context, menuItemData))
            {
                this.TryCollapseItem(context, current);
                this.TryExpandItem(context, menuItemData, this.NavigationMode);
            }
        }
        else
        {
            if (context is { Kind: MenuInteractionContextKind.Column, ColumnSurface: { } columnSurface }
                && columnSurface.GetExpandedItem(context.ColumnLevel) is not null)
            {
                this.TryExpandItem(context, menuItemData, this.NavigationMode);
            }
        }
    }

    /// <summary>
    ///     Processes when a menu item loses keyboard focus.
    /// </summary>
    /// <param name="context">The context defining which menu surface lost focus.</param>
    /// <param name="menuItemData">The menu item that lost focus.</param>
    public void OnItemLostFocus(MenuInteractionContext context, MenuItemData menuItemData)
    {
        this.LogItemLostFocus(menuItemData, context);
        this.hasMenuFocus = false;
    }

    /// <summary>
    ///     Expands a menu item's submenu in response to an explicit user request.
    /// </summary>
    /// <param name="context">The menu surface context where the expansion was requested.</param>
    /// <param name="menuItemData">The menu item to expand.</param>
    /// <param name="source">The input method that triggered the expansion.</param>
    /// <remarks>
    ///     Expansion requests are typically triggered by pressing the right arrow key on a parent menu item or clicking
    ///     an expansion indicator. Upon successful expansion, focus will move to either the expanded item itself (in
    ///     root menus) or the first child (in submenus). Items without children or already expanded items are ignored.
    /// </remarks>
    public void OnExpandRequested(MenuInteractionContext context, MenuItemData menuItemData, MenuInteractionInputSource source)
    {
        this.OnNavigationSourceChanged(source);
        this.CaptureFocusOwner(context.RootSurface);

        this.LogItemExpandRequested(menuItemData, source, context);

        this.TryExpandItem(context, menuItemData, this.NavigationMode);
    }

    /// <summary>
    ///     Handles menu item selection and dismisses the menu system.
    /// </summary>
    /// <param name="context">The menu surface context containing the invoked item.</param>
    /// <param name="menuItem">The menu item that was selected.</param>
    /// <param name="source">The input method that triggered the selection.</param>
    /// <remarks>
    ///     Invocation represents the final step in menu interaction where the user has selected an action. This method
    ///     dismisses the entire menu hierarchy and restores focus to the original element. The actual command execution
    ///     is handled separately by the menu item.
    /// </remarks>
    public void OnItemInvoked(MenuInteractionContext context, MenuItemData menuItem, MenuInteractionInputSource source)
    {
        this.OnNavigationSourceChanged(source);
        this.CaptureFocusOwner(context.RootSurface);

        this.LogItemInvoked(menuItem, source, context);

        // Set up pending dismissal so focus will be restored when the menu closes
        this.pendingDismissal = new PendingDismissal(
            context.Kind == MenuInteractionContextKind.Root ? MenuInteractionContextKind.Root : MenuInteractionContextKind.Column,
            focusAnchor: null);

        // Prefer dismissing from root when available to close the entire hierarchy in one operation
        switch (context)
        {
            case { RootSurface: { } rootSurface }:
                rootSurface.Dismiss();
                break;
            case { ColumnSurface: { } columnSurface }:
                columnSurface.Dismiss();
                break;
        }
    }

    /// <summary>
    ///     Handles an initial menu request originating from a root surface (e.g., right-click context menu).
    /// </summary>
    /// <param name="context">The interaction context for the root surface.</param>
    /// <param name="source">The input source that triggered the request.</param>
    public void OnMenuRequested(MenuInteractionContext context, MenuInteractionInputSource source)
    {
        context.EnsureValid();
        this.OnNavigationSourceChanged(source);

        if (context.RootSurface is not { } root)
        {
            return;
        }

        this.CaptureFocusOwner(root);
        root.Show(this.NavigationMode);
    }

    /// <summary>
    ///     Coordinates mutual exclusion for radio button menu items.
    /// </summary>
    /// <param name="menuItem">The radio menu item being selected.</param>
    /// <remarks>
    ///     When a radio menu item is selected, all other items in the same radio group will be automatically deselected
    ///     to maintain mutual exclusivity within the group.
    /// </remarks>
    /// <exception cref="ArgumentNullException">Thrown when <paramref name="menuItem"/> is <see langword="null"/>.</exception>
    public void OnRadioGroupSelectionRequested(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);
        services.HandleGroupSelection(menuItem);
    }

    /// <summary>
    ///     Initiates dismissal of the menu system from any hierarchical level.
    /// </summary>
    /// <param name="context">The menu surface context requesting dismissal.</param>
    /// <param name="kind">The type of dismissal being requested.</param>
    /// <returns><see langword="true"/> if dismissal was initiated; <see langword="false"/> if the request was ignored.</returns>
    /// <remarks>
    ///     Dismissal requests can originate from root menus (closing the entire system) or submenu columns (closing
    ///     back to the parent level). The controller manages the dismissal sequence and coordinates focus restoration
    ///     to maintain proper navigation flow.
    /// </remarks>
    public bool OnDismissRequested(MenuInteractionContext context, MenuDismissKind kind)
    {
        this.LogDismissRequested(context, kind);

        context.EnsureValid();

        this.OnNavigationSourceChanged(kind.ToInputSource());

        if (context.Kind == MenuInteractionContextKind.Root && context.RootSurface is { } rootSurface)
        {
            Debug.Assert(context.ColumnSurface is null, "Dismissing with a root context should not have a column surface");

            this.pendingDismissal = new PendingDismissal(MenuInteractionContextKind.Root, focusAnchor: null);
            rootSurface.Dismiss();
            return true;
        }

        if (context.Kind == MenuInteractionContextKind.Column && context.ColumnSurface is { } columnSurface)
        {
            // When dismissing with keyboard (ESC), restore focus to the anchor menu item
            // When dismissing with pointer (click outside), restore to original focus owner
            var anchor = kind == MenuDismissKind.KeyboardInput ? ResolveColumnAnchor(context) : null;
            this.LogColumnDismissScheduling(anchor);
            this.pendingDismissal = new PendingDismissal(MenuInteractionContextKind.Column, anchor);
            columnSurface.Dismiss();

            if (context.RootSurface is not null)
            {
                var expandedItem = context.RootSurface.GetExpandedItem();
                if (expandedItem is not null)
                {
                    context.RootSurface.CollapseItem(expandedItem, this.NavigationMode);
                }
            }

            return true;
        }

        this.LogDismissRequestIgnored();
        return false;
    }

    /// <summary>
    ///     Collapses an expanded menu item's submenu if currently open.
    /// </summary>
    /// <param name="context">The menu surface context containing the item to collapse.</param>
    /// <param name="menuItemData">The menu item whose submenu should be collapsed.</param>
    /// <remarks>
    ///     This method only acts on items that are currently expanded. Collapsed items are ignored to prevent
    ///     unnecessary operations during navigation transitions.
    /// </remarks>
    public void TryCollapseItem(MenuInteractionContext context, MenuItemData menuItemData)
    {
        if (!menuItemData.IsExpanded)
        {
            // If the item is not expanded, there's nothing to collapse.
            return;
        }

        if (context.Kind == MenuInteractionContextKind.Root)
        {
            Debug.Assert(context.RootSurface is not null, "Root interaction must have a root surface");
            context.ColumnSurface?.Dismiss();
            context.RootSurface.CollapseItem(menuItemData, this.NavigationMode);
        }
        else
        {
            Debug.Assert(context.ColumnSurface is not null, "Column interaction must have a column surface");
            context.ColumnSurface.CollapseItem(context.ColumnLevel, menuItemData, this.NavigationMode);
        }
    }

    /// <summary>
    ///     Completes the dismissal process when a menu surface has been closed.
    /// </summary>
    /// <param name="context">The menu surface context that was dismissed.</param>
    /// <remarks>
    ///     This method is called after a menu surface has been dismissed to complete any pending focus restoration and
    ///     cleanup. It coordinates between different dismissal scenarios to ensure proper navigation state is
    ///     maintained.
    /// </remarks>
    public void OnDismissed(MenuInteractionContext context)
    {
        this.LogOnDismissed(context);

        if (this.pendingDismissal is not { } pending || pending.Kind != context.Kind)
        {
            // Edge case: dismissal without matching pending dismissal
            if (context.Kind == MenuInteractionContextKind.Root)
            {
                this.LogRootDismissalWithoutPending();
                this.RestoreFocusAfterDismissal();
                this.pendingDismissal = null;
            }

            return;
        }

        // Handle the dismissal based on context and anchor
        if (pending.Kind == MenuInteractionContextKind.Column && pending.FocusAnchor is { } anchor && context.RootSurface is { } rootSurface)
        {
            // ESC key - restore focus to anchor menu item
            _ = rootSurface.FocusItem(anchor, MenuNavigationMode.Programmatic);
            this.LogColumnDismissalFocusRestored(anchor);
        }
        else if (pending.Kind == MenuInteractionContextKind.Column && pending.FocusAnchor is null)
        {
            // Click outside or item invoked - restore to original owner
            this.LogColumnDismissalWithoutAnchor();
            this.RestoreFocusAfterDismissal();
        }
        else if (pending.Kind == MenuInteractionContextKind.Root)
        {
            // Root dismissal - restore to original owner
            this.RestoreFocusAfterDismissal();
        }

        this.pendingDismissal = null;
    }

    /// <summary>
    ///     Handles directional keyboard navigation within the menu hierarchy.
    /// </summary>
    /// <param name="context">The menu surface context where navigation is occurring.</param>
    /// <param name="fromItemData">The menu item currently having focus.</param>
    /// <param name="direction">The direction of navigation being requested.</param>
    /// <param name="inputSource">The input method triggering the navigation.</param>
    /// <returns>
    ///     <see langword="true"/> if navigation was handled; <see langword="false"/> if the direction is not supported
    ///     in the current context.
    /// </returns>
    /// <remarks>
    ///     Directional navigation supports standard menu keyboard patterns with context-sensitive behavior:
    ///     <para>
    ///     <strong>Root Context:</strong> Left/Right arrows cycle horizontally through root menu items with wrapping.
    ///     Up/Down arrows expand submenus for items with children or move focus into existing column surfaces.</para>
    ///     <para>
    ///     <strong>Column Context:</strong> Up/Down arrows cycle vertically through items at the current level with
    ///     wrapping. Right arrow expands submenus for items with children. Left arrow navigates back to the parent
    ///     level, collapsing the current submenu and restoring focus to the parent item.</para>
    ///     <para>
    ///     All navigation maintains the single-branch policy where only one submenu remains expanded per hierarchical
    ///     level.</para>
    /// </remarks>
    public bool OnDirectionalNavigation(MenuInteractionContext context, MenuItemData fromItemData, MenuNavigationDirection direction, MenuInteractionInputSource inputSource)
    {
        context.EnsureValid();

        this.OnNavigationSourceChanged(inputSource);
        var navMode = inputSource.ToNavigationMode();

        // Prefer column-specific handling when we have a column surface available.
        if (context is { IsColumn: true, ColumnSurface: { } columnSurface })
        {
            return this.HandleColumnDirectionalNavigation(context, columnSurface, fromItemData, direction, navMode);
        }

        // Fall back to root-specific handling when in a root context.
        return context is { IsRoot: true, RootSurface: { } rootSurface }
            && this.HandleRootDirectionalNavigation(context, rootSurface, fromItemData, direction, navMode);
    }

    private static MenuItemData? GetExpandedItemForContext(MenuInteractionContext context)
        => context.Kind switch
        {
            MenuInteractionContextKind.Root => context.RootSurface?.GetExpandedItem(),
            MenuInteractionContextKind.Column => context.ColumnSurface?.GetExpandedItem(context.ColumnLevel),
            _ => null,
        };

    private static MenuItemData? ResolveColumnAnchor(MenuInteractionContext context)
    {
        if (context.ColumnSurface is not { } columnSurface)
        {
            return context.RootSurface?.GetExpandedItem();
        }

        if (context.ColumnLevel > 0)
        {
            var parentLevel = new MenuLevel(context.ColumnLevel - 1);
            return columnSurface.GetExpandedItem(parentLevel);
        }

        return context.RootSurface?.GetExpandedItem();
    }

    private void TryExpandItem(MenuInteractionContext context, MenuItemData menuItem, MenuNavigationMode mode)
    {
        if (!menuItem.HasChildren || menuItem.IsExpanded)
        {
            return;
        }

        this.LogExecuteRequestSubmenuOpening(menuItem);

        switch (context)
        {
            case { Kind: MenuInteractionContextKind.Root, RootSurface: { } surface }:
                surface.ExpandItem(menuItem, mode);
                _ = surface.FocusItem(menuItem, mode);
                break;
            case { Kind: MenuInteractionContextKind.Column, ColumnSurface: { } columnSurface }:
                columnSurface.ExpandItem(context.ColumnLevel, menuItem, mode);
                _ = columnSurface.FocusFirstItem(new MenuLevel(context.ColumnLevel + 1), mode);
                break;
            case { Kind: MenuInteractionContextKind.Root }:
                throw new InvalidOperationException("Root surface is required for root submenu requests.");
            default:
                throw new InvalidOperationException("Column surface is required for column submenu requests.");
        }
    }

    /// <summary>
    ///     Signals that the most recent interaction originated from a specific input source.
    /// </summary>
    /// <param name="inputSource">The input source for the menu interaction.</param>
    private void OnNavigationSourceChanged(MenuInteractionInputSource inputSource)
    {
        var newMode = inputSource.ToNavigationMode();
        if (this.NavigationMode == newMode)
        {
            return;
        }

        this.NavigationMode = newMode;
        this.LogNavigationModeChanged();
    }

    private void CaptureFocusOwner(IRootMenuSurface? rootSurface)
    {
        if (this.focusReturnTarget is not null)
        {
            return;
        }

        if (rootSurface?.FocusElement is not UIElement { } focusElement)
        {
            this.LogUnableToCaptureNotFocusable();
            return;
        }

        this.focusReturnTarget = new WeakReference<UIElement>(focusElement);
        this.LogCapturedFocusOwner();
    }

    private void RestoreFocusAfterDismissal()
    {
        if (this.focusReturnTarget is null)
        {
            return;
        }

        if (!this.RestoreFocusOwner())
        {
            this.LogRestoreFocusOwnerFailed();
        }

        this.ClearFocusReturnElement();
    }

    private bool TrySwitchRootExpansion(MenuInteractionContext context, MenuItemData targetItem)
    {
        if (context.Kind != MenuInteractionContextKind.Root || context.RootSurface is not { } rootSurface)
        {
            return false;
        }

        if (!targetItem.HasChildren)
        {
            return false;
        }

        var currentExpanded = rootSurface.GetExpandedItem();
        if (currentExpanded is { } && ReferenceEquals(currentExpanded, targetItem))
        {
            return true;
        }

        rootSurface.ExpandItem(targetItem, this.NavigationMode);
        return true;
    }

    private bool HandleColumnDirectionalNavigation(MenuInteractionContext context, ICascadedMenuSurface columnSurface, MenuItemData fromItemData, MenuNavigationDirection direction, MenuNavigationMode navMode)
    {
        // Right: expand submenu if present.
        if (direction is MenuNavigationDirection.Right && fromItemData.HasChildren)
        {
            this.TryExpandItem(context, fromItemData, navMode);
            return true;
        }

        // Vertical movement within the column (wraps).
        if (direction is MenuNavigationDirection.Up or MenuNavigationDirection.Down)
        {
            var adjacent = columnSurface.GetAdjacentItem(context.ColumnLevel, fromItemData, direction, wrap: true);

            this.LogNavigateToAdjacentItem(direction, adjacent.Id, context);

            _ = columnSurface.FocusItem(context.ColumnLevel, adjacent, navMode);
            return true;
        }

        // Left from a sub-column navigates back to the parent level.
        if (context.ColumnLevel > 0 && direction is MenuNavigationDirection.Left)
        {
            var parentLevel = new MenuLevel(context.ColumnLevel - 1);
            var parentActiveItem = columnSurface.GetExpandedItem(parentLevel);
            Debug.Assert(parentActiveItem is not null, "Expecting a valid parent active item when navigating left from a sub-column");

            this.LogNavigateToParentColumn(parentLevel, parentActiveItem.Id, context);

            columnSurface.TrimTo(new MenuLevel(parentLevel));
            parentActiveItem.IsExpanded = false;
            _ = columnSurface.FocusItem(parentLevel, parentActiveItem, navMode);
            return true;
        }

        // When at the boundary of columns, delegate to root-level navigation (e.g. move focus across root items).
        if (context.RootSurface is not { } rootSurface)
        {
            // No root surface, cannot navigate at the root level.
            return false;
        }

        if ((context.ColumnLevel == 0 && direction is MenuNavigationDirection.Left) ||
            (direction is MenuNavigationDirection.Right && !fromItemData.HasChildren))
        {
            var rootExpandedItem = rootSurface.GetExpandedItem();
            Debug.Assert(rootExpandedItem is not null, "Expecting a valid root expanded item when navigating from columns to root");

            var adjacent = rootSurface.GetAdjacentItem(rootExpandedItem, direction, wrap: true);
            rootSurface.ExpandItem(adjacent, navMode);
            return true;
        }

        return false;
    }

    private bool HandleRootDirectionalNavigation(MenuInteractionContext context, IRootMenuSurface rootSurface, MenuItemData fromItemData, MenuNavigationDirection direction, MenuNavigationMode navMode)
    {
        if (direction is MenuNavigationDirection.Left or MenuNavigationDirection.Right)
        {
            var adjacent = rootSurface.GetAdjacentItem(fromItemData, direction, wrap: true);

            this.LogNavigateToAdjacentItem(direction, adjacent.Id, context);

            _ = rootSurface.FocusItem(adjacent, navMode);
            return true;
        }

        if (direction is MenuNavigationDirection.Up or MenuNavigationDirection.Down)
        {
            if (fromItemData.IsExpanded)
            {
                Debug.Assert(context.ColumnSurface is not null, "Expecting a valid column surface when root item is expanded");
                _ = context.ColumnSurface.FocusFirstItem(new MenuLevel(context.ColumnLevel), navMode);
                return true;
            }

            if (fromItemData.HasChildren)
            {
                rootSurface.ExpandItem(fromItemData, navMode);
                return true;
            }
        }

        return false;
    }

    private void ClearFocusReturnElement()
        => this.focusReturnTarget = null;

    private bool RestoreFocusOwner()
    {
        if (this.focusReturnTarget is not { } weakReference || !weakReference.TryGetTarget(out var element))
        {
            this.LogRestoreFocusNoElement();
            this.focusReturnTarget = null;
            return false;
        }

        if (element.Focus(FocusState.Programmatic))
        {
            this.LogFocusRestoreSucceeded();
            this.focusReturnTarget = null;
            return true;
        }

        this.LogFocusRestoreFailed();
        return false;
    }

    private readonly struct PendingDismissal(MenuInteractionContextKind kind, MenuItemData? focusAnchor)
    {
        public MenuInteractionContextKind Kind { get; } = kind;

        public MenuItemData? FocusAnchor { get; } = focusAnchor;
    }
}
