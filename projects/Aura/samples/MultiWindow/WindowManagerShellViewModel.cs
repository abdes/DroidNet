// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Globalization;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using DroidNet.Aura;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DroidNet.Routing;
using DroidNet.Routing.Events;
using DroidNet.Routing.WinUI;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Media.Imaging;
using Windows.Graphics;

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
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel classes must be public")]
public sealed partial class WindowManagerShellViewModel : AbstractOutletContainer
{
    private readonly IWindowFactory windowFactory;
    private readonly IWindowManagerService windowManager;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly IDisposable windowEventsSubscription;

    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="WindowManagerShellViewModel"/> class.
    /// </summary>
    /// <param name="router">The router used for navigation.</param>
    /// <param name="hostingContext">The hosting context containing dispatcher information.</param>
    /// <param name="windowFactory">The window factory for creating new windows.</param>
    /// <param name="windowManager">The window manager service.</param>
    public WindowManagerShellViewModel(
        IRouter router,
        HostingContext hostingContext,
        IWindowFactory windowFactory,
        IWindowManagerService windowManager)
    {
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher, nameof(hostingContext));

        this.windowFactory = windowFactory;
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

    /// <summary>
    /// Gets or sets the selected backdrop for new windows.
    /// </summary>
    [ObservableProperty]
    public partial BackdropKind SelectedBackdrop { get; set; } = BackdropKind.Mica;

    /// <summary>
    /// Gets the available backdrop options.
    /// </summary>
    public IReadOnlyList<BackdropKind> AvailableBackdrops { get; } =
    [
        BackdropKind.None,
        BackdropKind.Mica,
        BackdropKind.MicaAlt,
        BackdropKind.Acrylic,
    ];

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
    /// Command to create a new tool window with the selected backdrop.
    /// </summary>
    [RelayCommand]
    private async Task CreateToolWindowAsync()
    {
        try
        {
            var window = await this.windowFactory.CreateDecoratedWindow<ToolWindow>(
                category: WindowCategory.Tool).ConfigureAwait(true);
            window.AppWindow.Title = string.Create(CultureInfo.InvariantCulture, $"Tool Window {window.AppWindow.Id.Value}");
            window.Activate();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to create tool window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to create a new document window with the selected backdrop.
    /// </summary>
    [RelayCommand]
    private async Task CreateDocumentWindowAsync()
    {
        try
        {
            var window = await this.windowFactory.CreateDecoratedWindow<DocumentWindow>(
                category: WindowCategory.Document).ConfigureAwait(true);
            window.AppWindow.Title = string.Create(CultureInfo.InvariantCulture, $"Document Window {window.AppWindow.Id.Value}");
            window.Activate();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to create document window: {ex.Message}");
        }
    }

    /// <summary>
    /// Command to create a new frameless window with the DroidNet logo.
    /// </summary>
    [RelayCommand]
    private async Task CreateFramelessWindowAsync()
    {
        try
        {
            var window = await this.windowFactory.CreateDecoratedWindow<Window>(
                category: WindowCategory.Frameless).ConfigureAwait(true);

            window.AppWindow.Resize(new SizeInt32(400, 400));

            // Set content
            var grid = new Grid
            {
                Background = new SolidColorBrush(Colors.Transparent),
                Children =
                {
                    new Image
                    {
                        Stretch = Stretch.Uniform,
                        Source = new BitmapImage(new Uri("ms-appx:///DroidNet.Aura/Assets/DroidNet.png")),
                    },
                },
            };

            window.Content = grid;
            window.Activate();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to create frameless window: {ex.Message}");
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
            Debug.WriteLine($"Failed to close window: {ex.Message}");
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
            Debug.WriteLine($"Failed to activate window: {ex.Message}");
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
            Debug.WriteLine($"Failed to close all windows: {ex.Message}");
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
                Title = context.Window.Title,
                Category = context.Category,
                IsActive = context.IsActive,
                CreatedAt = context.CreatedAt.ToLocalTime(),
                Backdrop = context.Decorations?.Backdrop ?? BackdropKind.None,
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
            ? string.Create(CultureInfo.InvariantCulture, $"{activeWindow.Window.Title} ({activeWindow.Category})")
            : "None";
    }
}
