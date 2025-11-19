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
using Windows.Foundation;

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
    public event AsyncEventHandler<PresenterStateChangeEventArgs>? PresenterStateChanging;

    /// <inheritdoc />
    public event AsyncEventHandler<PresenterStateChangeEventArgs>? PresenterStateChanged;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowClosingEventArgs>? WindowClosing;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowClosedEventArgs>? WindowClosed;

    /// <inheritdoc />
    public event AsyncEventHandler<WindowBoundsChangedEventArgs>? WindowBoundsChanged;

    /// <inheritdoc />
    public ManagedWindow? ActiveWindow => this.activeWindow;

    /// <inheritdoc />
    public IReadOnlyCollection<ManagedWindow> OpenWindows => this.windows.Values.ToList().AsReadOnly();

    /// <inheritdoc />
    public IObservable<WindowLifecycleEvent> WindowEvents => this.windowEventsSubject.AsObservable();

    /// <inheritdoc />
    public IObservable<WindowMetadataChange> MetadataChanged => this.metadataChangedSubject.AsObservable();

    /// <inheritdoc />
    public async Task<ManagedWindow> RegisterWindowAsync(Window window, IReadOnlyDictionary<string, object>? metadata = null)
        => await this.RegisterDecoratedWindowAsync(window, WindowCategory.System, metadata).ConfigureAwait(false);

    /// <inheritdoc />
    public async Task<ManagedWindow> RegisterDecoratedWindowAsync(
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

                if (resolvedDecoration?.ChromeEnabled == true)
                {
                    window.ExtendsContentIntoTitleBar = true;
                }

                // Initialize state
                if (window.AppWindow.Presenter is OverlappedPresenter presenter)
                {
                    context.PresenterState = presenter.State;
                }

                context.CurrentBounds = new Windows.Graphics.RectInt32(
                    window.AppWindow.Position.X,
                    window.AppWindow.Position.Y,
                    window.AppWindow.Size.Width,
                    window.AppWindow.Size.Height);
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
        var args = new WindowClosingEventArgs();
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
    public ManagedWindow? GetWindow(WindowId windowId)
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

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            if (window.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                var oldState = window.PresenterState;
                const OverlappedPresenterState newState = OverlappedPresenterState.Minimized;
                var oldBounds = window.CurrentBounds;
                var proposedRestored = window.RestoredBounds;

                this.RaisePresenterStateChanging(window, newState);

                presenter.Minimize();

                this.EnsurePresenterStateChangedIfNeeded(window, oldState, newState, presenter, oldBounds);
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task MaximizeWindowAsync(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            if (window.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                var oldState = window.PresenterState;
                const OverlappedPresenterState newState = OverlappedPresenterState.Maximized;
                var oldBounds = window.CurrentBounds;
                var proposedRestored = window.RestoredBounds;

                this.RaisePresenterStateChanging(window, newState);

                presenter.Maximize();
                this.EnsurePresenterStateChangedIfNeeded(window, oldState, newState, presenter, oldBounds);
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public async Task RestoreWindowAsync(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return;
        }

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            if (window.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                var oldState = window.PresenterState;
                const OverlappedPresenterState newState = OverlappedPresenterState.Restored;
                var oldBounds = window.CurrentBounds;
                var proposedRestored = window.RestoredBounds;

                this.RaisePresenterStateChanging(window, newState);

                presenter.Restore();
                this.EnsurePresenterStateChangedIfNeeded(window, oldState, newState, presenter, oldBounds);
            }
        }).ConfigureAwait(false);
    }

    /// <inheritdoc />
    public Task SetMetadataAsync(WindowId windowId, string key, object? value)
    {
        if (!this.windows.TryGetValue(windowId, out var window))
        {
            return Task.CompletedTask;
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
                return Task.CompletedTask;
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

        return Task.CompletedTask;
    }

    /// <inheritdoc />
    public Task RemoveMetadataAsync(WindowId windowId, string key) => this.SetMetadataAsync(windowId, key, value: null);

    /// <inheritdoc />
    public Task<object?> TryGetMetadataValueAsync(WindowId windowId, string key)
        => this.windows.TryGetValue(windowId, out var window) && window.Metadata != null && window.Metadata.TryGetValue(key, out var value)
            ? Task.FromResult<object?>(value)
            : Task.FromResult<object?>(null);

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

    private static Windows.Graphics.RectInt32 GetCurrentBounds(ManagedWindow context)
        => new(
            context.Window.AppWindow.Position.X,
            context.Window.AppWindow.Position.Y,
            context.Window.AppWindow.Size.Width,
            context.Window.AppWindow.Size.Height);

    private ManagedWindow CreateManagedWindow(
        Window window,
        WindowCategory category,
        WindowDecorationOptions? decoration = null,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        var context = new ManagedWindow
        {
            Id = window.AppWindow.Id,
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

        _ = this.WindowClosed?.Invoke(this, new WindowClosedEventArgs());
    }

    private void OnPresenterChanged(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var context))
        {
            return;
        }

        if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter)
        {
            var oldState = context.PresenterState;
            var newState = presenter.State;
            if (oldState != newState)
            {
                if (oldState == OverlappedPresenterState.Restored &&
                    (newState == OverlappedPresenterState.Maximized || newState == OverlappedPresenterState.Minimized))
                {
                    context.RestoredBounds = context.CurrentBounds;
                }

                var oldBounds = GetCurrentBounds(context);
                var newBounds = GetCurrentBounds(context);

                context.PresenterState = newState;
                this.LogPresenterStateChanged(windowId, oldState, newState);
                _ = this.PresenterStateChanged?.Invoke(this, new PresenterStateChangeEventArgs(oldState, newState, oldBounds, newBounds, context.RestoredBounds));
            }
        }
    }

    private void RaisePresenterStateChanging(ManagedWindow context, OverlappedPresenterState newState)
    {
        var oldState = context.PresenterState;
        var oldBounds = context.CurrentBounds;
        var proposedRestored = context.RestoredBounds;
        _ = this.PresenterStateChanging?.Invoke(this, new PresenterStateChangeEventArgs(oldState, newState, oldBounds, newBounds: null, proposedRestored));
    }

    private void EnsurePresenterStateChangedIfNeeded(ManagedWindow context, OverlappedPresenterState oldState, OverlappedPresenterState expectedFinalState, OverlappedPresenter presenter, Windows.Graphics.RectInt32 oldBounds)
    {
        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                var ctx = context;
                if (this.windows.TryGetValue(ctx.Id, out var current) && current.PresenterState == oldState)
                {
                    var finalState = presenter.State;
                    if (finalState == expectedFinalState)
                    {
                        var newBounds = GetCurrentBounds(ctx);
                        current.PresenterState = finalState;
                        this.LogPresenterStateChanged(ctx.Id, oldState, finalState);
                        _ = this.PresenterStateChanged?.Invoke(this, new PresenterStateChangeEventArgs(oldState, finalState, oldBounds, newBounds, current.RestoredBounds));
                    }
                }
            }
            catch (Exception ex) when (ex is not OperationCanceledException)
            {
                this.LogActivateWindowFailed(ex, context.Id);
            }
        });
    }

    private void OnBoundsChanged(WindowId windowId)
    {
        if (!this.windows.TryGetValue(windowId, out var context))
        {
            return;
        }

        var oldBounds = context.CurrentBounds;
        var newBounds = new Windows.Graphics.RectInt32(
            context.Window.AppWindow.Position.X,
            context.Window.AppWindow.Position.Y,
            context.Window.AppWindow.Size.Width,
            context.Window.AppWindow.Size.Height);
        context.CurrentBounds = newBounds;
        _ = this.WindowBoundsChanged?.Invoke(this, new WindowBoundsChangedEventArgs(oldBounds, newBounds));
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
