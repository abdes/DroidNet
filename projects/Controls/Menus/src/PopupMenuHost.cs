// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
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
    private const double WindowEdgePadding = 4d;
    private const double PointerEdgePadding = 1.5d;
    private const double PointerSafetyMargin = 20d;

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
    private XamlRoot? trackedXamlRoot;
    private int nextToken;
    private PopupRequest? pendingRequest;
    private PopupRequest? activeRequest;
    private PopupRequest? closingRequest;
    private bool suppressProgrammaticDismissForPendingOpen;
    private Point? lastPopupOffset;
    private Size lastPopupSize = Size.Empty;
    private HorizontalPlacement lastHorizontalPlacement = HorizontalPlacement.None;
    private VerticalPlacement lastVerticalPlacement = VerticalPlacement.None;
    private int lastPlacementToken;
    private Point? lastPointerWindowPosition;

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

        // Subscribe to presenter events to relay to controller
        this.presenter.ItemInvoked += this.OnPresenterItemInvoked;
        this.presenter.SizeChanged += this.OnPresenterSizeChanged;
    this.presenter.PointerMoved += this.OnPresenterPointerMoved;
    this.presenter.PointerExited += this.OnPresenterPointerExited;

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
    public UIElement RootElement => this.presenter;

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
    public bool ShowAt(FrameworkElement anchorElement, MenuNavigationMode navigationMode)
    {
        ArgumentNullException.ThrowIfNull(anchorElement);

        this.anchor = anchorElement;
        this.pendingDismissKind = MenuDismissKind.Programmatic;

        var request = new PopupRequest(++this.nextToken, anchorElement, navigationMode);
        this.pendingRequest = request;

        this.EnsurePointerEventSubscription(anchorElement);

        var isReanchor = this.state != PopupLifecycleState.Idle || this.popup.IsOpen;
        this.suppressProgrammaticDismissForPendingOpen = isReanchor;

        this.Opening?.Invoke(this, EventArgs.Empty);

        this.ScheduleOpen();
        return true;
    }

    /// <inheritdoc />
    public bool ShowAt(FrameworkElement anchor, Windows.Foundation.Point position, MenuNavigationMode navigationMode)
    {
        ArgumentNullException.ThrowIfNull(anchor);

        this.anchor = anchor;
        this.pendingDismissKind = MenuDismissKind.Programmatic;

        var request = new PopupRequest(++this.nextToken, anchor, navigationMode, customPosition: position);
        this.pendingRequest = request;

        this.EnsurePointerEventSubscription(anchor);

        var isReanchor = this.state != PopupLifecycleState.Idle || this.popup.IsOpen;
        this.suppressProgrammaticDismissForPendingOpen = isReanchor;

        this.Opening?.Invoke(this, EventArgs.Empty);

        this.ScheduleOpen();
        return true;
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
        this.presenter.SizeChanged -= this.OnPresenterSizeChanged;
        this.presenter.PointerMoved -= this.OnPresenterPointerMoved;
        this.presenter.PointerExited -= this.OnPresenterPointerExited;
        this.DetachPointerEventSubscription();
        this.DetachWindowChangeSubscription();
        this.ResetPlacementTracking();
        this.lastPointerWindowPosition = null;
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
        this.ResetPlacementTracking();
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
        this.ResetPlacementTracking();
        if (!hasPendingRequest)
        {
            this.DetachPointerEventSubscription();
        }

        if (resetSurface)
        {
            this.presenter?.Dismiss();

            this.MenuSource = null;

            if (controller is not null && rootSurface is not null)
            {
                var context = MenuInteractionContext.ForColumn(MenuLevel.First, this, rootSurface);
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

        this.SetPopupPosition(request, targetBounds);
        this.popup.XamlRoot = anchorElement.XamlRoot;

        // Align theme with the anchor to match Flyout behavior (Popup doesn't inherit theme automatically)
        this.ApplyThemeFromAnchor(anchorElement);

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

    // Mirror the anchor's ActualTheme so ThemeResource lookups in the presenter resolve
    // consistently with FlyoutPresenter visuals.
    private void ApplyThemeFromAnchor(FrameworkElement anchorElement) =>
        this.presenter.RequestedTheme = anchorElement.ActualTheme;

    private bool IsAnchorLayoutReady(FrameworkElement anchorElement) => this.popup is not null
        && anchorElement.IsLoaded
        && anchorElement.XamlRoot is not null
        && anchorElement.ActualWidth > 0
        && anchorElement.ActualHeight > 0;

    private void SetPopupPosition(PopupRequest request, Rect targetBounds)
    {
        if (this.lastPlacementToken != request.Token)
        {
            this.ResetPlacementTracking();
            this.lastPlacementToken = request.Token;
        }

        var presenterSize = this.GetPresenterDesiredSize(targetBounds);
        var offsets = this.CalculatePopupOffsets(request, targetBounds, presenterSize, out var horizontalPlacement, out var verticalPlacement);

        this.popup.HorizontalOffset = offsets.X;
        this.popup.VerticalOffset = offsets.Y;

        this.lastPopupOffset = offsets;
        this.lastPopupSize = presenterSize;
        this.lastHorizontalPlacement = horizontalPlacement;
        this.lastVerticalPlacement = verticalPlacement;
    }

    private bool TryGetAnchorBounds(PopupRequest request, out Rect targetBounds)
    {
        try
        {
            var transform = request.Anchor.TransformToVisual(visual: null);

            // If custom position is provided, use it; otherwise use anchor's bottom-left
            if (request.CustomPosition is { } customPos)
            {
                var topLeft = transform.TransformPoint(customPos);
                targetBounds = new Rect(topLeft, new Windows.Foundation.Size(1, 1));
            }
            else
            {
                var topLeft = transform.TransformPoint(new Point(0, 0));
                var bottomRight = transform.TransformPoint(new Point(request.Anchor.ActualWidth, request.Anchor.ActualHeight));
                targetBounds = new Rect(topLeft, bottomRight);
            }

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
        this.ResetPlacementTracking();
        this.DetachPointerEventSubscription();
        this.Closed?.Invoke(this, EventArgs.Empty);
    }

    private void EnsurePointerEventSubscription(FrameworkElement anchorElement)
    {
        if (anchorElement.XamlRoot?.Content is not UIElement rootContent)
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
        this.EnsureWindowChangeSubscription(anchorElement);
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
        this.DetachWindowChangeSubscription();
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

    private void OnPresenterPointerMoved(object sender, PointerRoutedEventArgs e)
    {
        if (!this.popup.IsOpen)
        {
            return;
        }

        var position = e.GetCurrentPoint(relativeTo: null).Position;
        this.lastPointerWindowPosition = position;
    }

    private void OnPresenterPointerExited(object sender, PointerRoutedEventArgs e)
    {
        if (!this.popup.IsOpen)
        {
            return;
        }

        var position = e.GetCurrentPoint(relativeTo: null).Position;
        this.lastPointerWindowPosition = position;
    }

    private void OnPresenterSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!this.popup.IsOpen || this.activeRequest is not { } request)
        {
            return;
        }

        if (!this.TryGetAnchorBounds(request, out var targetBounds))
        {
            return;
        }

        this.SetPopupPosition(request, targetBounds);
    }

    private void OnXamlRootChanged(XamlRoot sender, XamlRootChangedEventArgs args)
    {
        if (!this.popup.IsOpen || this.activeRequest is not { } request)
        {
            return;
        }

        if (!this.TryGetAnchorBounds(request, out var targetBounds))
        {
            return;
        }

        this.SetPopupPosition(request, targetBounds);
    }

    private void EnsureWindowChangeSubscription(FrameworkElement anchorElement)
    {
        if (anchorElement.XamlRoot is null)
        {
            return;
        }

        if (ReferenceEquals(this.trackedXamlRoot, anchorElement.XamlRoot))
        {
            return;
        }

        this.DetachWindowChangeSubscription();
        this.trackedXamlRoot = anchorElement.XamlRoot;
        this.trackedXamlRoot.Changed += this.OnXamlRootChanged;
    }

    private void DetachWindowChangeSubscription()
    {
        if (this.trackedXamlRoot is null)
        {
            return;
        }

        this.trackedXamlRoot.Changed -= this.OnXamlRootChanged;
        this.trackedXamlRoot = null;
    }

    private Size GetPresenterDesiredSize(Rect anchorBounds)
    {
        this.presenter.Measure(new Size(double.PositiveInfinity, double.PositiveInfinity));
        var desired = this.presenter.DesiredSize;

        var width = this.ResolveDimension(desired.Width, this.presenter.ActualWidth, anchorBounds.Width);
        var height = this.ResolveDimension(desired.Height, this.presenter.ActualHeight, anchorBounds.Height);

        return new Size(width, height);
    }

    private double ResolveDimension(double desired, double actual, double fallback)
    {
        if (double.IsNaN(desired) || double.IsInfinity(desired) || desired <= 0)
        {
            desired = actual;
        }

        if (double.IsNaN(desired) || double.IsInfinity(desired) || desired <= 0)
        {
            desired = fallback;
        }

        return desired > 0 ? desired : 1d;
    }

    private Point CalculatePopupOffsets(
        PopupRequest request,
        Rect anchorBounds,
        Size popupSize,
        out HorizontalPlacement horizontalPlacement,
        out VerticalPlacement verticalPlacement)
    {
        horizontalPlacement = HorizontalPlacement.None;
        verticalPlacement = VerticalPlacement.None;

        var width = popupSize.Width;
        var height = popupSize.Height;

        var preferredHorizontal = anchorBounds.Left;
        var preferredVertical = request.CustomPosition.HasValue ? anchorBounds.Top : anchorBounds.Bottom;

        var anchorHorizontalMin = anchorBounds.Right - width;
        var anchorHorizontalMax = request.CustomPosition.HasValue ? anchorBounds.Left : anchorBounds.Right;
        var anchorVerticalMin = anchorBounds.Bottom - height;
        var anchorVerticalMax = request.CustomPosition.HasValue ? anchorBounds.Top : anchorBounds.Bottom;

        EnsureOrdered(ref anchorHorizontalMin, ref anchorHorizontalMax);
        EnsureOrdered(ref anchorVerticalMin, ref anchorVerticalMax);

        double allowedHorizontalMin = anchorHorizontalMin;
        double allowedHorizontalMax = anchorHorizontalMax;
        double allowedVerticalMin = anchorVerticalMin;
        double allowedVerticalMax = anchorVerticalMax;

        var horizontal = SafeClamp(preferredHorizontal, allowedHorizontalMin, allowedHorizontalMax);
        var vertical = SafeClamp(preferredVertical, allowedVerticalMin, allowedVerticalMax);

        var xamlRoot = request.Anchor.XamlRoot;
        double windowWidth = double.NaN;
        double windowHeight = double.NaN;
        var windowConstraintHorizontalApplied = false;
        var windowConstraintVerticalApplied = false;

        if (xamlRoot is { } root)
        {
            windowWidth = root.Size.Width;
            windowHeight = root.Size.Height;

            if (IsPositiveFinite(windowWidth))
            {
                var availableWidth = Math.Max(0d, windowWidth - (2 * WindowEdgePadding));
                if (width <= availableWidth)
                {
                    windowConstraintHorizontalApplied = true;
                    var windowMin = WindowEdgePadding;
                    var windowMax = windowWidth - WindowEdgePadding - width;

                    var intersectionMin = Math.Max(allowedHorizontalMin, windowMin);
                    var intersectionMax = Math.Min(allowedHorizontalMax, windowMax);

                    if (intersectionMin <= intersectionMax)
                    {
                        allowedHorizontalMin = intersectionMin;
                        allowedHorizontalMax = intersectionMax;
                    }
                    else
                    {
                        allowedHorizontalMin = windowMin;
                        allowedHorizontalMax = windowMax;
                    }

                    horizontal = SafeClamp(horizontal, allowedHorizontalMin, allowedHorizontalMax);
                }
            }

            if (IsPositiveFinite(windowHeight))
            {
                var availableHeight = Math.Max(0d, windowHeight - (2 * WindowEdgePadding));
                if (height <= availableHeight)
                {
                    windowConstraintVerticalApplied = true;
                    var windowMin = WindowEdgePadding;
                    var windowMax = windowHeight - WindowEdgePadding - height;

                    var intersectionMin = Math.Max(allowedVerticalMin, windowMin);
                    var intersectionMax = Math.Min(allowedVerticalMax, windowMax);

                    if (intersectionMin <= intersectionMax)
                    {
                        allowedVerticalMin = intersectionMin;
                        allowedVerticalMax = intersectionMax;
                    }
                    else
                    {
                        allowedVerticalMin = windowMin;
                        allowedVerticalMax = windowMax;
                    }

                    vertical = SafeClamp(vertical, allowedVerticalMin, allowedVerticalMax);
                }
            }
        }

        if (!windowConstraintHorizontalApplied)
        {
            horizontal = SafeClamp(horizontal, allowedHorizontalMin, allowedHorizontalMax);
        }

        if (!windowConstraintVerticalApplied)
        {
            vertical = SafeClamp(vertical, allowedVerticalMin, allowedVerticalMax);
        }

        if (this.lastPointerWindowPosition is { } pointer
            && this.lastPopupOffset is { } previousOffset
            && this.lastPlacementToken == request.Token)
        {
            var priorWidth = this.lastPopupSize.Width;
            var priorHeight = this.lastPopupSize.Height;
            var pointerWithinPopup = priorWidth > 0
                && priorHeight > 0
                && pointer.X >= previousOffset.X
                && pointer.X <= previousOffset.X + priorWidth
                && pointer.Y >= previousOffset.Y
                && pointer.Y <= previousOffset.Y + priorHeight;

            if (pointerWithinPopup)
            {
                var pointerMargin = PointerSafetyMargin;
                var pointerRangeMin = pointer.X + pointerMargin - width;
                var pointerRangeMax = pointer.X - pointerMargin;

                if (pointerRangeMin <= pointerRangeMax)
                {
                    var pointerIntersectionMin = Math.Max(allowedHorizontalMin, pointerRangeMin);
                    var pointerIntersectionMax = Math.Min(allowedHorizontalMax, pointerRangeMax);

                    if (pointerIntersectionMin <= pointerIntersectionMax)
                    {
                        horizontal = SafeClamp(horizontal, pointerIntersectionMin, pointerIntersectionMax);
                        allowedHorizontalMin = pointerIntersectionMin;
                        allowedHorizontalMax = pointerIntersectionMax;
                    }
                    else
                    {
                        var fallbackMin = Math.Max(allowedHorizontalMin, pointer.X - width + PointerEdgePadding);
                        var fallbackMax = Math.Min(allowedHorizontalMax, pointer.X - PointerEdgePadding);
                        if (fallbackMin <= fallbackMax)
                        {
                            horizontal = SafeClamp(horizontal, fallbackMin, fallbackMax);
                            allowedHorizontalMin = fallbackMin;
                            allowedHorizontalMax = fallbackMax;
                        }
                    }
                }
                else
                {
                    var fallbackMin = Math.Max(allowedHorizontalMin, pointer.X - width + PointerEdgePadding);
                    var fallbackMax = Math.Min(allowedHorizontalMax, pointer.X - PointerEdgePadding);
                    if (fallbackMin <= fallbackMax)
                    {
                        horizontal = SafeClamp(horizontal, fallbackMin, fallbackMax);
                        allowedHorizontalMin = fallbackMin;
                        allowedHorizontalMax = fallbackMax;
                    }
                }
            }
        }

        if (windowConstraintHorizontalApplied && IsPositiveFinite(windowWidth))
        {
            var leftEdge = WindowEdgePadding;
            var rightEdge = windowWidth - WindowEdgePadding;
            if (AreClose(horizontal, leftEdge))
            {
                horizontalPlacement = HorizontalPlacement.WindowLeading;
            }
            else if (AreClose(horizontal + width, rightEdge))
            {
                horizontalPlacement = HorizontalPlacement.WindowTrailing;
            }
        }

        if (windowConstraintVerticalApplied && IsPositiveFinite(windowHeight))
        {
            var topEdge = WindowEdgePadding;
            var bottomEdge = windowHeight - WindowEdgePadding;
            if (AreClose(vertical, topEdge))
            {
                verticalPlacement = VerticalPlacement.WindowTop;
            }
            else if (AreClose(vertical + height, bottomEdge))
            {
                verticalPlacement = VerticalPlacement.WindowBottom;
            }
        }

        if (horizontalPlacement == HorizontalPlacement.None)
        {
            if (request.CustomPosition.HasValue)
            {
                horizontalPlacement = HorizontalPlacement.CustomOrigin;
            }
            else if (AreClose(horizontal, anchorBounds.Left))
            {
                horizontalPlacement = HorizontalPlacement.AnchorLeading;
            }
            else if (AreClose(horizontal + width, anchorBounds.Right))
            {
                horizontalPlacement = HorizontalPlacement.AnchorTrailing;
            }
        }

        if (verticalPlacement == VerticalPlacement.None)
        {
            if (request.CustomPosition.HasValue)
            {
                verticalPlacement = VerticalPlacement.CustomOrigin;
            }
            else if (AreClose(vertical, anchorVerticalMin))
            {
                verticalPlacement = VerticalPlacement.AnchorTop;
            }
            else if (AreClose(vertical, anchorVerticalMax))
            {
                verticalPlacement = VerticalPlacement.AnchorBottom;
            }
        }

        return new Point(horizontal, vertical);
    }

    private void ResetPlacementTracking()
    {
        this.lastPopupOffset = null;
        this.lastPopupSize = Size.Empty;
        this.lastHorizontalPlacement = HorizontalPlacement.None;
        this.lastVerticalPlacement = VerticalPlacement.None;
        this.lastPlacementToken = 0;
        this.lastPointerWindowPosition = null;
    }

    private static bool IsPositiveFinite(double value) => !double.IsNaN(value) && !double.IsInfinity(value) && value > 0;

    private static bool AreClose(double first, double second)
    {
        const double tolerance = 0.5;
        return Math.Abs(first - second) <= tolerance;
    }

    private static void EnsureOrdered(ref double min, ref double max)
    {
        if (min <= max)
        {
            return;
        }

        (min, max) = (max, min);
    }

    private static double SafeClamp(double value, double min, double max)
    {
        if (min > max)
        {
            (min, max) = (max, min);
        }

        return Math.Clamp(value, min, max);
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
        var handled = controller.OnDismissRequested(context, kind);
        return handled;
    }

    private enum HorizontalPlacement
    {
        None,
        AnchorLeading,
        AnchorTrailing,
        WindowLeading,
        WindowTrailing,
        CustomOrigin,
    }

    private enum VerticalPlacement
    {
        None,
        AnchorTop,
        AnchorBottom,
        WindowTop,
        WindowBottom,
        CustomOrigin,
    }
}
