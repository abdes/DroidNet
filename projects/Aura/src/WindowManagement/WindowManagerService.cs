// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.ComponentModel;
using System.Linq;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Hosting.WinUI;
using Microsoft.Extensions.Logging;
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
    private readonly IWindowFactory windowFactory;
    private readonly IAppThemeModeService? themeModeService;
    private readonly AppearanceSettingsService? appearanceSettings;
    private readonly ILogger<WindowManagerService> logger;
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
    /// <param name="logger">Logger for diagnostic output.</param>
    /// <param name="themeModeService">Optional theme service for applying themes to new windows.</param>
    /// <param name="appearanceSettings">Optional appearance settings for theme synchronization.</param>
    public WindowManagerService(
        IWindowFactory windowFactory,
        HostingContext hostingContext,
        ILogger<WindowManagerService> logger,
        IAppThemeModeService? themeModeService = null,
        AppearanceSettingsService? appearanceSettings = null)
    {
        ArgumentNullException.ThrowIfNull(windowFactory);
        ArgumentNullException.ThrowIfNull(hostingContext);
        ArgumentNullException.ThrowIfNull(logger);
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher);

        this.windowFactory = windowFactory;
        this.dispatcherQueue = hostingContext.Dispatcher;
        this.logger = logger;
        this.themeModeService = themeModeService;
        this.appearanceSettings = appearanceSettings;

        if (this.themeModeService is not null && this.appearanceSettings is not null)
        {
            this.appearanceSettings.PropertyChanged += this.AppearanceSettings_OnPropertyChanged;
        }

        this.logger.LogInformation("WindowManagerService initialized");
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

        // Create window on UI thread
        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                this.logger.LogDebug("Creating window of type {WindowType}", typeof(TWindow).Name);

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

                this.logger.LogInformation(
                    "Window created: ID={WindowId}, Type={WindowType}, Title={Title}",
                    context.Id,
                    windowType,
                    context.Title);

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
                this.logger.LogError(ex, "Failed to create window of type {WindowType}", typeof(TWindow).Name);
                throw new InvalidOperationException($"Failed to create window of type {typeof(TWindow).Name}", ex);
            }
        });

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
                this.logger.LogDebug("Creating window of type {WindowTypeName}", windowTypeName);

                var window = this.windowFactory.CreateWindow(windowTypeName);
                context = WindowContext.Create(window, windowType, title, metadata);

                this.ApplyTheme(context);

                this.RegisterWindowEvents(context);

                if (!this.windows.TryAdd(context.Id, context))
                {
                    throw new InvalidOperationException($"Failed to register window with ID {context.Id}");
                }

                this.logger.LogInformation(
                    "Window created: ID={WindowId}, Type={WindowType}, Title={Title}",
                    context.Id,
                    windowType,
                    context.Title);

                this.PublishEvent(WindowLifecycleEventType.Created, context);

                window.Activate();

                if (activateWindow)
                {
                    this.ActivateWindow(context);
                }
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to create window of type {WindowTypeName}", windowTypeName);
                throw new InvalidOperationException($"Failed to create window of type {windowTypeName}", ex);
            }
        });

        return context ?? throw new InvalidOperationException("Window creation failed");
    }

    /// <inheritdoc/>
    public async Task<bool> CloseWindowAsync(WindowContext context)
    {
        ArgumentNullException.ThrowIfNull(context);
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        return await this.CloseWindowAsync(context.Id);
    }

    /// <inheritdoc/>
    public async Task<bool> CloseWindowAsync(Guid windowId)
    {
        ObjectDisposedException.ThrowIf(this.isDisposed, this);

        if (!this.windows.TryGetValue(windowId, out var context))
        {
            this.logger.LogWarning("Attempted to close non-existent window: {WindowId}", windowId);
            return false;
        }

        var result = false;

        await this.dispatcherQueue.EnqueueAsync(() =>
        {
            try
            {
                this.logger.LogDebug("Closing window: {WindowId}", windowId);
                context.Window.Close();
                result = true;
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Error closing window: {WindowId}", windowId);
            }
        });

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

        if (!this.windows.TryGetValue(windowId, out var context))
        {
            this.logger.LogWarning("Attempted to activate non-existent window: {WindowId}", windowId);
            return;
        }

        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            try
            {
                context.Window.Activate();

                // Update active window tracking
                if (this.activeWindow is not null)
                {
                    var previousActive = this.activeWindow.WithActivationState(false);
                    _ = this.windows.TryUpdate(previousActive.Id, previousActive, this.activeWindow);
                    this.PublishEvent(WindowLifecycleEventType.Deactivated, previousActive);
                }

                var updatedContext = context.WithActivationState(true);
                _ = this.windows.TryUpdate(windowId, updatedContext, context);
                this.activeWindow = updatedContext;

                this.PublishEvent(WindowLifecycleEventType.Activated, updatedContext);

                this.logger.LogDebug("Window activated: {WindowId}", windowId);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Error activating window: {WindowId}", windowId);
            }
        });
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

        this.logger.LogInformation("Closing all windows ({Count} total)", this.windows.Count);

        var closeTasks = this.windows.Values.Select(context => this.CloseWindowAsync(context.Id));
        _ = await Task.WhenAll(closeTasks);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        if (this.isDisposed)
        {
            return;
        }

        this.logger.LogInformation("Disposing WindowManagerService");

        this.windowEventsSubject.OnCompleted();
        this.windowEventsSubject.Dispose();
        this.windows.Clear();

        if (this.appearanceSettings is not null)
        {
            this.appearanceSettings.PropertyChanged -= this.AppearanceSettings_OnPropertyChanged;
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

        window.Activated += (_, _) => this.OnWindowActivated(context);
        window.Closed += (_, _) => this.OnWindowClosed(context);
    }

    /// <summary>
    /// Handles window activation.
    /// </summary>
    /// <param name="context">The activated window context.</param>
    private void OnWindowActivated(WindowContext context)
    {
        if (this.activeWindow?.Id == context.Id)
        {
            // Already active
            return;
        }

        // Deactivate previous window
        if (this.activeWindow is not null)
        {
            var previousActive = this.activeWindow.WithActivationState(false);
            _ = this.windows.TryUpdate(previousActive.Id, previousActive, this.activeWindow);
            this.PublishEvent(WindowLifecycleEventType.Deactivated, previousActive);
        }

        // Activate current window
        var updatedContext = context.WithActivationState(true);
        _ = this.windows.TryUpdate(context.Id, updatedContext, context);
        this.activeWindow = updatedContext;

        this.PublishEvent(WindowLifecycleEventType.Activated, updatedContext);
    }

    /// <summary>
    /// Handles window closure.
    /// </summary>
    /// <param name="context">The closed window context.</param>
    private void OnWindowClosed(WindowContext context)
    {
        if (this.windows.TryRemove(context.Id, out _))
        {
            this.logger.LogInformation("Window closed: ID={WindowId}, Title={Title}", context.Id, context.Title);

            if (this.activeWindow?.Id == context.Id)
            {
                this.activeWindow = null;
            }

            this.PublishEvent(WindowLifecycleEventType.Closed, context);
        }
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
            try
            {
                this.themeModeService.ApplyThemeMode(context.Window, currentTheme);
            }
            catch (Exception ex)
            {
                this.logger.LogError(ex, "Failed to apply theme to window {WindowId}", context.Id);
            }
        }

        if (this.dispatcherQueue.HasThreadAccess)
        {
            ApplyThemeCore();
            return;
        }

        if (!this.dispatcherQueue.TryEnqueue(ApplyThemeCore))
        {
            this.logger.LogWarning("Failed to enqueue theme application for window {WindowId}", context.Id);
        }
    }

    /// <summary>
    /// Returns a snapshot of the current window contexts.
    /// </summary>
    /// <returns>An immutable list of tracked window contexts.</returns>
    private WindowContext[] GetWindowSnapshot()
    {
        return this.windows.Values.ToArray();
    }
}
