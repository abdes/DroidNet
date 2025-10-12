// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls.Menus;

/// <summary>
///     <see cref="ICascadedMenuHost"/> implementation backed by <see cref="Popup"/>.
///     Acts as its own <see cref="ICascadedMenuSurface"/>.
/// </summary>
internal sealed partial class PopupMenuHost : ICascadedMenuHost
{
    private const int OpenDebounceDelayMilliseconds = 35;

    private readonly Popup popup;
    private readonly CascadedColumnsPresenter presenter;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly TimeSpan openDebounceDelay = TimeSpan.FromMilliseconds(OpenDebounceDelayMilliseconds);
    private readonly KeyEventHandler presenterKeyDownHandler;
    private readonly PointerEventHandler rootPointerPressedHandler;
    private readonly KeyEventHandler rootKeyDownHandler;
    private DispatcherQueueTimer? openTimer;

    private PopupLifecycleState state = PopupLifecycleState.Idle;
    private MenuDismissKind pendingDismissKind = MenuDismissKind.Programmatic;
    private FrameworkElement? anchor;
    private UIElement? rootPassThroughElement;
    private UIElement? rootInputEventSource;
    private int nextToken;
    private PopupRequest? pendingRequest;
    private PopupRequest? activeRequest;
    private PopupRequest? closingRequest;
    private bool suppressProgrammaticDismissForPendingOpen;

    /// <summary>
    ///     Initializes a new instance of the <see cref="PopupMenuHost"/> class.
    /// </summary>
    public PopupMenuHost()
    {
        this.dispatcherQueue = DispatcherQueue.GetForCurrentThread();
        this.presenter = new CascadedColumnsPresenter
        {
            HorizontalAlignment = HorizontalAlignment.Stretch,
            VerticalAlignment = VerticalAlignment.Stretch,
        };

        this.presenterKeyDownHandler = this.HandleEscDismissal;
        this.presenter.AddHandler(UIElement.KeyDownEvent, this.presenterKeyDownHandler, handledEventsToo: true);

        this.rootPointerPressedHandler = this.OnRootPointerPressed;
        this.rootKeyDownHandler = this.HandleEscDismissal;

        this.popup = new Popup
        {
            Child = this.presenter,
            ShouldConstrainToRootBounds = false,
            IsLightDismissEnabled = false,
        };

        this.popup.Opened += this.OnPopupOpened;
        this.popup.Closed += this.OnPopupClosed;
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
    public FrameworkElement? Anchor => this.anchor;

    /// <inheritdoc />
    public ICascadedMenuSurface Surface => this;

    /// <inheritdoc />
    public IRootMenuSurface? RootSurface
    {
        get => this.presenter.RootSurface;
        set
        {
            this.presenter.RootSurface = value;
            this.rootPassThroughElement = value as UIElement;
        }
    }

    /// <inheritdoc />
    public IMenuSource? MenuSource
    {
        get => this.presenter.MenuSource;
        set => this.presenter.MenuSource = value;
    }

    /// <inheritdoc />
    public double MaxLevelHeight
    {
        get => this.presenter.MaxColumnHeight;
        set => this.presenter.MaxColumnHeight = value;
    }

    /// <inheritdoc />
    public bool IsOpen => this.popup.IsOpen;

    /// <inheritdoc />
    public MenuItemData GetAdjacentItem(MenuLevel level, MenuItemData itemData, MenuNavigationDirection direction, bool wrap = true)
        => this.presenter.GetAdjacentItem(level, itemData, direction, wrap);

    /// <inheritdoc />
    public MenuItemData? GetExpandedItem(MenuLevel level)
        => this.presenter.GetExpandedItem(level);

    /// <inheritdoc />
    public MenuItemData? GetFocusedItem(MenuLevel level)
        => this.presenter.GetFocusedItem(level);

    /// <inheritdoc />
    public bool FocusItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.FocusItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public bool FocusFirstItem(MenuLevel level, MenuNavigationMode navigationMode)
        => this.presenter.FocusFirstItem(level, navigationMode);

    /// <inheritdoc />
    public void ExpandItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.ExpandItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void CollapseItem(MenuLevel level, MenuItemData itemData, MenuNavigationMode navigationMode)
        => this.presenter.CollapseItem(level, itemData, navigationMode);

    /// <inheritdoc />
    public void TrimTo(MenuLevel level)
        => this.presenter.TrimTo(level);

    /// <inheritdoc />
    public void ShowAt(MenuItem anchorItem, MenuNavigationMode navigationMode)
    {
        ArgumentNullException.ThrowIfNull(anchorItem);

        this.anchor = anchorItem;
        this.pendingDismissKind = MenuDismissKind.Programmatic;

        var request = new PopupRequest(++this.nextToken, anchorItem, navigationMode);
        this.pendingRequest = request;

        this.EnsurePointerEventSubscription(anchorItem);

        var isReanchor = this.state != PopupLifecycleState.Idle || this.popup.IsOpen;
        this.suppressProgrammaticDismissForPendingOpen = isReanchor;

        this.Opening?.Invoke(this, EventArgs.Empty);

        this.ScheduleOpen();
    }

    /// <inheritdoc />
    public void Dismiss(MenuDismissKind kind = MenuDismissKind.Programmatic)
    {
        this.pendingDismissKind = kind;

        if (this.TryCancelPendingOpen(kind))
        {
            return;
        }

        if (this.TryCloseOpenPopup(kind))
        {
            return;
        }

        if (this.state == PopupLifecycleState.Closing)
        {
            return;
        }

        this.LogDismissIgnored(kind);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.popup.Opened -= this.OnPopupOpened;
        this.popup.Closed -= this.OnPopupClosed;

        if (this.openTimer is not null)
        {
            this.openTimer.Stop();
            this.openTimer.Tick -= this.OnOpenTimerTick;
            this.openTimer = null;
        }

        this.presenter.RemoveHandler(UIElement.KeyDownEvent, this.presenterKeyDownHandler);
        this.DetachPointerEventSubscription();
    }

    private bool TryCancelPendingOpen(MenuDismissKind kind)
    {
        if (this.state != PopupLifecycleState.PendingOpen || this.pendingRequest is not { } pending)
        {
            return false;
        }

        if (this.activeRequest is not null)
        {
            return false;
        }

        if (kind == MenuDismissKind.Programmatic && this.suppressProgrammaticDismissForPendingOpen)
        {
            this.LogDismissDeferred(kind, pending.Anchor);
            this.pendingDismissKind = MenuDismissKind.Programmatic;
            return true;
        }

        if (this.anchor is not null && !ReferenceEquals(this.anchor, pending.Anchor))
        {
            this.LogDismissIgnored(kind);
            return true;
        }

        var args = new MenuHostClosingEventArgs(kind);
        this.LogDismissRequested(kind);
        this.closingRequest = pending;
        this.Closing?.Invoke(this, args);
        if (args.Cancel)
        {
            this.LogDismissCancelled(kind);
            this.closingRequest = null;
            return true;
        }

        this.openTimer?.Stop();
        this.pendingRequest = null;
        this.anchor = null;
        this.activeRequest = null;
        this.closingRequest = null;
        this.state = PopupLifecycleState.Idle;
        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.suppressProgrammaticDismissForPendingOpen = false;
        this.DetachPointerEventSubscription();
        this.LogPendingOpenCancelled(kind, pending.Anchor);
        this.Closed?.Invoke(this, EventArgs.Empty);
        return true;
    }

    private bool TryCloseOpenPopup(MenuDismissKind kind)
    {
        if (this.state is not (PopupLifecycleState.Open or PopupLifecycleState.Opening))
        {
            return false;
        }

        if (this.pendingRequest is not null)
        {
            this.LogDismissDeferred(kind, this.pendingRequest.Value.Anchor);
            this.closingRequest = null;
            this.pendingDismissKind = MenuDismissKind.Programmatic;
            this.state = PopupLifecycleState.PendingOpen;
            return true;
        }

        var args = new MenuHostClosingEventArgs(kind);
        this.LogDismissRequested(kind);
        this.closingRequest = this.activeRequest;
        this.Closing?.Invoke(this, args);
        if (args.Cancel)
        {
            this.LogDismissCancelled(kind);
            this.closingRequest = null;
            return true;
        }

        this.state = PopupLifecycleState.Closing;
        this.popup.IsOpen = false;
        this.LogDismissAccepted(kind);
        return true;
    }

    private void OnPopupOpened(object? sender, object e)
    {
        this.LogPopupOpened();
        if (this.state != PopupLifecycleState.Closing)
        {
            this.state = PopupLifecycleState.Open;
        }

        this.Opened?.Invoke(this, EventArgs.Empty);
    }

    private void OnPopupClosed(object? sender, object e)
    {
        var dismissalKind = this.pendingDismissKind;
        if (dismissalKind == MenuDismissKind.Programmatic
            && this.closingRequest is null
            && this.state == PopupLifecycleState.Open)
        {
            dismissalKind = MenuDismissKind.PointerInput;
            this.pendingDismissKind = dismissalKind;
        }

        var closedRequest = this.closingRequest ?? this.activeRequest;
        var pendingRequestSnapshot = this.pendingRequest;
        var hasPendingRequest = pendingRequestSnapshot.HasValue;
        var hasNewerPending = false;
        if (hasPendingRequest && closedRequest is PopupRequest requestBeingClosed)
        {
            hasNewerPending = pendingRequestSnapshot!.Value.Token != requestBeingClosed.Token;
        }

        if (closedRequest is not null && this.activeRequest is { } active && closedRequest.Value.Token != active.Token)
        {
            this.LogPopupClosedIgnored(dismissalKind, closedRequest.Value.Anchor, active.Anchor);
            this.pendingDismissKind = MenuDismissKind.Programmatic;
            this.closingRequest = null;
            return;
        }

        this.CompleteClose(dismissalKind, resetSurface: !hasNewerPending, hasPendingRequest);

        if (hasPendingRequest)
        {
            this.ScheduleOpen();
        }
    }

    private void CompleteClose(MenuDismissKind dismissalKind, bool resetSurface, bool hasPendingRequest)
    {
        this.LogPopupClosed(dismissalKind);
        var anchorSnapshot = this.anchor;
        var menuSourceSnapshot = this.MenuSource;
        var controller = menuSourceSnapshot?.Services.InteractionController;
        var rootSurface = this.RootSurface;

        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.anchor = null;
        this.activeRequest = null;
        this.closingRequest = null;
        this.state = hasPendingRequest ? PopupLifecycleState.PendingOpen : PopupLifecycleState.Idle;
        this.suppressProgrammaticDismissForPendingOpen = false;
        if (!hasPendingRequest)
        {
            this.DetachPointerEventSubscription();
        }

        if (resetSurface)
        {
            this.presenter?.Dismiss();

            Debug.WriteLine("[PopupMenuHost] Resetting MenuSource after close");
            this.MenuSource = null;

            if (controller is not null && rootSurface is not null)
            {
                var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, rootSurface);
                Debug.WriteLine("[PopupMenuHost] Notifying controller.OnDismissed (column)");
                controller.OnDismissed(context);
            }
        }

        if (dismissalKind == MenuDismissKind.KeyboardInput && anchorSnapshot is UIElement anchorElement)
        {
            _ = anchorElement.Focus(FocusState.Keyboard);
        }

        this.Closed?.Invoke(this, EventArgs.Empty);
    }

    private void ScheduleOpen()
    {
        if (this.pendingRequest is null)
        {
            return;
        }

        this.EnsureOpenTimer();
        this.openTimer!.Stop();
        this.openTimer.Interval = this.openDebounceDelay;
        this.openTimer.Start();

        if (this.state is PopupLifecycleState.Idle or PopupLifecycleState.PendingOpen)
        {
            this.state = PopupLifecycleState.PendingOpen;
        }
    }

    private void EnsureOpenTimer()
    {
        if (this.openTimer is not null)
        {
            return;
        }

        this.openTimer = this.dispatcherQueue.CreateTimer();
        this.openTimer.IsRepeating = false;
        this.openTimer.Tick += this.OnOpenTimerTick;
    }

    private void OnOpenTimerTick(DispatcherQueueTimer sender, object args)
    {
        if (this.pendingRequest is not { } request)
        {
            this.state = PopupLifecycleState.Idle;
            return;
        }

        if (this.state == PopupLifecycleState.Closing)
        {
            sender.Start();
            return;
        }

        if (!this.IsAnchorLayoutReady(request.Anchor))
        {
            sender.Start();
            return;
        }

        if (!this.TryGetAnchorBounds(request, out var targetBounds))
        {
            return;
        }

        var anchorElement = request.Anchor;
        this.pendingRequest = null;
        this.activeRequest = request;
        this.anchor = anchorElement;
        this.suppressProgrammaticDismissForPendingOpen = false;

        this.EnsurePointerEventSubscription(anchorElement);

        var popupWasOpen = this.popup.IsOpen;
        this.LogShowAt(anchorElement, request.NavigationMode, targetBounds);
        this.popup.HorizontalOffset = targetBounds.Left;
        this.popup.VerticalOffset = targetBounds.Bottom;
        this.popup.XamlRoot = anchorElement.XamlRoot;

        if (!popupWasOpen)
        {
            this.state = PopupLifecycleState.Opening;
            this.popup.IsOpen = true;
        }
        else
        {
            this.state = PopupLifecycleState.Open;
        }

        if (request.NavigationMode == MenuNavigationMode.KeyboardInput)
        {
            _ = this.presenter.FocusFirstItem(MenuLevel.First, request.NavigationMode);
        }
    }

    private bool IsAnchorLayoutReady(MenuItem anchorElement) => this.popup is not null
        && anchorElement.IsLoaded
        && anchorElement.XamlRoot is not null
        && anchorElement.ActualWidth > 0
        && anchorElement.ActualHeight > 0;

    private bool TryGetAnchorBounds(PopupRequest request, out Rect targetBounds)
    {
        try
        {
            var transform = request.Anchor.TransformToVisual(visual: null);
            var topLeft = transform.TransformPoint(new Point(0, 0));
            var bottomRight = transform.TransformPoint(new Point(request.Anchor.ActualWidth, request.Anchor.ActualHeight));
            targetBounds = new Rect(topLeft, bottomRight);
            return true;
        }
        catch (InvalidOperationException ex)
        {
            this.HandleOpenFailure(ex, request);
        }
        catch (ArgumentException ex)
        {
            this.HandleOpenFailure(ex, request);
        }

        targetBounds = default;
        return false;
    }

    private void HandleOpenFailure(Exception ex, PopupRequest request)
    {
        this.LogShowAtFailed(request.Anchor, ex);
        this.pendingRequest = null;
        this.activeRequest = null;
        this.closingRequest = null;
        this.anchor = null;
        this.pendingDismissKind = MenuDismissKind.Programmatic;
        this.state = PopupLifecycleState.Idle;
        this.suppressProgrammaticDismissForPendingOpen = false;
        this.DetachPointerEventSubscription();
        this.Closed?.Invoke(this, EventArgs.Empty);
    }

    private void EnsurePointerEventSubscription(MenuItem anchorItem)
    {
        if (anchorItem.XamlRoot?.Content is not UIElement rootContent)
        {
            return;
        }

        if (ReferenceEquals(this.rootInputEventSource, rootContent))
        {
            return;
        }

        this.DetachPointerEventSubscription();
        rootContent.AddHandler(UIElement.PointerPressedEvent, this.rootPointerPressedHandler, handledEventsToo: true);
        rootContent.AddHandler(UIElement.KeyDownEvent, this.rootKeyDownHandler, handledEventsToo: false);
        this.rootInputEventSource = rootContent;
    }

    private void DetachPointerEventSubscription()
    {
        if (this.rootInputEventSource is not UIElement source)
        {
            return;
        }

        source.RemoveHandler(UIElement.PointerPressedEvent, this.rootPointerPressedHandler);
        source.RemoveHandler(UIElement.KeyDownEvent, this.rootKeyDownHandler);
        this.rootInputEventSource = null;
    }

    private void OnRootPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        if (this.state == PopupLifecycleState.Idle && this.pendingRequest is null)
        {
            return;
        }

        var windowPoint = e.GetCurrentPoint(relativeTo: null).Position;
        if (this.IsPointWithinPresenter(windowPoint))
        {
            return;
        }

        if (this.rootPassThroughElement is UIElement passThrough)
        {
            var hitRootItem = VisualTreeHelper.FindElementsInHostCoordinates(windowPoint, passThrough, includeAllElements: true)
                .OfType<MenuItem>()
                .FirstOrDefault();

            if (hitRootItem is not null)
            {
                if (!ReferenceEquals(hitRootItem, this.anchor))
                {
                    this.ShowAt(hitRootItem, MenuNavigationMode.PointerInput);
                }

                return;
            }
        }

        if (!this.TryRequestControllerDismiss(MenuDismissKind.PointerInput))
        {
            this.Dismiss(MenuDismissKind.PointerInput);
        }
    }

    private bool IsPointWithinPresenter(Point windowPoint)
    {
        if (!this.presenter.IsLoaded)
        {
            return false;
        }

        try
        {
            var transform = this.presenter.TransformToVisual(visual: null);
            var bounds = transform.TransformBounds(new Rect(0, 0, this.presenter.ActualWidth, this.presenter.ActualHeight));
            return bounds.Contains(windowPoint);
        }
        catch (InvalidOperationException)
        {
            return false;
        }
    }

    private void HandleEscDismissal(object sender, KeyRoutedEventArgs e)
    {
        if (e.Handled)
        {
            return;
        }

        if (e.Key != Windows.System.VirtualKey.Escape)
        {
            return;
        }

        var requireActiveInteraction = !ReferenceEquals(sender, this.presenter);
        if (requireActiveInteraction && this.state == PopupLifecycleState.Idle && this.pendingRequest is null)
        {
            return;
        }

        if (this.TryRequestControllerDismiss(MenuDismissKind.KeyboardInput))
        {
            e.Handled = true;
            return;
        }

        this.Dismiss(MenuDismissKind.KeyboardInput);
        e.Handled = true;
    }

    private bool TryRequestControllerDismiss(MenuDismissKind kind)
    {
        if (this.MenuSource?.Services.InteractionController is not { } controller)
        {
            return false;
        }

        if (this.presenter.RootSurface is not { } rootSurface)
        {
            return false;
        }

        var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, rootSurface);
        Debug.WriteLine($"[PopupMenuHost] Requesting controller dismiss kind={kind} contextKind={context.Kind} column={context.ColumnLevel}");
        var handled = controller.OnDismissRequested(context, kind);
        Debug.WriteLine($"[PopupMenuHost] Controller dismiss handled={handled}");
        return handled;
    }
}
