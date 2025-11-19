// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Coordinates Aura-managed windows within a WinUI 3 host.
/// </summary>
public sealed partial class WindowManagerService : IWindowManagerService
{
    private readonly ILogger<WindowManagerService> logger;
    private readonly IEnumerable<IMenuProvider> menuProviders;
    private readonly ISettingsService<IWindowDecorationSettings>? decorationSettingsService;
    private readonly DispatcherQueue dispatcherQueue;

    private readonly ConcurrentDictionary<WindowId, ManagedWindow> windows = new();
    private readonly Subject<WindowLifecycleEvent> windowEventsSubject = new();
    private readonly Subject<WindowMetadataChange> metadataChangedSubject = new();
    private readonly IDisposable? routerContextCreatedSubscription;
    private readonly IDisposable? routerContextDestroyedSubscription;

    private ManagedWindow? activeWindow;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowManagerService"/> class.
    /// Coordinates Aura-managed windows within a WinUI 3 host, providing registration,
    /// activation, decoration, and lifecycle management for windows.
    /// </summary>
    /// <param name="hostingContext">The hosting context containing dispatcher and application references.</param>
    /// <param name="menuProviders">A collection of menu providers for window menus.</param>
    /// <param name="loggerFactory">Optional logger factory for logging window manager events.</param>
    /// <param name="router">Optional router for integration with navigation events.</param>
    /// <param name="decorationSettingsService">Optional settings service for window decoration configuration.</param>
    public WindowManagerService(
        HostingContext hostingContext,
        IEnumerable<IMenuProvider> menuProviders,
        ILoggerFactory? loggerFactory = null,
        IRouter? router = null,
        ISettingsService<IWindowDecorationSettings>? decorationSettingsService = null)
    {
        ArgumentNullException.ThrowIfNull(hostingContext);
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher, nameof(hostingContext));
        ArgumentNullException.ThrowIfNull(menuProviders);

        this.logger = loggerFactory?.CreateLogger<WindowManagerService>() ?? NullLogger<WindowManagerService>.Instance;
        this.menuProviders = menuProviders;
        this.dispatcherQueue = hostingContext.Dispatcher;
        this.decorationSettingsService = decorationSettingsService;

        if (router is not null)
        {
            this.routerContextCreatedSubscription = router.Events
                .OfType<ContextCreated>()
                .Subscribe(this.OnRouterContextCreated);

            this.routerContextDestroyedSubscription = router.Events
                .OfType<ContextDestroyed>()
                .Subscribe(this.OnRouterContextDestroyed);

            this.LogRouterIntegrationEnabled();
        }
        else
        {
            this.LogRouterNotAvailable();
        }

        this.LogServiceInitialized();
    }

    /// <inheritdoc />
    public event AsyncEventHandler<PresenterStateChangeEventArgs>? PresenterStateChanged;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowClosingEventArgs>? WindowClosing;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowClosedEventArgs>? WindowClosed;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowBoundsChangedEventArgs>? WindowBoundsChanged;

    /// <inheritdoc />
    public IManagedWindow? ActiveWindow => this.activeWindow;

    /// <inheritdoc />
    public IReadOnlyCollection<IManagedWindow> OpenWindows => this.windows.Values.ToList().AsReadOnly();

    /// <inheritdoc />
    public IObservable<WindowLifecycleEvent> WindowEvents => this.windowEventsSubject.AsObservable();

    /// <inheritdoc />
    public IObservable<WindowMetadataChange> MetadataChanged => this.metadataChangedSubject.AsObservable();

    /// <inheritdoc />
    public async Task<IManagedWindow> RegisterWindowAsync(Window window, IReadOnlyDictionary<string, object>? metadata = null)
        => await this.RegisterDecoratedWindowAsync(window, WindowCategory.System, metadata).ConfigureAwait(false);

    /// <inheritdoc />
    public async Task<IManagedWindow> RegisterDecoratedWindowAsync(
        Window window,
        WindowCategory category,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        if (this.windows.Values.Any(wc => ReferenceEquals(wc.Window, window)))
        {
            throw new InvalidOperationException("Window is already registered with the window manager");
        }

        ManagedWindow? context = null;

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                var resolvedDecoration = this.ResolveDecoration(window.AppWindow.Id, category);
                context = this.CreateManagedWindow(window, category, resolvedDecoration, metadata);

                this.RegisterWindowEvents(context);

                if (!this.windows.TryAdd(context.Id, context))
                {
                    throw new InvalidOperationException($"Failed to register window with ID {context.Id.Value}");
                }

                this.LogWindowRegistered(context.Id, category);
                this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Created, context));

                // Initialize RestoredBounds with current bounds
                context.RestoredBounds = context.CurrentBounds;

                if (resolvedDecoration?.ChromeEnabled == true)
                {
                    window.ExtendsContentIntoTitleBar = true;
                }
            }
            catch (Exception ex)
            {
                this.LogRegisterWindowFailed(ex, window.GetType().Name);
                throw new InvalidOperationException($"Failed to register window of type {window.GetType().Name}", ex);
            }
        }).ConfigureAwait(true);

        return context ?? throw new InvalidOperationException("Window registration failed");
    }

    /// <inheritdoc />
    public async Task<bool> CloseWindowAsync(WindowId windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        if (!this.windows.TryGetValue(windowId, out var context))
        {
            this.LogCloseMissingWindow(windowId);
            return false;
        }

        // Raise Closing event
        var args = new WindowClosingEventArgs { WindowId = windowId };
        if (this.WindowClosing != null)
        {
            await this.WindowClosing.Invoke(this, args).ConfigureAwait(true);
        }

        if (args.Cancel)
        {
            return false;
        }

        var result = false;
        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            this.LogClosingWindow(windowId);
            context.Window.Close();
            result = true;
        }).ConfigureAwait(true);

        return result;
    }

    /// <inheritdoc />
    public async Task CloseAllWindowsAsync()
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        var windowIds = this.windows.Keys.ToList();
        this.LogClosingAllWindows(windowIds.Count);
        foreach (var windowId in windowIds)
        {
            _ = await this.CloseWindowAsync(windowId).ConfigureAwait(false);
        }
    }

    /// <inheritdoc />
    public void ActivateWindow(WindowId windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        if (!this.windows.ContainsKey(windowId))
        {
            this.LogActivateMissingWindow(windowId);
            return;
        }

        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                if (!this.windows.TryGetValue(windowId, out var targetContext))
                {
                    return;
                }

                targetContext.Window.Activate();
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                this.LogActivateWindowFailed(ex, windowId);
            }
#pragma warning restore CA1031 // Do not catch general exception types
        });
    }

    /// <inheritdoc />
    public IManagedWindow? GetWindow(WindowId windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        return this.windows.TryGetValue(windowId, out var context) ? context : null;
    }

    /// <inheritdoc />
    public async Task MinimizeWindowAsync(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.MinimizeAsync().ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task MaximizeWindowAsync(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.MaximizeAsync().ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task RestoreWindowAsync(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.RestoreAsync().ConfigureAwait(false);

        // Manually trigger presenter state change notification since WinUI doesn't always
        // fire AppWindow.Changed.DidPresenterChange for minimize→restore transitions
        this.OnPresenterChanged(windowId);
    }

    /// <inheritdoc />
    public async Task MoveWindowAsync(WindowId windowId, Windows.Graphics.PointInt32 position)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.MoveAsync(position).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task ResizeWindowAsync(WindowId windowId, Windows.Graphics.SizeInt32 size)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.ResizeAsync(size).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task SetWindowBoundsAsync(WindowId windowId, Windows.Graphics.RectInt32 bounds)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.SetBoundsAsync(bounds).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task SetWindowMinimumSizeAsync(WindowId windowId, int? minimumWidth, int? minimumHeight)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await window.DispatcherQueue.EnqueueAsync(() =>
        {
            if (window.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                presenter.PreferredMinimumWidth = minimumWidth;
                presenter.PreferredMinimumHeight = minimumHeight;
                window.MinimumWidth = minimumWidth;
                window.MinimumHeight = minimumHeight;
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public void SetMetadata(WindowId windowId, string key, object? value)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        object? oldValue = null;
        var newValue = value;

        lock (window)
        {
            var currentMetadata = window.Metadata != null
                ? new Dictionary<string, object>(window.Metadata, StringComparer.Ordinal)
                : new Dictionary<string, object>(StringComparer.Ordinal);

            if (currentMetadata.TryGetValue(key, out var existingValue))
            {
                oldValue = existingValue;
            }

            if (Equals(oldValue, newValue))
            {
                return;
            }

            if (newValue == null)
            {
                _ = currentMetadata.Remove(key);
            }
            else
            {
                currentMetadata[key] = newValue;
            }

            window.Metadata = currentMetadata;
        }

        this.metadataChangedSubject.OnNext(new WindowMetadataChange(windowId, key, oldValue, newValue));
        this.LogMetadataChanged(windowId, key);
    }

    /// <inheritdoc />
    public void RemoveMetadata(WindowId windowId, string key) => this.SetMetadata(windowId, key, value: null);

    /// <inheritdoc />
    public object? TryGetMetadataValue(WindowId windowId, string key)
        => this.windows.TryGetValue(windowId, out var window) && window.Metadata != null && window.Metadata.TryGetValue(key, out var value)
            ? value
            : null;

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.isDisposed = true;
        this.metadataChangedSubject.OnCompleted();
        this.metadataChangedSubject.Dispose();
        this.windowEventsSubject.OnCompleted();
        this.windowEventsSubject.Dispose();
        this.windows.Clear();
        this.routerContextCreatedSubscription?.Dispose();
        this.routerContextDestroyedSubscription?.Dispose();
    }

    private ManagedWindow CreateManagedWindow(
        Window window,
        WindowCategory category,
        WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        var context = new ManagedWindow
        {
            Id = window.AppWindow.Id,
            DispatcherQueue = this.dispatcherQueue,
            Window = window,
            Category = category,
            CreatedAt = DateTimeOffset.UtcNow,
            Decorations = decoration,
            Metadata = metadata,
        };

        // Log creation (using existing logger)
        this.LogWindowCreated(context);

        if (decoration is not { Menu.MenuProviderId: { } menuProviderId })
        {
            return context;
        }

        var provider = this.menuProviders.FirstOrDefault(p => string.Equals(
            p.ProviderId,
            menuProviderId,
            StringComparison.Ordinal));

        if (provider is not null)
        {
            context.SetMenuSource(provider.CreateMenuSource());
            this.LogMenuSourceCreated(menuProviderId, context);
        }
        else
        {
            this.LogMenuProviderNotFound(menuProviderId, context);
        }

        return context;
    }

    private void RegisterWindowEvents(ManagedWindow context)
    {
        var window = context.Window;
        var windowId = context.Id;

        window.Activated += (_, _) => this.OnWindowActivated(windowId);
        window.Closed += (_, _) => this.OnWindowClosed(windowId);

        window.AppWindow.Changed += AppWindowChangedHandler;
        context.Cleanup = () => window.AppWindow.Changed -= AppWindowChangedHandler;

        void AppWindowChangedHandler(AppWindow sender, AppWindowChangedEventArgs args)
        {
            if (args.DidPresenterChange)
            {
                this.OnPresenterChanged(windowId);
            }

            if (args.DidSizeChange || args.DidPositionChange)
            {
                this.OnBoundsChanged(windowId);
            }
        }
    }

    private void OnWindowActivated(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var currentContext))
        {
            return;
        }

        if (this.activeWindow?.Id == windowId)
        {
            return;
        }

        var updatedContext = currentContext.WithActivationState(true);
        _ = this.windows.TryUpdate(windowId, updatedContext, currentContext);

        if (this.activeWindow is { } previousActive && previousActive.Id != windowId)
        {
            var deactivatedContext = previousActive.WithActivationState(false);
            _ = this.windows.TryUpdate(deactivatedContext.Id, deactivatedContext, previousActive);
            this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Deactivated, deactivatedContext));
        }

        this.activeWindow = updatedContext;
        this.LogWindowActivated(windowId);
        this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Activated, updatedContext));
    }

    private void OnWindowClosed(WindowId windowId)
    {
        if (!this.windows.TryRemove(windowId, out var context))
        {
            return;
        }

        this.LogWindowClosed(context.Id);

        context.Cleanup?.Invoke();

        this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Closed, context));

        if (this.activeWindow?.Id == windowId)
        {
            this.activeWindow = null;
        }

        _ = this.WindowClosed?.Invoke(this, new WindowClosedEventArgs { WindowId = windowId });
    }

    private void OnPresenterChanged(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var context))
        {
            return;
        }

        if (context.Window.AppWindow.Presenter is not OverlappedPresenter presenter)
        {
            return;
        }

        var state = presenter.State;
        var bounds = context.CurrentBounds;

        // If transitioning to Restored state (from Max/Min), sync RestoredBounds with actual position
        // This handles cases where user bypasses WindowManager and restores directly via AppWindow API
        if (state == OverlappedPresenterState.Restored)
        {
            context.RestoredBounds = bounds;
        }

        this.LogPresenterStateChanged(windowId, state, state);
        _ = this.PresenterStateChanged?.Invoke(
            this,
            new PresenterStateChangeEventArgs(windowId, state, bounds));
    }

    private void OnBoundsChanged(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var context))
        {
            return;
        }

        var bounds = context.CurrentBounds;

        // Update RestoredBounds if window is in Restored state (user is adjusting their preferred size/position)
        if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter &&
            presenter.State == OverlappedPresenterState.Restored)
        {
            context.RestoredBounds = bounds;
        }

        _ = this.WindowBoundsChanged?.Invoke(this, new WindowBoundsChangedEventArgs(windowId, bounds));
    }

    private WindowDecorationOptions? ResolveDecoration(WindowId windowId, WindowCategory category)
    {
        if (this.decorationSettingsService is not null)
        {
            this.LogDecorationResolvedFromSettings(windowId, category);
            return this.decorationSettingsService.Settings.GetEffectiveDecoration(category);
        }

        this.LogNoDecorationResolved(windowId);
        return null;
    }

    private void OnRouterContextCreated(ContextCreated evt)
    {
        if (evt.Context?.NavigationTarget is not Window window)
        {
            return;
        }

        if (this.windows.Values.Any(wc => ReferenceEquals(wc.Window, window)))
        {
            this.LogRouterWindowAlreadyTracked(evt.Context.NavigationTargetKey.Name);
            return;
        }

        var targetName = evt.Context.NavigationTargetKey.Name;
        var windowCategory = evt.Context.NavigationTargetKey.IsMain ? WindowCategory.Main : WindowCategory.Secondary;
        this.LogTrackingRouterWindow(targetName);

        var metadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["RoutingTarget"] = targetName,
            ["CreatedByRouter"] = true,
        };

        _ = this.RegisterDecoratedWindowAsync(window, windowCategory, metadata)
            .ContinueWith(
                task =>
                {
                    if (task.Exception is not null)
                    {
                        this.LogRouterWindowTrackingFailed(task.Exception.InnerException ?? task.Exception, targetName);
                    }
                },
                TaskScheduler.Default);
    }

    private void OnRouterContextDestroyed(ContextDestroyed evt)
    {
        if (evt.Context?.NavigationTarget is Window)
        {
            this.LogRouterWindowDestroyed(evt.Context.NavigationTargetKey.Name);
        }
    }
}
