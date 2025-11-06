// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Settings;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.Windowing;

/// <summary>
///     Coordinates Aura-managed windows within a WinUI 3 host.
/// </summary>
/// <remarks>
///     The service tracks windows supplied by the application, decorates them using configured
///     settings, applies appearance updates, and publishes lifecycle events. While it wires up
///     activation/closure handlers and offers helpers for closing or requesting activation, showing
///     the window remains an application decision.
/// </remarks>
public sealed partial class WindowManagerService : IWindowManagerService
{
    private readonly ILogger<WindowManagerService> logger;
    private readonly IWindowContextFactory windowContextFactory;
    private readonly ISettingsService<IWindowDecorationSettings>? decorationSettingsService;
    private readonly DispatcherQueue dispatcherQueue;

    private readonly ConcurrentDictionary<WindowId, WindowContext> windows = new();
    private readonly Subject<WindowLifecycleEvent> windowEventsSubject = new();
    private readonly IDisposable? routerContextCreatedSubscription;
    private readonly IDisposable? routerContextDestroyedSubscription;

    private WindowContext? activeWindow;
    private bool isDisposed;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowManagerService"/> class.
    /// </summary>
    /// <param name="windowContextFactory">Factory responsible for creating <see cref="WindowContext"/> instances.</param>
    /// <param name="hostingContext">Provides access to the application dispatcher and related WinUI services.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a category logger.</param>
    /// <param name="router">Optional router used to auto-register windows created via navigation.</param>
    /// <param name="decorationSettingsService">Optional service resolving decoration metadata by window category.</param>
    public WindowManagerService(
        IWindowContextFactory windowContextFactory,
        HostingContext hostingContext,
        ILoggerFactory? loggerFactory = null,
        IRouter? router = null,
        ISettingsService<IWindowDecorationSettings>? decorationSettingsService = null)
    {
        ArgumentNullException.ThrowIfNull(windowContextFactory);
        ArgumentNullException.ThrowIfNull(hostingContext);
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher, nameof(hostingContext));

        this.logger = loggerFactory?.CreateLogger<WindowManagerService>() ?? NullLogger<WindowManagerService>.Instance;
        this.windowContextFactory = windowContextFactory;
        this.dispatcherQueue = hostingContext.Dispatcher;
        this.decorationSettingsService = decorationSettingsService;

        // Integrate with router if available to track router-created windows
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
    public IObservable<WindowLifecycleEvent> WindowEvents => this.windowEventsSubject.AsObservable();

    /// <inheritdoc />
    public WindowContext? ActiveWindow => this.activeWindow;

    /// <inheritdoc />
    public IReadOnlyCollection<WindowContext> OpenWindows => this.windows.Values.ToList().AsReadOnly();

    /// <inheritdoc />
    public async Task<WindowContext> RegisterWindowAsync(Window window, IReadOnlyDictionary<string, object>? metadata = null)
        => await this.RegisterDecoratedWindowAsync(
            window,
            WindowCategory.System,
            metadata).ConfigureAwait(false);

    /// <inheritdoc />
    public async Task<WindowContext> RegisterDecoratedWindowAsync(
        Window window,
        WindowCategory category,
        IReadOnlyDictionary<string, object>? metadata = null)
    {
        ArgumentNullException.ThrowIfNull(window);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        // Check if window is already registered
        if (this.windows.Values.Any(wc => ReferenceEquals(wc.Window, window)))
        {
            throw new InvalidOperationException("Window is already registered with the window manager");
        }

        WindowContext? context = null;

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                // Resolve decoration using the configured priority system.
                var resolvedDecoration = this.ResolveDecoration(window.AppWindow.Id, category);

                context = this.windowContextFactory.Create(window, category, resolvedDecoration, metadata);

                // Register window events
                this.RegisterWindowEvents(context);

                // Add to collection
                if (!this.windows.TryAdd(context.Id, context))
                {
                    throw new InvalidOperationException($"Failed to register window with ID {context.Id.Value}");
                }

                this.LogWindowRegistered(context.Id, category);

                // Publish event
                this.PublishEvent(WindowLifecycleEventType.Created, context);

                // Ensure chrome settings are in place before the application decides to show the window.
                // ExtendsContentIntoTitleBar must be set before window.Show() for backdrop to work.
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
    public async Task<bool> CloseWindowAsync(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        return await this.CloseWindowAsync(context.Id).ConfigureAwait(true);
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

        var result = false;

#pragma warning disable CA1031 // Window closing should not propagate to callers
        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                this.LogClosingWindow(windowId);
                context.Window.Close();
                result = true;
            }
            catch (Exception ex)
            {
                this.LogCloseWindowFailed(ex, windowId);
            }
        }).ConfigureAwait(true);
#pragma warning restore CA1031 // Window closing should not propagate to callers

        return result;
    }

    /// <inheritdoc />
    public void ActivateWindow(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        this.ActivateWindow(context.Id);
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

#pragma warning disable CA1031 // Window activation errors should be logged, not thrown
        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                if (!this.windows.TryGetValue(windowId, out var targetContext))
                {
                    this.LogActivateMissingWindow(windowId);
                    return;
                }

                targetContext.Window.Activate();
            }
            catch (Exception ex)
            {
                this.LogActivateWindowFailed(ex, windowId);
            }
        });
#pragma warning restore CA1031 // Window activation errors should be logged, not thrown
    }

    /// <inheritdoc />
    public WindowContext? GetWindow(WindowId windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        return this.windows.TryGetValue(windowId, out var context) ? context : null;
    }

    /// <inheritdoc />
    public IReadOnlyCollection<WindowContext> GetWindowsByCategory(WindowCategory category)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        return this.windows.Values
            .Where(w => w.Category.Equals(category))
            .ToList()
            .AsReadOnly();
    }

    /// <inheritdoc />
    public async Task CloseAllWindowsAsync()
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        this.LogClosingAllWindows(this.windows.Count);

        // Capture immutable snapshot of window IDs to ensure deterministic closure
        // even as the collection mutates during concurrent window removal
        var windowIds = this.windows.Keys.ToArray();
        var closeTasks = windowIds.Select(this.CloseWindowAsync);
        _ = await Task.WhenAll(closeTasks).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.LogServiceDisposing();

        this.windowEventsSubject.OnCompleted();
        this.windowEventsSubject.Dispose();
        this.windows.Clear();

        this.routerContextCreatedSubscription?.Dispose();
        this.routerContextDestroyedSubscription?.Dispose();

        this.isDisposed = true;
    }

    /// <summary>
    ///     Handles router context creation events by registering the routed window.
    /// </summary>
    /// <param name="evt">Details about the navigation context that was created.</param>
    private void OnRouterContextCreated(ContextCreated evt)
    {
        // Only handle window-based navigation contexts
        if (evt.Context?.NavigationTarget is not Window window)
        {
            return;
        }

        // Check if already tracked
        if (this.windows.Values.Any(wc => ReferenceEquals(wc.Window, window)))
        {
            this.LogRouterWindowAlreadyTracked(evt.Context.NavigationTargetKey.Name);
            return;
        }

        var targetName = evt.Context.NavigationTargetKey.Name;

        // TODO: revise the router target type to ensure consistency with window management
        var windowCategory = evt.Context.NavigationTargetKey.IsMain ? WindowCategory.Main : WindowCategory.Secondary;

        this.LogTrackingRouterWindow(targetName);

        // Register the window - RegisterWindowAsync will handle UI thread marshalling
        var metadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["RoutingTarget"] = targetName,
            ["CreatedByRouter"] = true,
        };

#pragma warning disable CA1031 // Router window tracking failures should be logged, not thrown
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
#pragma warning restore CA1031
    }

    /// <summary>
    ///     Handles router context destruction events.
    /// </summary>
    /// <param name="evt">Details about the navigation context that was destroyed.</param>
    private void OnRouterContextDestroyed(ContextDestroyed evt)
    {
        // The window.Closed event handler will have already removed it from tracking
        if (evt.Context?.NavigationTarget is Window)
        {
            this.LogRouterWindowDestroyed(evt.Context.NavigationTargetKey.Name);
        }
    }

    /// <summary>
    ///     Hooks up activation and closure handlers for a newly registered window.
    /// </summary>
    /// <param name="context">The window context to watch.</param>
    private void RegisterWindowEvents(WindowContext context)
    {
        var window = context.Window;
        var windowId = context.Id;

        window.Activated += (_, _) => this.OnWindowActivated(windowId);
        window.Closed += (_, _) => this.OnWindowClosed(windowId);
    }

    /// <summary>
    ///     Updates tracked state and publishes notifications when a window becomes active.
    /// </summary>
    /// <param name="windowId">The identifier of the activated window.</param>
    private void OnWindowActivated(WindowId windowId)
    {
        while (true)
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

            if (!this.windows.TryUpdate(windowId, updatedContext, currentContext))
            {
                // Retry if another thread modified the context concurrently.
                continue;
            }

            // Deactivate previously active window if it differs from the current one.
            if (this.activeWindow is { } previousActive && previousActive.Id != windowId)
            {
                var deactivatedContext = previousActive.WithActivationState(false);
                if (this.windows.TryUpdate(deactivatedContext.Id, deactivatedContext, previousActive))
                {
                    this.PublishEvent(WindowLifecycleEventType.Deactivated, deactivatedContext);
                }
            }

            this.activeWindow = updatedContext;
            this.PublishEvent(WindowLifecycleEventType.Activated, updatedContext);
            return;
        }
    }

    /// <summary>
    ///     Removes a closed window from the tracking dictionary and emits lifecycle events.
    /// </summary>
    /// <param name="windowId">The identifier of the closed window.</param>
    private void OnWindowClosed(WindowId windowId)
    {
        if (!this.windows.TryRemove(windowId, out var context))
        {
            return;
        }

        this.LogWindowClosed(context.Id);

        if (this.activeWindow?.Id == windowId)
        {
            this.activeWindow = null;
        }

        this.PublishEvent(WindowLifecycleEventType.Closed, context);
    }

    /// <summary>
    ///     Raises the specified window lifecycle event for observers.
    /// </summary>
    /// <param name="eventType">The event to publish.</param>
    /// <param name="context">The window context tied to the event.</param>
    private void PublishEvent(WindowLifecycleEventType eventType, WindowContext context)
    {
        var lifecycleEvent = WindowLifecycleEvent.Create(eventType, context);
        this.windowEventsSubject.OnNext(lifecycleEvent);
    }

    /// <summary>
    ///     Resolves decoration instructions using the configured priority system.
    /// </summary>
    /// <param name="windowId">The window identifier used for logging purposes.</param>
    /// <param name="category">The window category to look up.</param>
    /// <returns>The resolved <see cref="Decoration.WindowDecorationOptions"/>, or null when no decoration applies.</returns>
    private Decoration.WindowDecorationOptions? ResolveDecoration(
        WindowId windowId,
        WindowCategory category)
    {
        // Tier 2: Settings registry lookup (includes persisted overrides and code-defined defaults)
        if (this.decorationSettingsService is not null)
        {
            // TODO: listen for changes on the decoration settings service and update the context accordingly as long as it is not disposed
            this.LogDecorationResolvedFromSettings(windowId, category);
            return this.decorationSettingsService.Settings.GetEffectiveDecoration(category);
        }

        // Tier 3: No decoration available
        this.LogNoDecorationResolved(windowId);
        return null;
    }

    /// <summary>
    ///     Returns an immutable snapshot of the current window contexts.
    /// </summary>
    /// <returns>An immutable list of tracked window contexts.</returns>
    private WindowContext[] GetWindowSnapshot() => [.. this.windows.Values];
}
