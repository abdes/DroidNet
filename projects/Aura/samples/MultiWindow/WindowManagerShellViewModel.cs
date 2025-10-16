// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura.WindowManagement;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

/// <summary>
/// View model for the window manager demonstration shell.
/// </summary>
/// <remarks>
/// This view model provides controls for creating and managing multiple windows,
/// demonstrating Aura's multi-window capabilities including window lifecycle tracking,
/// theme synchronization, and different window types.
/// </remarks>
public sealed partial class WindowManagerShellViewModel : AbstractOutletContainer
{
    private readonly IWindowManagerService windowManager;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly IDisposable windowEventsSubscription;

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowManagerShellViewModel"/> class.
    /// </summary>
    /// <param name="router">The router used for navigation.</param>
    /// <param name="hostingContext">The hosting context containing dispatcher information.</param>
    /// <param name="windowManager">The window manager service.</param>
    public WindowManagerShellViewModel(
        IRouter router,
        HostingContext hostingContext,
        IWindowManagerService windowManager)
    {
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher);

        this.windowManager = windowManager;
        this.dispatcherQueue = hostingContext.Dispatcher;

        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));

        _ = router.Events.OfType<ActivationComplete>()
            .Take(1)
            .Subscribe(@event => this.Window = (Window)@event.Context.NavigationTarget);

        // Subscribe to window lifecycle events to update the window list
        this.windowEventsSubscription = this.windowManager.WindowEvents
            .Subscribe(evt =>
            {
                // Dispatch to UI thread
                _ = this.dispatcherQueue.TryEnqueue(() => this.OnWindowLifecycleEvent(evt));
            });

        // Initialize the window list
        this.UpdateWindowList();
    }

    /// <summary>
    /// Gets the window associated with this view model.
    /// </summary>
    public Window? Window { get; private set; }

    /// <summary>
    /// Gets the content view model for the primary outlet.
    /// </summary>
    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;

    /// <summary>
    /// Gets the collection of currently open windows for display.
    /// </summary>
    [ObservableProperty]
    public partial ObservableCollection<WindowInfo> OpenWindows { get; set; } = [];

    /// <summary>
    /// Gets the total count of open windows.
    /// </summary>
    [ObservableProperty]
    public partial int WindowCount { get; set; }

    /// <summary>
    /// Gets the currently active window information.
    /// </summary>
    [ObservableProperty]
    public partial string? ActiveWindowInfo { get; set; }

    /// <summary>
    /// Command to create a new main window.
    /// </summary>
    [RelayCommand]
    private async Task CreateMainWindowAsync()
    {
        try
        {
            _ = await this.windowManager.CreateWindowAsync<MainWindow>(
                windowType: "Main",
                title: $"Main Window {this.WindowCount + 1}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to create main window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to create a new tool window.
    /// </summary>
    [RelayCommand]
    private async Task CreateToolWindowAsync()
    {
        try
        {
            _ = await this.windowManager.CreateWindowAsync<ToolWindow>(
                windowType: "Tool",
                title: $"Tool Window {this.WindowCount + 1}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to create tool window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to create a new document window.
    /// </summary>
    [RelayCommand]
    private async Task CreateDocumentWindowAsync()
    {
        try
        {
            _ = await this.windowManager.CreateWindowAsync<DocumentWindow>(
                windowType: "Document",
                title: $"Document {this.WindowCount + 1}");
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to create document window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to close a specific window.
    /// </summary>
    /// <param name="windowInfo">The window information to close.</param>
    [RelayCommand]
    private async Task CloseWindowAsync(WindowInfo? windowInfo)
    {
        if (windowInfo is null)
        {
            return;
        }

        try
        {
            _ = await this.windowManager.CloseWindowAsync(windowInfo.Id);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to close window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to activate a specific window.
    /// </summary>
    /// <param name="windowInfo">The window information to activate.</param>
    [RelayCommand]
    private void ActivateWindow(WindowInfo? windowInfo)
    {
        if (windowInfo is null)
        {
            return;
        }

        try
        {
            this.windowManager.ActivateWindow(windowInfo.Id);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to activate window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to close all windows.
    /// </summary>
    [RelayCommand]
    private async Task CloseAllWindowsAsync()
    {
        try
        {
            await this.windowManager.CloseAllWindowsAsync();
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to close all windows: {ex.Message}");
        }
    }

    /// <inheritdoc/>
    protected override void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.windowEventsSubscription.Dispose();
        }

        this.isDisposed = true;
        base.Dispose(disposing);
    }

    /// <summary>
    /// Handles window lifecycle events to update the UI.
    /// </summary>
    /// <param name="evt">The window lifecycle event.</param>
    private void OnWindowLifecycleEvent(WindowLifecycleEvent evt)
    {
        this.UpdateWindowList();
        this.UpdateActiveWindowInfo();
    }

    /// <summary>
    /// Updates the list of open windows.
    /// </summary>
    private void UpdateWindowList()
    {
        this.OpenWindows.Clear();

        foreach (var context in this.windowManager.OpenWindows.OrderBy(w => w.CreatedAt))
        {
            this.OpenWindows.Add(new WindowInfo
            {
                Id = context.Id,
                Title = context.Title,
                WindowType = context.WindowType,
                IsActive = context.IsActive,
                CreatedAt = context.CreatedAt.ToLocalTime(),
            });
        }

        this.WindowCount = this.OpenWindows.Count;
    }

    /// <summary>
    /// Updates the active window information display.
    /// </summary>
    private void UpdateActiveWindowInfo()
    {
        var activeWindow = this.windowManager.ActiveWindow;
        this.ActiveWindowInfo = activeWindow is not null
            ? $"{activeWindow.Title} ({activeWindow.WindowType})"
            : "None";
    }
}

/// <summary>
/// Display model for window information.
/// </summary>
public sealed partial class WindowInfo : ObservableObject
{
    /// <summary>
    /// Gets or sets the window unique identifier.
    /// </summary>
    [ObservableProperty]
    public partial Guid Id { get; set; }

    /// <summary>
    /// Gets or sets the window title.
    /// </summary>
    [ObservableProperty]
    public partial string Title { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets the window type.
    /// </summary>
    [ObservableProperty]
    public partial string WindowType { get; set; } = string.Empty;

    /// <summary>
    /// Gets or sets a value indicating whether the window is active.
    /// </summary>
    [ObservableProperty]
    public partial bool IsActive { get; set; }

    /// <summary>
    /// Gets or sets the creation timestamp.
    /// </summary>
    [ObservableProperty]
    public partial DateTimeOffset CreatedAt { get; set; }
}
