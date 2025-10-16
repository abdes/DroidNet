// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.ComponentModel;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using CommunityToolkit.WinUI;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Aura.WindowManagement;

/// <summary>
/// Manages the lifecycle of multiple windows in a WinUI 3 application.
/// </summary>
/// <remarks>
/// This service provides centralized management of window creation, activation, and closure.
/// It publishes window lifecycle events through a reactive stream and ensures proper cleanup
/// of window resources. The service is thread-safe and integrates with the Aura theme system.
/// </remarks>
public sealed partial class WindowManagerService : IWindowManagerService
{
    private readonly ILogger<WindowManagerService> logger;
    private readonly IWindowFactory windowFactory;
    private readonly IAppThemeModeService? themeModeService;
    private readonly IAppearanceSettings? appearanceSettings;
    private readonly ISettingsService<IAppearanceSettings>? appearanceSettingsService;
    private readonly DispatcherQueue dispatcherQueue;

    private readonly ConcurrentDictionary<Guid, WindowContext> windows = new();
    private readonly Subject<WindowLifecycleEvent> windowEventsSubject = new();

    private WindowContext? activeWindow;
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowManagerService"/> class.
    /// </summary>
    /// <param name="windowFactory">Factory for creating window instances.</param>
    /// <param name="hostingContext">The hosting context containing the UI dispatcher queue.</param>
    /// <param name="loggerFactory">Optional logger factory used to create a service logger.</param>
    /// <param name="themeModeService">Optional theme service for applying themes to new windows.</param>
    /// <param name="appearanceSettings">Optional appearance settings for accessing theme properties.</param>
    /// <param name="appearanceSettingsService">Optional settings service for PropertyChanged notifications.</param>
    public WindowManagerService(
        IWindowFactory windowFactory,
        HostingContext hostingContext,
        ILoggerFactory? loggerFactory = null,
        IAppThemeModeService? themeModeService = null,
        IAppearanceSettings? appearanceSettings = null,
        ISettingsService<IAppearanceSettings>? appearanceSettingsService = null)
    {
        ArgumentNullException.ThrowIfNull(windowFactory);
        ArgumentNullException.ThrowIfNull(hostingContext);
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher);

        this.logger = loggerFactory?.CreateLogger<WindowManagerService>() ?? NullLogger<WindowManagerService>.Instance;
        this.windowFactory = windowFactory;
        this.dispatcherQueue = hostingContext.Dispatcher;
        this.themeModeService = themeModeService;
        this.appearanceSettings = appearanceSettings;
        this.appearanceSettingsService = appearanceSettingsService;

        if (this.themeModeService is not null && this.appearanceSettingsService is not null)
        {
            this.appearanceSettingsService.PropertyChanged += this.AppearanceSettings_OnPropertyChanged;
        }

        this.LogServiceInitialized();
    }

    /// <inheritdoc/>
    public IObservable<WindowLifecycleEvent> WindowEvents => this.windowEventsSubject.AsObservable();

    /// <inheritdoc/>
    public WindowContext? ActiveWindow => this.activeWindow;

    /// <inheritdoc/>
    public IReadOnlyCollection<WindowContext> OpenWindows => this.windows.Values.ToList().AsReadOnly();

    /// <inheritdoc/>
    public async Task<WindowContext> CreateWindowAsync<TWindow>(
        string windowType = "Main",
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true)
        where TWindow : Window
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        WindowContext? context = null;
        var requestedWindowType = typeof(TWindow).Name;

        // Create window on UI thread
        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                this.LogCreatingWindow(requestedWindowType);

                var window = this.windowFactory.CreateWindow<TWindow>();
                context = WindowContext.Create(window, windowType, title, metadata);

                // Apply theme if services are available
                this.ApplyTheme(context);

                // Register window events
                this.RegisterWindowEvents(context);

                // Add to collection
                if (!this.windows.TryAdd(context.Id, context))
                {
                    throw new InvalidOperationException($"Failed to register window with ID {context.Id}");
                }

                this.LogWindowCreated(context.Id, windowType, context.Title);

                // Publish event
                this.PublishEvent(WindowLifecycleEventType.Created, context);

                // Show and optionally activate window
                window.Activate();

                if (activateWindow)
                {
                    this.ActivateWindow(context);
                }
            }
            catch (Exception ex)
            {
                this.LogWindowCreateFailed(ex, requestedWindowType);
                throw new InvalidOperationException($"Failed to create window of type {requestedWindowType}", ex);
            }
        }).ConfigureAwait(true);

        return context ?? throw new InvalidOperationException("Window creation failed");
    }

    /// <inheritdoc/>
    public async Task<WindowContext> CreateWindowAsync(
        string windowTypeName,
        string windowType = "Main",
        string? title = null,
        IReadOnlyDictionary<string, object>? metadata = null,
        bool activateWindow = true)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        ArgumentException.ThrowIfNullOrWhiteSpace(windowTypeName);

        WindowContext? context = null;

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                this.LogCreatingWindow(windowTypeName);

                var window = this.windowFactory.CreateWindow(windowTypeName);
                context = WindowContext.Create(window, windowType, title, metadata);

                this.ApplyTheme(context);

                this.RegisterWindowEvents(context);

                if (!this.windows.TryAdd(context.Id, context))
                {
                    throw new InvalidOperationException($"Failed to register window with ID {context.Id}");
                }

                this.LogWindowCreated(context.Id, windowType, context.Title);

                this.PublishEvent(WindowLifecycleEventType.Created, context);

                window.Activate();

                if (activateWindow)
                {
                    this.ActivateWindow(context);
                }
            }
            catch (Exception ex)
            {
                this.LogWindowCreateFailed(ex, windowTypeName);
                throw new InvalidOperationException($"Failed to create window of type {windowTypeName}", ex);
            }
        }).ConfigureAwait(true);

        return context ?? throw new InvalidOperationException("Window creation failed");
    }

    /// <inheritdoc/>
    public async Task<bool> CloseWindowAsync(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        return await this.CloseWindowAsync(context.Id).ConfigureAwait(true);
    }

    /// <inheritdoc/>
    public async Task<bool> CloseWindowAsync(Guid windowId)
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

    /// <inheritdoc/>
    public void ActivateWindow(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        this.ActivateWindow(context.Id);
    }

    /// <inheritdoc/>
    public void ActivateWindow(Guid windowId)
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

    /// <inheritdoc/>
    public WindowContext? GetWindow(Guid windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        return this.windows.TryGetValue(windowId, out var context) ? context : null;
    }

    /// <inheritdoc/>
    public IReadOnlyCollection<WindowContext> GetWindowsByType(string windowType)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);
        ArgumentException.ThrowIfNullOrWhiteSpace(windowType);

        return this.windows.Values
            .Where(w => string.Equals(w.WindowType, windowType, StringComparison.OrdinalIgnoreCase))
            .ToList()
            .AsReadOnly();
    }

    /// <inheritdoc/>
    public async Task CloseAllWindowsAsync()
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        this.LogClosingAllWindows(this.windows.Count);

        // Capture immutable snapshot of window IDs to ensure deterministic closure
        // even as the collection mutates during concurrent window removal
        var windowIds = this.windows.Keys.ToArray();
        var closeTasks = windowIds.Select(id => this.CloseWindowAsync(id));
        _ = await Task.WhenAll(closeTasks).ConfigureAwait(true);
    }

    /// <inheritdoc/>
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

        if (this.appearanceSettingsService is not null)
        {
            this.appearanceSettingsService.PropertyChanged -= this.AppearanceSettings_OnPropertyChanged;
        }

        this.isDisposed = true;
    }

    /// <summary>
    /// Registers event handlers for window lifecycle events.
    /// </summary>
    /// <param name="context">The window context to register events for.</param>
    private void RegisterWindowEvents(WindowContext context)
    {
        var window = context.Window;
        var windowId = context.Id;

        window.Activated += (_, _) => this.OnWindowActivated(windowId);
        window.Closed += (_, _) => this.OnWindowClosed(windowId);
    }

    /// <summary>
    /// Handles window activation.
    /// </summary>
    /// <param name="windowId">The identifier of the activated window.</param>
    private void OnWindowActivated(Guid windowId)
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
    /// Handles window closure.
    /// </summary>
    /// <param name="windowId">The identifier of the closed window.</param>
    private void OnWindowClosed(Guid windowId)
    {
        if (!this.windows.TryRemove(windowId, out var context))
        {
            return;
        }

        this.LogWindowClosed(context.Id, context.Title ?? string.Empty);

        if (this.activeWindow?.Id == windowId)
        {
            this.activeWindow = null;
        }

        this.PublishEvent(WindowLifecycleEventType.Closed, context);
    }

    /// <summary>
    /// Publishes a window lifecycle event to subscribers.
    /// </summary>
    /// <param name="eventType">The type of event.</param>
    /// <param name="context">The window context.</param>
    private void PublishEvent(WindowLifecycleEventType eventType, WindowContext context)
    {
        var lifecycleEvent = WindowLifecycleEvent.Create(eventType, context);
        this.windowEventsSubject.OnNext(lifecycleEvent);
    }

    /// <summary>
    /// Handles appearance settings changes related to theme updates.
    /// </summary>
    /// <param name="sender">The event sender.</param>
    /// <param name="args">Event arguments.</param>
    private void AppearanceSettings_OnPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (!string.Equals(args.PropertyName, nameof(IAppearanceSettings.AppThemeMode), StringComparison.Ordinal))
        {
            return;
        }

        foreach (var context in this.GetWindowSnapshot())
        {
            this.ApplyTheme(context);
        }
    }

    /// <summary>
    /// Applies the active appearance theme to the specified window context.
    /// </summary>
    /// <param name="context">The window context that should receive the theme.</param>
    private void ApplyTheme(WindowContext? context)
    {
        if (context is null || this.themeModeService is null || this.appearanceSettings is null)
        {
            return;
        }

        var currentTheme = this.appearanceSettings.AppThemeMode;

        void ApplyThemeCore()
        {
#pragma warning disable CA1031 // Theme application failures should be logged, not thrown
            try
            {
                this.themeModeService.ApplyThemeMode(context.Window, currentTheme);
            }
            catch (Exception ex)
            {
                this.LogThemeApplyFailed(ex, context.Id);
            }
#pragma warning restore CA1031 // Theme application failures should be logged, not thrown
        }

        if (this.dispatcherQueue.HasThreadAccess)
        {
            ApplyThemeCore();
            return;
        }

        if (!this.dispatcherQueue.TryEnqueue(ApplyThemeCore))
        {
            this.LogThemeApplyEnqueueFailed(context.Id);
        }
    }

    /// <summary>
    /// Returns a snapshot of the current window contexts.
    /// </summary>
    /// <returns>An immutable list of tracked window contexts.</returns>
    private WindowContext[] GetWindowSnapshot() => [.. this.windows.Values];
}
