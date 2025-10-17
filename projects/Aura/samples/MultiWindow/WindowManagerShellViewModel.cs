// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Globalization;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura;
using DroidNet.Aura.WindowManagement;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;

namespace DroidNet.Samples.Aura.MultiWindow;

#pragma warning disable CA1031 // Do not catch general exception types

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
                _ = this.dispatcherQueue.TryEnqueue(() => this.OnWindowLifecycleEvent(evt)));

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
    /// Command to create a new tool window.
    /// </summary>
    [RelayCommand]
    private async Task CreateToolWindowAsync()
    {
        try
        {
            _ = await this.windowManager.CreateWindowAsync<ToolWindow>(
                category: WindowCategory.Tool,
                title: string.Create(CultureInfo.InvariantCulture, $"Tool Window {this.WindowCount + 1}"))
                .ConfigureAwait(true);
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
                category: WindowCategory.Document,
                title: string.Create(CultureInfo.InvariantCulture, $"Document {this.WindowCount + 1}"))
                .ConfigureAwait(true);
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
            _ = await this.windowManager.CloseWindowAsync(windowInfo.Id).ConfigureAwait(true);
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
            await this.windowManager.CloseAllWindowsAsync().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"Failed to close all windows: {ex.Message}");
        }
    }

    /// <summary>
    /// Handles window lifecycle events to update the UI.
    /// </summary>
    /// <param name="evt">The window lifecycle event.</param>
    private void OnWindowLifecycleEvent(WindowLifecycleEvent evt)
    {
        _ = evt; // unused

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
                Category = context.Category,
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
            ? string.Create(CultureInfo.InvariantCulture, $"{activeWindow.Title} ({activeWindow.Category})")
            : "None";
    }
}
