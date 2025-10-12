// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     A <see cref="ICascadedMenuHost"/> implementation backed by <see cref="MenuFlyout"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class FlyoutMenuHost : ICascadedMenuHost
{
    private readonly MenuFlyout flyout;
    private MenuDismissKind pendingDismissKind = MenuDismissKind.Programmatic;
    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FlyoutMenuHost"/> class.
    /// </summary>
    public FlyoutMenuHost()
    {
        this.flyout = new MenuFlyout
        {
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
        };

        this.flyout.Opening += this.OnFlyoutOpening;
        this.flyout.Opened += this.OnFlyoutOpened;
        this.flyout.Closing += this.OnFlyoutClosing;
        this.flyout.Closed += this.OnFlyoutClosed;
    }

    /// <inheritdoc />
    public event EventHandler? Opening;

    /// <inheritdoc />
    public event EventHandler? Opened;

    /// <inheritdoc />
    public event EventHandler<MenuHostClosingEventArgs>? Closing;

    /// <inheritdoc />
    public event EventHandler? Closed;

    /// <inheritdoc />
    public FrameworkElement? Anchor => this.flyout.Target;

    /// <inheritdoc />
    public ICascadedMenuSurface Surface => this;

    /// <inheritdoc />
    public IRootMenuSurface? RootSurface
    {
        get => this.flyout.RootSurface;
        set => this.flyout.RootSurface = value;
    }

    /// <inheritdoc />
    public IMenuSource? MenuSource
    {
        get => this.flyout.MenuSource;
        set => this.flyout.MenuSource = value;
    }

    /// <inheritdoc />
    public double MaxLevelHeight
    {
        get => this.flyout.MaxColumnHeight;
        set => this.flyout.MaxColumnHeight = value;
    }

    /// <inheritdoc />
    public bool IsOpen => this.flyout.IsOpen;

    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        Debug.WriteLine("[FlyoutMenuHost] ICascadedMenuSurface.Dismiss()");
        this.pendingDismissKind = kind;
        this.flyout.Dismiss(kind);
    }

    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
        => this.RequirePresenter().GetAdjacentItem(level, itemData, direction, wrap);

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem(MenuLevel level)
        => this.RequirePresenter().GetExpandedItem(level);

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem(MenuLevel level)
        => this.RequirePresenter().GetFocusedItem(level);

    /// <inheritdoc />
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().FocusItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode)
        => this.RequirePresenter().FocusFirstItem(level, navigationMode);

    /// <inheritdoc />
    public void ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().ExpandItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.RequirePresenter().CollapseItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void TrimTo(MenuLevel level)
        => this.RequirePresenter().TrimTo(level);

    /// <inheritdoc />
    public void ShowAt(MenuItem anchor, MenuNavigationMode navigationMode)
    {
        Debug.WriteLine("[FlyoutMenuHost] ShowAt");
        _ = navigationMode;
        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.flyout.ShowAt(anchor);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;

        this.flyout.Opening -= this.OnFlyoutOpening;
        this.flyout.Opened -= this.OnFlyoutOpened;
        this.flyout.Closing -= this.OnFlyoutClosing;
        this.flyout.Closed -= this.OnFlyoutClosed;

        this.flyout.MenuSource = null;
        this.flyout.RootSurface = null;
    }

    private void OnFlyoutOpening(object? sender, object e)
        => this.Opening?.Invoke(this, EventArgs.Empty);

    private void OnFlyoutOpened(object? sender, object e)
        => this.Opened?.Invoke(this, EventArgs.Empty);

    private void OnFlyoutClosing(object? sender, FlyoutBaseClosingEventArgs e)
    {
        Debug.WriteLine("[FlyoutMenuHost] Closing");

        var dismissalKind = this.ResolveDismissKind();
        var args = new MenuHostClosingEventArgs(dismissalKind);
        this.Closing?.Invoke(this, args);
        if (args.Cancel)
        {
            e.Cancel = true;
            return;
        }

        this.pendingDismissKind = MenuDismissKind.Programmatic;
    }

    private void OnFlyoutClosed(object? sender, object e)
    {
        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.Closed?.Invoke(this, EventArgs.Empty);

        Debug.WriteLine("[FlyoutMenuHost] Closed");

        var controller = this.MenuSource?.Services.InteractionController;
        var rootSurface = this.RootSurface;
        if (controller is not null && rootSurface is not null)
        {
            var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, rootSurface);
            Debug.WriteLine("[FlyoutMenuHost] Notifying controller.OnDismissed (column)");
            controller.OnDismissed(context);
        }
    }

    private MenuDismissKind ResolveDismissKind() => this.pendingDismissKind;

    private CascadedColumnsPresenter RequirePresenter()
        => this.flyout.Presenter
            ?? throw new InvalidOperationException("Cascaded menu surface is not available before the host opens.");
}
