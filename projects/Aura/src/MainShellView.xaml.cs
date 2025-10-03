// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive;
using System.Reactive.Linq;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Windows.Foundation;
using WinUIEx;
using GridLength = Microsoft.UI.Xaml.GridLength;

namespace DroidNet.Aura;

/// <summary>
/// Represents the main shell view of the application, providing the main user interface and handling window-related events.
/// </summary>
/// <remarks>
/// This class is responsible for initializing the main shell view, setting up the custom title bar, and managing the minimum window width.
/// It decorates the window with a custom title bar, an application icon, and provides a collapsible main menu and a flyout menu for settings and theme selection.
/// </remarks>
[ViewModel(typeof(MainShellViewModel))]
public sealed partial class MainShellView : INotifyPropertyChanged
{
    private double minWindowWidth;

    /// <summary>
    /// Initializes a new instance of the <see cref="MainShellView"/> class.
    /// </summary>
    public MainShellView()
    {
        this.InitializeComponent();

        // Convert Loaded and SizeChanged events to observables
        var loadedObservable = Observable.FromEventPattern<RoutedEventHandler, RoutedEventArgs>(
            h => this.CustomTitleBar.Loaded += h,
            h => this.CustomTitleBar.Loaded -= h)
            .Select(_ => Unit.Default);

        var sizeChangedObservable = Observable.FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
            h => this.CustomTitleBar.SizeChanged += h,
            h => this.CustomTitleBar.SizeChanged -= h)
            .Select(_ => Unit.Default);

        // Merge the observables and throttle the events
        var throttledObservable = loadedObservable
            .Merge(sizeChangedObservable)
            .Throttle(TimeSpan.FromMilliseconds(100));

        // Subscribe to the throttled observable to call SetupCustomTitleBar
        _ = throttledObservable.Subscribe(_ => this.DispatcherQueue.TryEnqueue(this.SetupCustomTitleBar));
    }

    /// <inheritdoc/>
    public event PropertyChangedEventHandler? PropertyChanged;

    /// <summary>
    /// Gets the minimum width of the window.
    /// </summary>
    public double MinWindowWidth
    {
        get => this.minWindowWidth;
        private set
        {
            if (Math.Abs(this.minWindowWidth - value) < 0.5f)
            {
                return;
            }

            this.minWindowWidth = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.MinWindowWidth)));

            if (this.ViewModel?.Window is WindowEx window && window.MinWidth < this.MinWindowWidth)
            {
                window.MinWidth = this.MinWindowWidth;
            }
        }
    }

    /// <summary>
    /// Converts a <see cref="Rect"/> to a <see cref="Windows.Graphics.RectInt32"/> based on the specified scale.
    /// </summary>
    /// <param name="bounds">The bounds to convert.</param>
    /// <param name="scale">The scale factor to apply.</param>
    /// <returns>A <see cref="Windows.Graphics.RectInt32"/> representing the scaled bounds.</returns>
    private static Windows.Graphics.RectInt32 GetRect(Rect bounds, double scale) => new(
        _X: (int)Math.Round(bounds.X * scale),
        _Y: (int)Math.Round(bounds.Y * scale),
        _Width: (int)Math.Round(bounds.Width * scale),
        _Height: (int)Math.Round(bounds.Height * scale));

    /// <summary>
    /// Sets up the custom title bar for the window.
    /// </summary>
    private void SetupCustomTitleBar()
    {
        Debug.Assert(
            this.ViewModel?.Window is not null,
            "expecting a properly setup ViewModel when loaded");

        var appWindow = this.ViewModel.Window.AppWindow;
        var scaleAdjustment = this.CustomTitleBar.XamlRoot.RasterizationScale;

        this.CustomTitleBar.Height = appWindow.TitleBar.Height / scaleAdjustment;

        this.LeftPaddingColumn.Width = new GridLength(appWindow.TitleBar.LeftInset / scaleAdjustment);
        this.RightPaddingColumn.Width = new GridLength(appWindow.TitleBar.RightInset / scaleAdjustment);

        var transform = this.PrimaryCommands.TransformToVisual(visual: null);
        var bounds = transform.TransformBounds(
            new Rect(
                0,
                0,
                this.PrimaryCommands.ActualWidth,
                this.PrimaryCommands.ActualHeight));
        var primaryCommandsRect = GetRect(bounds, scaleAdjustment);

        transform = this.SecondaryCommands.TransformToVisual(visual: null);
        bounds = transform.TransformBounds(
            new Rect(
                0,
                0,
                this.SecondaryCommands.ActualWidth,
                this.SecondaryCommands.ActualHeight));
        var secondaryCommandsRect = GetRect(bounds, scaleAdjustment);

        var rectArray = new[] { primaryCommandsRect, secondaryCommandsRect };

        var nonClientInputSrc = InputNonClientPointerSource.GetForWindowId(appWindow.Id);
        nonClientInputSrc.SetRegionRects(NonClientRegionKind.Passthrough, rectArray);

        this.MinWindowWidth = this.LeftPaddingColumn.Width.Value + this.IconColumn.ActualWidth +
                              this.PrimaryCommands.ActualWidth + this.DragColumn.MinWidth +
                              this.SecondaryCommands.ActualWidth +
                              this.RightPaddingColumn.Width.Value;
    }
}
