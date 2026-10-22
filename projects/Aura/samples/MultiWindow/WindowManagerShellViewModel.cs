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
using DroidNet.Documents;
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
///     View model for the window manager demonstration shell.
/// </summary>
/// <remarks>
///     This view model provides controls for creating and managing multiple windows, demonstrating
///     Aura's multi-window capabilities including window lifecycle tracking, theme synchronization,
///     and different window types.
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "ViewModel classes must be public")]
public sealed partial class WindowManagerShellViewModel : AbstractOutletContainer
{
    private readonly IWindowFactory windowFactory;
    private readonly IWindowManagerService windowManager;
    private readonly DispatcherQueue dispatcherQueue;
    private readonly DemoDocumentTabSource tabSource;
    private readonly DemoDocumentWindows documentWindows;
    private readonly Dictionary<DocumentId, IDocumentMetadata> documents = [];
    private readonly Dictionary<WindowId, DocumentId> windowActiveDocument = [];
    private readonly IDisposable windowEventsSubscription;

    private bool isDisposed;
    private int documentCount;

    /// <summary>
    ///     Initializes a new instance of the <see cref="WindowManagerShellViewModel"/> class.
    /// </summary>
    /// <param name="router">The router used for navigation.</param>
    /// <param name="hostingContext">The hosting context containing dispatcher information.</param>
    /// <param name="windowFactory">The window factory for creating new windows.</param>
    /// <param name="windowManager">The window manager service.</param>
    /// <param name="tabSource">The sample's documents, whose tabs every window that shows documents binds.</param>
    /// <param name="documentWindows">Opens the sample's document windows.</param>
    public WindowManagerShellViewModel(
        IRouter router,
        HostingContext hostingContext,
        IWindowFactory windowFactory,
        IWindowManagerService windowManager,
        DemoDocumentTabSource tabSource,
        DemoDocumentWindows documentWindows)
    {
        ArgumentNullException.ThrowIfNull(hostingContext.Dispatcher, nameof(hostingContext));

        this.windowFactory = windowFactory;
        this.windowManager = windowManager;
        this.dispatcherQueue = hostingContext.Dispatcher;
        this.tabSource = tabSource;
        this.documentWindows = documentWindows;

        this.Outlets.Add(OutletName.Primary, (nameof(this.ContentViewModel), null));

        _ = router.Events.OfType<ActivationComplete>()
            .Take(1)
            .Subscribe(@event =>
            {
                this.Window = (Window)@event.Context.NavigationTarget;

                // The main window shows the demo's document tabs.
                if (this.windowManager.GetWindow(this.Window.AppWindow.Id) is { } main)
                {
                    main.DocumentTabSource = this.tabSource;
                }
            });

        // A window that closes closes its documents first, which the user may refuse.
        this.windowManager.WindowClosing += this.OnWindowClosingAsync;

        // Subscribe to window lifecycle events to update the window list
        this.windowEventsSubscription = this.windowManager.WindowEvents
            .Subscribe(evt =>
                _ = this.dispatcherQueue.TryEnqueue(() => this.OnWindowLifecycleEvent(evt)));

        // Follow the documents to show the active one, ask before a document closes, and close an emptied document
        // window.
        this.tabSource.DocumentOpened += this.OnDocOpened;
        this.tabSource.DocumentClosing += this.OnDocClosing;
        this.tabSource.DocumentActivated += this.OnDocActivated;
        this.tabSource.DocumentClosed += this.OnDocClosed;
        this.tabSource.WindowEmptied += this.OnWindowEmptied;

        // Initialize the window list
        this.UpdateWindowList();
    }

    [ObservableProperty]
    public partial string? ActiveDocumentTitle { get; set; }

    [ObservableProperty]
    public partial bool ActiveDocumentIsDirty { get; set; }

    /// <summary>
    /// Gets or sets whether the demo should ask for confirmation when closing documents.
    /// </summary>
    [ObservableProperty]
    public partial bool ConfirmBeforeClose { get; set; } = true;

    /// <summary>
    ///     Gets a simple indicator text when the active document is dirty.
    /// </summary>
    public string ActiveDocumentDirtyIndicator => this.ActiveDocumentIsDirty ? " • Unsaved" : string.Empty;

    /// <summary>
    ///     Gets the window associated with this view model.
    /// </summary>
    public Window? Window { get; private set; }

    /// <summary>
    ///     Gets the content view model for the primary outlet.
    /// </summary>
    public object? ContentViewModel => this.Outlets[OutletName.Primary].viewModel;

    /// <summary>
    ///     Gets the collection of currently open windows for display.
    /// </summary>
    [ObservableProperty]
    public partial ObservableCollection<WindowInfo> OpenWindows { get; set; } = [];

    /// <summary>
    ///     Gets the total count of open windows.
    /// </summary>
    [ObservableProperty]
    public partial int WindowCount { get; set; }

    /// <summary>
    ///     Gets the currently active window information.
    /// </summary>
    [ObservableProperty]
    public partial string? ActiveWindowInfo { get; set; }

    /// <summary>
    ///     Gets or sets the selected backdrop for new windows.
    /// </summary>
    [ObservableProperty]
    public partial BackdropKind SelectedBackdrop { get; set; } = BackdropKind.Mica;

    /// <summary>
    ///     Gets the available backdrop options.
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
            this.tabSource.DocumentOpened -= this.OnDocOpened;
            this.tabSource.DocumentClosing -= this.OnDocClosing;
            this.tabSource.DocumentActivated -= this.OnDocActivated;
            this.tabSource.DocumentClosed -= this.OnDocClosed;
            this.tabSource.WindowEmptied -= this.OnWindowEmptied;

            this.windowManager.WindowClosing -= this.OnWindowClosingAsync;

            foreach (var metadata in this.documents.Values)
            {
                metadata.PropertyChanged -= this.OnDocPropertyChanged;
            }

            this.documents.Clear();
        }

        this.isDisposed = true;
        base.Dispose(disposing);
    }

    /// <summary>
    ///     Command to create a new tool window with the selected backdrop.
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
    ///     Command to open a new document in the main window.
    /// </summary>
    [RelayCommand]
    private async Task CreateDocumentAsync()
    {
        if (this.MainWindowId() is { } main)
        {
            _ = await this.tabSource.OpenDocumentAsync(main, this.NewDocument()).ConfigureAwait(true);
        }
    }

    /// <summary>
    ///     Command to open a new document in a new document window.
    /// </summary>
    [RelayCommand]
    private async Task CreateDocumentWindowAsync()
    {
        try
        {
            if (await this.documentWindows.OpenAsync().ConfigureAwait(true) is { } window)
            {
                _ = await this.tabSource.OpenDocumentAsync(window, this.NewDocument()).ConfigureAwait(true);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to create document window: {ex.Message}");
        }
    }

    /// <summary>
    ///     Command to open a document in the main window whose tab is pinned and pin-locked: it shows no pin button, the
    ///     user cannot unpin it, and it cannot be dragged out of the tab strip.
    /// </summary>
    [RelayCommand]
    private async Task CreatePinLockedDocumentAsync()
    {
        try
        {
            if (this.MainWindowId() is not { } main)
            {
                return;
            }

            var md = new SampleDocumentMetadata
            {
                Title = "Pinned Document",
                IsPinnedHint = true,
                IsPinLockedHint = true,
            };
            _ = await this.tabSource.OpenDocumentAsync(main, md).ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Failed to create pinned document: {ex.Message}");
        }
    }

    /// <summary>
    ///     Command to create a new frameless window with the DroidNet logo.
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
    ///     Command to close a specific window.
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
    ///     Command to activate a specific window.
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
    ///     Command to close all windows.
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
    ///     Handles window lifecycle events to update the UI.
    /// </summary>
    /// <param name="evt">The window lifecycle event.</param>
    private void OnWindowLifecycleEvent(WindowLifecycleEvent evt)
    {
        _ = evt; // unused

        this.UpdateWindowList();
        this.UpdateActiveWindowInfo();
    }

    /// <summary>
    ///     Updates the list of open windows.
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
    ///     Updates the active window information display.
    /// </summary>
    private void UpdateActiveWindowInfo()
    {
        var activeWindow = this.windowManager.ActiveWindow;
        this.ActiveWindowInfo = activeWindow is not null
            ? string.Create(CultureInfo.InvariantCulture, $"{activeWindow.Window.Title} ({activeWindow.Category})")
            : "None";

        if (activeWindow is not null && this.windowActiveDocument.TryGetValue(activeWindow.Id, out var docId) && this.documents.TryGetValue(docId, out var meta))
        {
            this.ActiveDocumentTitle = meta.Title;
            this.ActiveDocumentIsDirty = meta.IsDirty;
        }
        else
        {
            this.ActiveDocumentTitle = null;
            this.ActiveDocumentIsDirty = false;
        }
    }

    // Document service event handlers (kept near other UI update helpers)
    private void OnDocOpened(object? sender, DocumentOpenedEventArgs e)
    {
        if (e is null || e.Metadata is null)
        {
            return;
        }

        var dq = this.windowManager.GetWindow(e.WindowId)?.Window?.DispatcherQueue ?? this.dispatcherQueue;
        _ = dq.TryEnqueue(() => _ = OnDocOpenedAsync(e));

        Task OnDocOpenedAsync(DocumentOpenedEventArgs args)
        {
            Debug.WriteLine($"OnDocOpened: DocId={args.Metadata.DocumentId}, Title='{args.Metadata.Title}', Window={args.WindowId.Value}, ShouldSelect={args.ShouldSelect}");
            if (this.documents.TryAdd(args.Metadata.DocumentId, args.Metadata))
            {
                args.Metadata.PropertyChanged += this.OnDocPropertyChanged;
            }

            if (args.ShouldSelect)
            {
                this.windowActiveDocument[args.WindowId] = args.Metadata.DocumentId;
            }

            if (args.WindowId == this.windowManager.ActiveWindow?.Id)
            {
                this.ActiveDocumentTitle = args.Metadata.Title;
                this.ActiveDocumentIsDirty = args.Metadata.IsDirty;
            }

            return Task.CompletedTask;
        }
    }

    // A document's metadata changes by itself; the active document's title and dirty state follow it.
    private void OnDocPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs e)
    {
        if (sender is not IDocumentMetadata metadata)
        {
            return;
        }

        _ = this.dispatcherQueue.TryEnqueue(() =>
        {
            Debug.WriteLine($"OnDocPropertyChanged: DocId={metadata.DocumentId}, Title='{metadata.Title}', IsDirty={metadata.IsDirty}");
            if (this.windowManager.ActiveWindow is { } active
                && this.windowActiveDocument.TryGetValue(active.Id, out var activeId) && activeId == metadata.DocumentId)
            {
                this.ActiveDocumentTitle = metadata.Title;
                this.ActiveDocumentIsDirty = metadata.IsDirty;
            }
        });
    }

    private void OnDocActivated(object? sender, DocumentActivatedEventArgs e)
    {
        if (e is null)
        {
            return;
        }

        var dq = this.windowManager.GetWindow(e.WindowId)?.Window?.DispatcherQueue ?? this.dispatcherQueue;
        _ = dq.TryEnqueue(() => _ = OnDocActivatedAsync(e));

        Task OnDocActivatedAsync(DocumentActivatedEventArgs args)
        {
            Debug.WriteLine($"OnDocActivated: DocId={args.DocumentId}, Window={args.WindowId.Value}");

            if (!this.documents.TryGetValue(args.DocumentId, out var metadata))
            {
                return Task.CompletedTask;
            }

            this.windowActiveDocument[args.WindowId] = args.DocumentId;

            if (args.WindowId == this.windowManager.ActiveWindow?.Id)
            {
                this.ActiveDocumentTitle = metadata.Title;
                this.ActiveDocumentIsDirty = metadata.IsDirty;
            }

            return Task.CompletedTask;
        }
    }

    private void OnDocClosing(object? sender, DocumentClosingEventArgs e)
    {
        if (e is null || e.Metadata is null)
        {
            return;
        }

        // Create a TaskCompletionSource that will be completed by the UI confirmation dialog.
        var tcs = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        e.AddVetoTask(tcs.Task);

        // Respect the demo toggle; if confirmation is disabled, allow the close immediately. A forced close asks
        // nothing.
        if (!this.ConfirmBeforeClose || e.Force)
        {
            _ = tcs.TrySetResult(true);
            return;
        }

        // Show a confirmation dialog on the target window's UI thread. We prefer the
        // window in the event args so the dialog is shown on the proper window; fall
        // back to this view model's Window property.
        var targetWindow = this.windowManager.GetWindow(e.WindowId)?.Window ?? this.Window;
        if (targetWindow is null)
        {
            // If there's no UI available for the window, allow close by default.
            _ = tcs.TrySetResult(true);
            return;
        }

        var delt = targetWindow.DispatcherQueue;
        _ = delt.TryEnqueue(() => _ = ShowConfirmDialogAsync(targetWindow));

        async Task ShowConfirmDialogAsync(Window win)
        {
            try
            {
                var dialog = new ContentDialog
                {
                    Title = "Close document?",
                    Content = string.Create(CultureInfo.InvariantCulture, $"Close '{e.Metadata.Title}'?"),
                    PrimaryButtonText = "Close",
                    CloseButtonText = "Cancel",
                };

                if (win.Content?.XamlRoot is not null)
                {
                    dialog.XamlRoot = win.Content.XamlRoot;
                    var result = await dialog.ShowAsync().AsTask().ConfigureAwait(true);
                    _ = tcs.TrySetResult(result == ContentDialogResult.Primary);
                }
                else
                {
                    Debug.WriteLine("OnDocClosing: target window has no XamlRoot; allowing close.");
                    _ = tcs.TrySetResult(true);
                }
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"OnDocClosing dialog failed: {ex.Message}");
                _ = tcs.TrySetResult(false);
            }
        }
    }

    // Closing a window closes its documents first, asking about each one in that window: Cancel keeps the window open.
    private async Task OnWindowClosingAsync(object? sender, WindowClosingEventArgs args)
    {
        if (!args.Cancel)
        {
            args.Cancel = !await this.tabSource.CloseWindowDocumentsAsync(args.WindowId).ConfigureAwait(true);
        }
    }

    // A document window exists to show its tabs: it closes once its last tab was closed or moved to another window.
    private async void OnWindowEmptied(object? sender, WindowEmptiedEventArgs args)
    {
        if (this.windowManager.GetWindow(args.Window)?.Category == WindowCategory.Document)
        {
            _ = await this.windowManager.CloseWindowAsync(args.Window).ConfigureAwait(true);
        }
    }

    private WindowId? MainWindowId()
        => this.windowManager.OpenWindows.FirstOrDefault(static window => window.Category == WindowCategory.Main)?.Id;

    private SampleDocumentMetadata NewDocument()
        => new() { Title = string.Create(CultureInfo.InvariantCulture, $"Document {++this.documentCount}") };

    private void OnDocClosed(object? sender, DocumentClosedEventArgs e)
    {
        if (e is null || e.Metadata is null)
        {
            return;
        }

        var dq = this.windowManager.GetWindow(e.WindowId)?.Window?.DispatcherQueue ?? this.dispatcherQueue;
        _ = dq.TryEnqueue(() => HandleDocClosed(e));

        void HandleDocClosed(DocumentClosedEventArgs args)
        {
            Debug.WriteLine($"OnDocClosed: DocId={args.Metadata.DocumentId}, Title='{args.Metadata.Title}', Window={args.WindowId.Value}");

            if (this.documents.Remove(args.Metadata.DocumentId, out var closed))
            {
                closed.PropertyChanged -= this.OnDocPropertyChanged;
            }

            if (this.windowActiveDocument.TryGetValue(args.WindowId, out var active) && active == args.Metadata.DocumentId)
            {
                _ = this.windowActiveDocument.Remove(args.WindowId);
            }

            if (args.WindowId == this.windowManager.ActiveWindow?.Id)
            {
                this.ActiveDocumentTitle = null;
                this.ActiveDocumentIsDirty = false;
            }
        }
    }
}
