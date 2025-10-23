// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;

namespace DroidNet.Controls.Menus;

/// <summary>
///     A <see cref="ICascadedMenuHost"/> implementation backed by <see cref="FlyoutBase"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class FlyoutMenuHost : ICascadedMenuHost
{
    private readonly FlyoutBase flyout;
    private CascadedColumnsPresenter? presenter;
    private MenuDismissKind pendingDismissKind = MenuDismissKind.Programmatic;
    private bool isDisposed;
    private double maxLevelHeight = 480d;

    /// <summary>
    ///     Initializes a new instance of the <see cref="FlyoutMenuHost"/> class.
    /// </summary>
    public FlyoutMenuHost()
    {
        this.flyout = new CustomFlyout(this)
        {
            Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft,
            LightDismissOverlayMode = LightDismissOverlayMode.Off,
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
    public UIElement RootElement => this.presenter ?? throw new InvalidOperationException("Presenter not yet created");

    /// <inheritdoc />
    public IRootMenuSurface? RootSurface
    {
        get;
        set
        {
            field = value;
            if (this.presenter is { })
            {
                this.presenter.RootSurface = value;
            }
        }
    }

    /// <inheritdoc />
    public IMenuSource? MenuSource
    {
        get;
        set
        {
            field = value;
            if (this.presenter is { })
            {
                this.presenter.MenuSource = value;
            }
        }
    }

    /// <inheritdoc />
    public double MaxLevelHeight
    {
        get => this.maxLevelHeight;
        set
        {
            this.maxLevelHeight = value;
            if (this.presenter is { })
            {
                this.presenter.MaxColumnHeight = value;
            }
        }
    }

    /// <inheritdoc />
    public bool IsOpen => this.flyout.IsOpen;

    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        this.LogDismiss(kind);
        this.pendingDismissKind = kind;
        this.presenter?.Dismiss(kind);
        this.flyout.Hide();
    }

    /// <inheritdoc />
    public bool ShowAt(FrameworkElement anchor, MenuNavigationMode navigationMode)
    {
        _ = navigationMode;

        if (anchor is MenuItem { ItemData: { IsExpanded: false } itemData })
        {
            itemData.IsExpanded = true;
        }

        this.pendingDismissKind = MenuDismissKind.Programmatic;
        var options = new FlyoutShowOptions
        {
            Position = new Windows.Foundation.Point(0, anchor.ActualHeight),
            ShowMode = FlyoutShowMode.Standard,
        };
        this.flyout.ShowAt(anchor, options);
        return true;
    }

    /// <inheritdoc />
    public bool ShowAt(FrameworkElement anchor, Windows.Foundation.Point position, MenuNavigationMode navigationMode)
    {
        _ = navigationMode;
        this.pendingDismissKind = MenuDismissKind.Programmatic;

        var options = new FlyoutShowOptions
        {
            Position = position,
            ShowMode = FlyoutShowMode.Standard,
        };
        this.flyout.ShowAt(anchor, options);
        return true;
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

        if (this.presenter is not null)
        {
            this.presenter.ItemInvoked -= this.OnPresenterItemInvoked;
        }

        this.MenuSource = null;
        this.RootSurface = null;
    }

    /// <summary>
    ///     Creates the presenter for the flyout.
    /// </summary>
    /// <returns>The created presenter control.</returns>
    internal Control CreatePresenter()
    {
        this.LogCreatePresenter();

        // Clean up existing presenter if it exists
        if (this.presenter is not null)
        {
            this.presenter.ItemInvoked -= this.OnPresenterItemInvoked;
        }

        this.presenter = new CascadedColumnsPresenter
        {
            MaxColumnHeight = this.MaxLevelHeight,
            MenuSource = this.MenuSource,
            RootSurface = this.RootSurface,
        };

        // Subscribe to presenter events to relay to controller
        this.presenter.ItemInvoked += this.OnPresenterItemInvoked;

        return this.presenter;
    }

    private void OnPresenterItemInvoked(object? sender, MenuItemInvokedEventArgs e)
    {
        if (this.MenuSource is not { Services.InteractionController: { } controller })
        {
            return;
        }

        // Create the context - we're in a cascaded menu, so use the column surface
        var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, this.RootSurface);
        controller.OnItemInvoked(context, e.ItemData, e.InputSource);
    }

    private void OnFlyoutOpening(object? sender, object e)
    {
        Debug.Assert(this.presenter is { }, "Presenter should not be null");

        this.LogOpening();

        if (this.MenuSource is null)
        {
            this.LogNoMenuSource();
            return;
        }

        // We take out the presenter from the focus chain, so that FlyoutMenu does not automatically
        // focus the first item. We'll manage focus ourselves once opened, using `ColumnPresenter`.
        this.presenter.IsTabStop = false;

        // If the Flyout is used with a root surface (e.g., MenuBar), ensure the root surface
        // is set as the pass-through element for input events.
        if (this.RootSurface is UIElement passThrough)
        {
            this.flyout.OverlayInputPassThroughElement = passThrough;
        }

        // Raise the public Opening event
        this.Opening?.Invoke(this, EventArgs.Empty);
    }

    private void OnFlyoutOpened(object? sender, object e)
    {
        this.LogOpened();
        this.Opened?.Invoke(this, EventArgs.Empty);
    }

    private void OnFlyoutClosing(object? sender, FlyoutBaseClosingEventArgs e)
    {
        this.LogClosing();

        // Handle situations where the flyout is open and closed before the presenter is created.
        if (this.presenter is not { })
        {
            return;
        }

        this.flyout.OverlayInputPassThroughElement = null;

        // Raise the public Closing event
        var dismissalKind = this.ResolveDismissKind();
        var args = new MenuHostClosingEventArgs(dismissalKind);
        this.Closing?.Invoke(this, args);
        if (args.Cancel)
        {
            e.Cancel = true;
            return;
        }

        // Reset the presenter after closing to prepare it for the next open
        this.presenter.Reset();

        this.pendingDismissKind = MenuDismissKind.Programmatic;
    }

    private void OnFlyoutClosed(object? sender, object e)
    {
        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.Closed?.Invoke(this, EventArgs.Empty);

        var controller = this.MenuSource?.Services.InteractionController;
        var surface = this.RootSurface;
        if (controller is not null && surface is not null)
        {
            var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, surface);
            controller.OnDismissed(context);
        }
    }

    private MenuDismissKind ResolveDismissKind() => this.pendingDismissKind;

    private CascadedColumnsPresenter RequirePresenter()
        => this.presenter
            ?? throw new InvalidOperationException("Cascaded menu surface is not available before the host opens.");

    /// <summary>
    ///     Custom FlyoutBase implementation that delegates presenter creation to FlyoutMenuHost.
    /// </summary>
    private sealed partial class CustomFlyout(FlyoutMenuHost host) : FlyoutBase
    {
        private readonly FlyoutMenuHost host = host;

        protected override Control CreatePresenter()
            => this.host.CreatePresenter();
    }
}
