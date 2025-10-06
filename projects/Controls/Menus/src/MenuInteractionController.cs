// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;

namespace DroidNet.Controls;

/// <summary>
///     Coordinates interaction flows shared between menu containers. The controller keeps
///     track of the current navigation modality (pointer vs. keyboard), applies selection
///     logic through <see cref="MenuServices"/>, and raises higher-level events so menu
///     surfaces can materialize columns or dismiss open presenters consistently.
/// </summary>
/// <remarks>
///     Initializes a new instance of the <see cref="MenuInteractionController"/> class.
/// </remarks>
/// <param name="services">The shared services instance produced by <see cref="MenuBuilder"/>.</param>
public sealed class MenuInteractionController(MenuServices services)
{
    private readonly MenuServices services = services ?? throw new ArgumentNullException(nameof(services));
    private readonly Dictionary<int, MenuItemData> activeByColumn = [];
    private MenuItemData? activeRoot;

    /// <summary>
    ///     Gets the current navigation mode used to open the active menu path.
    /// </summary>
    public MenuNavigationMode NavigationMode { get; private set; } = MenuNavigationMode.PointerInput;

    /// <summary>
    ///     Attempts to retrieve the currently active item for the specified column level.
    /// </summary>
    /// <param name="columnLevel">The zero-based column level.</param>
    /// <returns>The active <see cref="MenuItemData"/> when available; otherwise <see langword="null"/>.</returns>
    public MenuItemData? GetActiveItem(int columnLevel)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(columnLevel);
        return columnLevel == 0 && this.activeRoot is not null
            ? this.activeRoot
            : this.activeByColumn.TryGetValue(columnLevel, out var item) ? item : null;
    }

    /// <summary>
    ///     Signals that the most recent interaction originated from a specific input source.
    /// </summary>
    /// <param name="source">The activation source for the interaction.</param>
    public void OnNavigationSourceChanged(MenuInteractionInputSource source)
    {
        Debug.WriteLine($"[MenuInteractionController] Navigation source changed to {source}");
        this.NavigationMode = source switch
        {
            MenuInteractionInputSource.PointerInput => MenuNavigationMode.PointerInput,
            MenuInteractionInputSource.KeyboardInput => MenuNavigationMode.KeyboardInput,
            _ => this.NavigationMode,
        };
    }

    /// <summary>
    ///     Handles pointer hover across menu items within the supplied context.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="origin">The visual element representing the menu item.</param>
    /// <param name="menuItem">The menu item under the pointer.</param>
    /// <param name="menuOpen">Indicates whether a root-level submenu is already open.</param>
    public void OnPointerEntered(MenuInteractionContext context, FrameworkElement origin, MenuItemData menuItem, bool menuOpen = false)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        Debug.WriteLine($"[MenuInteractionController] PointerEntered context={context.Kind} column={context.EffectiveColumnLevel} item={menuItem.Id} menuOpen={menuOpen}");

        if (context.Kind == MenuInteractionContextKind.Root)
        {
            if (!menuOpen)
            {
                this.OnNavigationSourceChanged(MenuInteractionInputSource.PointerInput);
                return;
            }

            ArgumentNullException.ThrowIfNull(origin);

            var currentRoot = this.activeByColumn.TryGetValue(0, out var activeRoot) ? activeRoot : null;
            if (ReferenceEquals(currentRoot, menuItem))
            {
                return;
            }
        }
        else
        {
            ArgumentNullException.ThrowIfNull(origin);
        }

        this.OnNavigationSourceChanged(MenuInteractionInputSource.PointerInput);
        this.ExecuteActivateItem(context, menuItem, this.NavigationMode);

        if (menuItem.HasChildren)
        {
            Debug.WriteLine($"[MenuInteractionController] PointerEntered requesting submenu for {menuItem.Id}");
            this.ExecuteRequestSubmenu(context, origin, menuItem, this.NavigationMode);
        }
    }

    /// <summary>
    ///     Focuses a menu item from keyboard navigation and optionally opens its submenu.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="origin">The visual element hosting the item, required to open a submenu.</param>
    /// <param name="menuItem">The menu item to focus.</param>
    /// <param name="source">The activation source that triggered the focus.</param>
    /// <param name="openSubmenu">Whether the submenu should be opened as part of the focus request.</param>
    public void OnFocusRequested(MenuInteractionContext context, FrameworkElement? origin, MenuItemData menuItem, MenuInteractionInputSource source, bool openSubmenu)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        Debug.WriteLine($"[MenuInteractionController] FocusRequested context={context.Kind} column={context.EffectiveColumnLevel} item={menuItem.Id} source={source} openSubmenu={openSubmenu}");
        this.OnNavigationSourceChanged(source);
        this.ExecuteActivateItem(context, menuItem, this.NavigationMode);

        if (openSubmenu && menuItem.HasChildren && origin is not null)
        {
            Debug.WriteLine($"[MenuInteractionController] FocusRequested opening submenu for {menuItem.Id}");
            this.ExecuteRequestSubmenu(context, origin, menuItem, this.NavigationMode);
        }
    }

    /// <summary>
    ///     Handles pointer or keyboard requests to open a submenu at the supplied context.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="origin">The visual element hosting the item.</param>
    /// <param name="menuItem">The menu item owning the requested submenu.</param>
    /// <param name="source">The activation source for the request.</param>
    public void OnSubmenuRequested(MenuInteractionContext context, FrameworkElement origin, MenuItemData menuItem, MenuInteractionInputSource source)
    {
        ArgumentNullException.ThrowIfNull(origin);
        ArgumentNullException.ThrowIfNull(menuItem);

        Debug.WriteLine($"[MenuInteractionController] SubmenuRequested context={context.Kind} column={context.EffectiveColumnLevel} item={menuItem.Id} source={source}");
        this.OnNavigationSourceChanged(source);
        this.ExecuteRequestSubmenu(context, origin, menuItem, this.NavigationMode);
    }

    /// <summary>
    ///     Handles horizontal navigation requests originating from a column back to the root surface.
    /// </summary>
    /// <param name="context">The interaction context supplying the surfaces.</param>
    /// <param name="direction">The horizontal direction to traverse.</param>
    public void OnRootNavigationRequested(MenuInteractionContext context, MenuInteractionHorizontalDirection direction)
    {
        var rootSurface = context.RootSurface ?? throw new InvalidOperationException("Root surface is required for horizontal navigation requests.");

        this.OnNavigationSourceChanged(MenuInteractionInputSource.KeyboardInput);
        rootSurface.NavigateRoot(direction, this.NavigationMode);
    }

    /// <summary>
    ///     Handles an invoked menu item by coordinating selection updates and notifying registered surfaces.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="menuItem">The menu item that was invoked.</param>
    /// <param name="source">The activation source for the invocation.</param>
    public void OnInvokeRequested(MenuInteractionContext context, MenuItemData menuItem, MenuInteractionInputSource source)
    {
        ArgumentNullException.ThrowIfNull(menuItem);
        this.ExecuteInvoke(context, menuItem, source);
    }

    /// <summary>
    ///     Handles a radio-group selection request by delegating to <see cref="MenuServices"/>.
    /// </summary>
    /// <param name="menuItem">The menu item requesting radio-group coordination.</param>
    public void OnRadioGroupSelectionRequested(MenuItemData menuItem)
    {
        ArgumentNullException.ThrowIfNull(menuItem);
        this.services.HandleGroupSelection(menuItem);
    }

    /// <summary>
    ///     Handles a dismiss request from any surface.
    /// </summary>
    /// <param name="context">The interaction context initiating the dismissal.</param>
    /// <param name="kind">The dismissal kind.</param>
    public void OnDismissRequested(MenuInteractionContext context, MenuDismissKind kind) => this.ExecuteDismiss(context, kind);

#if false // ALL THESE METHODS ARE FOR FUTURE USE IN PROGRAMMATIC MENU SYSTEM CONTROL ONLY
    /// <summary>
    ///     Requests a submenu for a specific menu item programmatically.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="origin">The menu item control that originated the request.</param>
    /// <param name="menuItem">The data for which a submenu should be shown.</param>
    /// <param name="navigationMode">Optional navigation mode to associate with the request.</param>
    public void RequestSubmenu(MenuInteractionContext context, FrameworkElement origin, MenuItemData menuItem, MenuNavigationMode? navigationMode = null)
    {
        ArgumentNullException.ThrowIfNull(origin);
        ArgumentNullException.ThrowIfNull(menuItem);

        var mode = navigationMode ?? this.NavigationMode;
        this.ExecuteRequestSubmenu(context, origin, menuItem, mode);
    }

    /// <summary>
    ///     Activates a menu item and deactivates any descendants beyond the specified context.
    /// </summary>
    /// <param name="context">The interaction context (root or a specific column).</param>
    /// <param name="menuItem">The menu item to activate.</param>
    /// <param name="navigationMode">Optional navigation mode to associate with the activation.</param>
    public void ActivateItem(MenuInteractionContext context, MenuItemData menuItem, MenuNavigationMode? navigationMode = null)
    {
        ArgumentNullException.ThrowIfNull(menuItem);

        var mode = navigationMode ?? this.NavigationMode;
        this.ExecuteActivateItem(context, menuItem, mode);
    }

    /// <summary>
    ///     Clears any active menu path and signals dismissal to host surfaces.
    /// </summary>
    public void Dismiss() => this.ExecuteDismiss(MenuDismissKind.Programmatic);
#endif

    private void ExecuteActivateItem(MenuInteractionContext context, MenuItemData menuItem, MenuNavigationMode mode)
    {
        var effectiveColumnLevel = context.EffectiveColumnLevel;
        this.NavigationMode = mode;

        foreach (var key in this.activeByColumn.Keys.Where(k => k >= effectiveColumnLevel).ToList())
        {
            if (!ReferenceEquals(this.activeByColumn[key], menuItem))
            {
                Debug.WriteLine($"[MenuInteractionController] Deactivating item {this.activeByColumn[key].Id} at column {key}");
                this.activeByColumn[key].IsExpanded = false;
            }

            _ = this.activeByColumn.Remove(key);
        }

        if (context.Kind == MenuInteractionContextKind.Root)
        {
            this.activeRoot = menuItem;
            this.activeByColumn[effectiveColumnLevel] = menuItem;
            (context.RootSurface ?? throw new InvalidOperationException("Root surface is required for root context activation.")).FocusRoot(menuItem, mode);
        }
        else
        {
            this.activeByColumn[effectiveColumnLevel] = menuItem;
            var columnSurface = context.ColumnSurface ?? throw new InvalidOperationException("Column surface is required for column context activation.");
            columnSurface.CloseFromColumn(context.ColumnLevel + 1);
            columnSurface.FocusColumnItem(menuItem, context.ColumnLevel, mode);
        }
    }

    private void ExecuteRequestSubmenu(MenuInteractionContext context, FrameworkElement origin, MenuItemData menuItem, MenuNavigationMode mode)
    {
        this.ExecuteActivateItem(context, menuItem, mode);

        if (!menuItem.HasChildren)
        {
            return;
        }

        Debug.WriteLine($"[MenuInteractionController] RequestSubmenu opening submenu for {menuItem.Id}");

        if (context.Kind == MenuInteractionContextKind.Root)
        {
            (context.RootSurface ?? throw new InvalidOperationException("Root surface is required for root submenu requests.")).OpenRootSubmenu(menuItem, origin, mode);
        }
        else
        {
            var columnSurface = context.ColumnSurface ?? throw new InvalidOperationException("Column surface is required for column submenu requests.");
            columnSurface.OpenChildColumn(menuItem, origin, context.EffectiveColumnLevel, mode);
        }

        menuItem.IsExpanded = true;
    }

    private void ExecuteInvoke(MenuInteractionContext context, MenuItemData menuItem, MenuInteractionInputSource source)
    {
        var mode = source switch
        {
            MenuInteractionInputSource.PointerInput => MenuNavigationMode.PointerInput,
            MenuInteractionInputSource.KeyboardInput => MenuNavigationMode.KeyboardInput,
            _ => this.NavigationMode,
        };

        this.ExecuteActivateItem(context, menuItem, mode);
        this.services.HandleGroupSelection(menuItem);

        var surface = context.Kind == MenuInteractionContextKind.Root
            ? context.RootSurface
            : context.ColumnSurface;
        surface?.Invoke(menuItem, source);

        this.ExecuteDismiss(context, MenuDismissKind.Programmatic);
    }

    private void ExecuteDismiss(MenuInteractionContext context, MenuDismissKind kind)
    {
        foreach (var item in this.activeByColumn.Values.Distinct())
        {
            Debug.WriteLine($"[MenuInteractionController] Deactivating item {item.Id} at active column");
            item.IsExpanded = false;
        }

        this.activeByColumn.Clear();

        if (this.activeRoot is { } root)
        {
            Debug.WriteLine($"[MenuInteractionController] Deactivating active root {root.Id}");
            root.IsExpanded = false;
            this.activeRoot = null;
        }

        // Update navigation mode based on dismissal kind.
        this.NavigationMode = kind switch
        {
            MenuDismissKind.PointerInput => MenuNavigationMode.PointerInput,
            MenuDismissKind.KeyboardInput => MenuNavigationMode.KeyboardInput,
            _ => this.NavigationMode,
        };

        // Notify surfaces to close and return focus to app if appropriate.
        context.ColumnSurface?.CloseFromColumn(0);
        context.RootSurface?.CloseFromColumn(0);
        context.RootSurface?.ReturnFocusToApp();
    }
}
