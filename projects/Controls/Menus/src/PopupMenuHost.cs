// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.InteropServices;
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
    private readonly PopupPlacementHelper placementHelper = new();
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
        this.DetachPointerEventSubscription();
        this.DetachWindowChangeSubscription();
        this.placementHelper.Reset();
    }

    private static Rect GetViewportRect(XamlRoot? xamlRoot)
    {
        if (xamlRoot is null)
        {
            return Rect.Empty;
        }

        var size = xamlRoot.Size;
        return new Rect(0, 0, size.Width, size.Height);
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
        this.placementHelper.Reset();
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

        // Clear any wait cursor
        CursorInterop.SetArrowCursor();

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
        this.placementHelper.Reset();
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

        var anchorElement = request.Anchor;
        this.pendingRequest = null;
        this.activeRequest = request;
        this.anchor = anchorElement;
        this.suppressProgrammaticDismissForPendingOpen = false;

        this.EnsurePointerEventSubscription(anchorElement);

        this.popup.XamlRoot = anchorElement.XamlRoot;

        var popupWasOpen = this.popup.IsOpen;
        var viewport = GetViewportRect(anchorElement.XamlRoot);
        if (!this.TrySetPopupPosition(request, anchorElement, viewport))
        {
            this.HandleOpenFailure(new InvalidOperationException("Unable to calculate popup placement."), request);
            return;
        }

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

    private bool TrySetPopupPosition(PopupRequest request, FrameworkElement anchorElement, Rect viewport)
    {
        var placementRequest = new PopupPlacementHelper.PlacementRequest(
            request.Token,
            anchorElement,
            this.presenter,
            viewport,
            request.CustomPosition);

        if (!this.placementHelper.TryPlace(placementRequest, out var placement))
        {
            return false;
        }

        this.popup.HorizontalOffset = placement.Offset.X;
        this.popup.VerticalOffset = placement.Offset.Y;

        this.LogShowAt(anchorElement, request.NavigationMode, placement.AnchorBounds);
        return true;
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
        this.placementHelper.Reset();
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
                    _ = this.ShowAt(hitRootItem, MenuNavigationMode.PointerInput);
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

    private void OnPresenterSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!this.popup.IsOpen || this.activeRequest is not { } request)
        {
            return;
        }

        var anchorElement = request.Anchor;
        if (anchorElement.XamlRoot is null)
        {
            return;
        }

        var viewport = GetViewportRect(anchorElement.XamlRoot);
        if (!this.TrySetPopupPosition(request, anchorElement, viewport))
        {
            this.HandleOpenFailure(new InvalidOperationException("Unable to reposition popup after size change."), request);
        }
    }

    private void OnXamlRootChanged(XamlRoot sender, XamlRootChangedEventArgs args)
    {
        if (!this.popup.IsOpen || this.activeRequest is not { } request)
        {
            return;
        }

        var anchorElement = request.Anchor;
        if (anchorElement.XamlRoot is null)
        {
            return;
        }

        var viewport = GetViewportRect(anchorElement.XamlRoot);
        if (!this.TrySetPopupPosition(request, anchorElement, viewport))
        {
            this.HandleOpenFailure(new InvalidOperationException("Unable to reposition popup after viewport change."), request);
        }
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

    private static partial class CursorInterop
    {
        private const int IDCARROW = 32512;

        private static readonly IntPtr ArrowCursorHandle = LoadCursor(IntPtr.Zero, new IntPtr(IDCARROW));

        public static void SetArrowCursor()
        {
            if (ArrowCursorHandle != IntPtr.Zero)
            {
                _ = SetCursor(ArrowCursorHandle);
            }
        }

        [LibraryImport("user32.dll", EntryPoint = "LoadCursorW", SetLastError = false)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial IntPtr LoadCursor(IntPtr hInstance, IntPtr lpCursorName);

        [LibraryImport("user32.dll", EntryPoint = "SetCursor", SetLastError = false)]
        [DefaultDllImportSearchPaths(DllImportSearchPath.System32)]
        private static partial IntPtr SetCursor(IntPtr hCursor);
    }
}
