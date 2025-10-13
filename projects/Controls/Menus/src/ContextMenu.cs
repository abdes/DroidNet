// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

namespace DroidNet.Controls.Menus;

/// <summary>
///     Provides attached property support for showing context menus on UIElements.
/// </summary>
/// <remarks>
///     The FlyoutMenuHost instances are stored as attached properties on individual elements
///     and are automatically cleaned up when elements are unloaded via the Unloaded event handler.
/// </remarks>
[SuppressMessage(
    "Microsoft.Design",
    "CA1001:TypesThatOwnDisposableFieldsShouldBeDisposable",
    Justification = "Static class manages disposable hosts via attached properties; each host is disposed in element's Unloaded event")]
public static class ContextMenu
{
    /// <summary>
    ///     Identifies the MenuSource attached property.
    /// </summary>
    public static readonly DependencyProperty MenuSourceProperty = DependencyProperty.RegisterAttached(
        "MenuSource",
        typeof(IMenuSource),
        typeof(ContextMenu),
        new PropertyMetadata(defaultValue: null, OnMenuSourceChanged));

    private static readonly DependencyProperty MenuHostProperty = DependencyProperty.RegisterAttached(
        "Host",
        typeof(ICascadedMenuHost),
        typeof(ContextMenu),
        new PropertyMetadata(defaultValue: null));

    /// <summary>
    ///     Gets or sets the factory delegate for creating cascaded menu hosts. Exposed for testing purposes.
    /// </summary>
    internal static Func<IRootMenuSurface?, ICascadedMenuHost> CreateMenuHost { get; set; } = CreateDefaultMenuHost;

    /// <summary>
    ///     Gets the menu source for the specified element.
    /// </summary>
    /// <param name="element">The element to get the menu source from.</param>
    /// <returns>The menu source, or null if not set.</returns>
    public static IMenuSource? GetMenuSource(UIElement element)
        => (IMenuSource?)element.GetValue(MenuSourceProperty);

    /// <summary>
    ///     Sets the menu source for the specified element.
    /// </summary>
    /// <param name="element">The element to set the menu source on.</param>
    /// <param name="value">The menu source to set.</param>
    public static void SetMenuSource(UIElement element, IMenuSource? value)
        => element.SetValue(MenuSourceProperty, value);

    /// <summary>
    ///     Creates the default menu host implementation.
    /// </summary>
    /// <param name="rootSurface">The optional root surface for focus management.</param>
    /// <returns>A new <see cref="ICascadedMenuHost"/> instance.</returns>
    internal static ICascadedMenuHost CreateDefaultMenuHost(IRootMenuSurface? rootSurface)
        => new FlyoutMenuHost { RootSurface = rootSurface };

    private static ICascadedMenuHost? GetMenuHost(UIElement element)
        => (ICascadedMenuHost?)element.GetValue(MenuHostProperty);

    private static void SetMenuHost(UIElement element, ICascadedMenuHost? value)
        => element.SetValue(MenuHostProperty, value);

    private static bool IsFocusableElement(UIElement element)
        => element is Control { IsTabStop: true } or Control { AllowFocusOnInteraction: true };

    private static void OnMenuSourceChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
    {
        if (d is not UIElement element)
        {
            return;
        }

        // Clean up old host if menu source is being changed or cleared
        var oldHost = GetMenuHost(element);
        if (oldHost is not null)
        {
            element.ContextRequested -= OnElementContextRequested;
            if (element is FrameworkElement fe)
            {
                fe.Unloaded -= OnElementUnloaded;
            }

            oldHost.Opened -= OnHostOpened;
            oldHost.Closed -= OnHostClosed;
            oldHost.Dispose();
            SetMenuHost(element, value: null);
        }

        if (e.NewValue is not IMenuSource newMenuSource)
        {
            return;
        }

        // Create new host with optional root surface for focus management
        IRootMenuSurface? rootSurface = IsFocusableElement(element)
            ? new ContextMenuRootSurfaceAdapter(element)
            : null;

#pragma warning disable CA2000 // Dispose objects before losing scope
        var host = CreateMenuHost(rootSurface);
#pragma warning restore CA2000 // Dispose objects before losing scope

        host.Opened += OnHostOpened;
        host.Closed += OnHostClosed;

        SetMenuHost(element, host);
        element.ContextRequested += OnElementContextRequested;

        if (element is FrameworkElement frameworkElement)
        {
            frameworkElement.Unloaded += OnElementUnloaded;
        }
    }

    private static void OnElementUnloaded(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement element)
        {
            return;
        }

        // Clean up the host when the element is removed from the visual tree
        var host = GetMenuHost(element);
        if (host is not null)
        {
            element.ContextRequested -= OnElementContextRequested;
            element.Unloaded -= OnElementUnloaded;
            host.Opened -= OnHostOpened;
            host.Closed -= OnHostClosed;
            host.Dispose();
            SetMenuHost(element, value: null);
        }
    }

    private static void OnElementContextRequested(object sender, ContextRequestedEventArgs e)
    {
        if (sender is not UIElement element)
        {
            return;
        }

        // Mark handled early to suppress system default context menu feedback/spinner
        e.Handled = true;

        var host = GetMenuHost(element);
        if (host is null)
        {
            return;
        }

        // Get the current menu source (it may have changed dynamically)
        var menuSource = GetMenuSource(element);
        if (menuSource is null)
        {
            return;
        }

        // Ensure we have a root surface adapter and capture trigger info
        var adapterRef = EnsureAdapterWithTrigger(element, host, e);

        // Stash the latest menu source so Show can apply it just-in-time,
        // avoiding races with host clearing MenuSource during close.
        adapterRef.SetPendingSource(new MenuSourceView(menuSource.Items, menuSource.Services));

        // Ask the controller to show the menu, so it can capture and later restore focus
        if (menuSource.Services.InteractionController is { } controller)
        {
            var context = MenuInteractionContext.ForRoot(adapterRef);
            controller.OnMenuRequested(context, MenuInteractionInputSource.PointerInput);
            return;
        }

        // Fallback to direct show if no controller is available
        if (element is FrameworkElement frameworkElement)
        {
            _ = e.TryGetPosition(frameworkElement, out var pfallback);
            ApplyAndShowFallback(host, adapterRef, frameworkElement, pfallback, menuSource);
        }
    }

    private static ContextMenuRootSurfaceAdapter EnsureAdapterWithTrigger(UIElement element, ICascadedMenuHost host, ContextRequestedEventArgs e)
    {
        if (host.RootSurface is ContextMenuRootSurfaceAdapter existing && element is FrameworkElement fe1)
        {
            _ = e.TryGetPosition(fe1, out var p1);
            existing.SetTrigger(fe1, p1);
            return existing;
        }

        if (element is FrameworkElement fe2)
        {
            var adapter = new ContextMenuRootSurfaceAdapter(element);
            _ = e.TryGetPosition(fe2, out var p2);
            adapter.SetTrigger(fe2, p2);
            host.RootSurface = adapter;
            return adapter;
        }

        var fallback = new ContextMenuRootSurfaceAdapter(element);
        host.RootSurface = fallback;
        return fallback;
    }

    private static void ApplyAndShowFallback(ICascadedMenuHost host, ContextMenuRootSurfaceAdapter adapter, FrameworkElement anchor, Windows.Foundation.Point position, IMenuSource menuSource)
    {
        // Apply pending source then show directly. Keep logic centralized to remain under method length limits.
        adapter.SetTrigger(anchor, position);
        adapter.SetPendingSource(new MenuSourceView(menuSource.Items, menuSource.Services));
        host.RootSurface = adapter;
        adapter.Show(MenuNavigationMode.PointerInput);
    }

    private static void OnHostOpened(object? sender, EventArgs e)
    {
        if (sender is not ICascadedMenuHost host)
        {
            return;
        }

        host.SetupInitialKeyboardNavigation();
    }

    private static void OnHostClosed(object? sender, EventArgs e)
    {
        if (sender is not ICascadedMenuHost host)
        {
            return;
        }

        // Do not clear MenuSource here. The host manages its own MenuSource lifecycle during
        // close/reopen sequences (see PopupMenuHost.CompleteClose). Clearing it here can race
        // with a pending reopen and cause an empty menu to materialize.
    }

    /// <summary>
    ///     A minimal <see cref="IRootMenuSurface"/> adapter for context menu trigger elements.
    /// </summary>
    /// <remarks>
    ///     This adapter wraps a <see cref="UIElement"/> (the context menu trigger) to provide a
    ///     root surface for focus management purposes. It only implements the minimal functionality
    ///     needed for focus capture/restoration and dismissal - other methods throw
    ///     <see cref="NotSupportedException"/> as they're not applicable to context menus.
    /// </remarks>
    /// <remarks>
    ///     Initializes a new instance of the <see cref="ContextMenuRootSurfaceAdapter"/> class.
    /// </remarks>
    /// <param name="triggerElement">
    ///     The UI element that triggered the context menu. This element serves as the focus anchor.
    /// </param>
    private sealed class ContextMenuRootSurfaceAdapter(UIElement triggerElement) : IRootMenuSurface
    {
        private FrameworkElement? lastAnchor;
        private Windows.Foundation.Point? lastPosition;
        private IMenuSource? pendingSource;

        /// <summary>
        ///     Gets the underlying trigger element. This is used by <see cref="MenuInteractionController"/>
        ///     for focus capture as a UIElement.
        /// </summary>
        public UIElement TriggerElement { get; } = triggerElement;

        /// <inheritdoc/>
        public object? FocusElement => this.TriggerElement.XamlRoot is { } xamlRoot
            ? FocusManager.GetFocusedElement(xamlRoot)
            : null;

        public void SetTrigger(FrameworkElement anchor, Windows.Foundation.Point position)
        {
            this.lastAnchor = anchor;
            this.lastPosition = position;
        }

        public void SetPendingSource(IMenuSource source)
        {
            this.pendingSource = source;
        }

        /// <inheritdoc/>
        public void Show(MenuNavigationMode navigationMode)
        {
            if (this.lastAnchor is null || this.lastPosition is null)
            {
                // Nothing to show without a position; no-op.
                return;
            }

            // We need a host to display; since ContextMenu creates a cascaded menu host as attached property
            // the host is responsible for showing. Here we try to locate it via attached property storage.
            if (GetMenuHost(this.TriggerElement) is ICascadedMenuHost host)
            {
                if (this.pendingSource is not null)
                {
                    host.MenuSource = this.pendingSource;
                }

                host.RootSurface = this;
                host.ShowAt(this.lastAnchor!, this.lastPosition!.Value, navigationMode);
            }
        }

        /// <inheritdoc/>
        public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
        {
            // Delegate to the cascaded menu host that manages this context menu
            if (GetMenuHost(this.TriggerElement) is ICascadedMenuHost host)
            {
                host.Dismiss(kind);
            }
        }

        /// <inheritdoc/>
        /// <exception cref="NotSupportedException">
        ///     Context menus don't have adjacent items - they're standalone flyouts.
        /// </exception>
        public MenuItemData GetAdjacentItem(MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
            => throw new NotSupportedException("Context menus don't support root-level navigation.");

        /// <inheritdoc/>
        /// <returns>Always returns null as context menus don't have expanded root items.</returns>
        public MenuItemData? GetExpandedItem() => null;

        /// <inheritdoc/>
        /// <returns>Always returns null as context menus don't track focused items at root level.</returns>
        public MenuItemData? GetFocusedItem() => null;

        /// <inheritdoc/>
        /// <exception cref="NotSupportedException">
        ///     Context menus don't support programmatic focus of root items.
        /// </exception>
        public bool FocusItem(MenuItemData itemData, MenuNavigationMode navigationMode)
            => throw new NotSupportedException("Context menus don't support root-level focus navigation.");

        /// <inheritdoc/>
        /// <exception cref="NotSupportedException">
        ///     Context menus don't support programmatic focus of first item at root level.
        /// </exception>
        public bool FocusFirstItem(MenuNavigationMode navigationMode)
            => throw new NotSupportedException("Context menus don't support root-level focus navigation.");

        /// <inheritdoc/>
        /// <exception cref="NotSupportedException">
        ///     Context menus don't support expanding root items.
        /// </exception>
        public void ExpandItem(MenuItemData itemData, MenuNavigationMode navigationMode)
            => throw new NotSupportedException("Context menus don't support root-level expansion.");

        /// <inheritdoc/>
        /// <exception cref="NotSupportedException">
        ///     Context menus don't support collapsing root items.
        /// </exception>
        public void CollapseItem(MenuItemData itemData, MenuNavigationMode navigationMode)
            => throw new NotSupportedException("Context menus don't support root-level collapsing.");
    }
}
