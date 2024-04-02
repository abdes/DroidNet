// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;

/// <summary>A decorated panel that represents a dock.</summary>
[ViewModel(typeof(DockPanelViewModel))]
[ObservableObject]
public sealed partial class DockPanel
{
    private const double ResizeThrottleInMs = 100.0;

    private readonly PointerEventHandler pointerEnterEventHandler;
    private readonly PointerEventHandler pointerExitEventHandler;

    private IDisposable? sizeChangedSubscription;

    [ObservableProperty]
    private Visibility overlayVisibility = Visibility.Collapsed;

    public DockPanel()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, args) =>
        {
            if (args.OldValue is not null)
            {
                args.OldValue.PropertyChanged -= this.OnIsInDockingModePropertyChanged;
                args.OldValue.PropertyChanged -= this.OnIsBeingDockedPropertyChanged;
                args.OldValue.IsActive = false;
            }

            if (this.ViewModel is null)
            {
                return;
            }

            this.ViewModel.PropertyChanged += this.OnIsInDockingModePropertyChanged;
            this.ViewModel.PropertyChanged += this.OnIsBeingDockedPropertyChanged;
            this.ViewModel.IsActive = true;
        };

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;

        this.pointerEnterEventHandler = (_, _) => this.ShowOverlay();
        this.pointerExitEventHandler = (_, _) => this.HideOverlay();
    }

    public override string ToString() => $"{nameof(DockPanel)} [{this.ViewModel?.Title ?? string.Empty}]";

    private void OnLoaded(object o, RoutedEventArgs routedEventArgs)
    {
        // Register for size changes, but we don't want to trigger the effect on every change when a dock is being
        // continuously resized. So, we throttle the events and only react after a stable size is reached.
        this.sizeChangedSubscription = Observable.FromEventPattern<SizeChangedEventHandler, SizeChangedEventArgs>(
                h => this.SizeChanged += h,
                h => this.SizeChanged -= h)
            .Throttle(TimeSpan.FromMilliseconds(ResizeThrottleInMs))
            .Subscribe(
                evt => this.DispatcherQueue.TryEnqueue(() => this.ViewModel?.OnSizeChanged(evt.EventArgs.NewSize)));

        // We should always set the initial size in the view model after the view is loaded, and everytime the view
        // model changes.
        this.ViewModel?.OnSizeChanged(this.GetActualSize());
        this.ViewModelChanged += (_, _) => this.ViewModel?.OnSizeChanged(this.GetActualSize());
    }

    private void OnUnloaded(object o, RoutedEventArgs routedEventArgs)
    {
        this.sizeChangedSubscription?.Dispose();
        this.ViewModel = null;
    }

    private void OnIsBeingDockedPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not DockPanelViewModel vm || !string.Equals(
                args.PropertyName,
                nameof(DockPanelViewModel.IsBeingDocked),
                StringComparison.Ordinal))
        {
            return;
        }

        if (vm.IsBeingDocked)
        {
            this.AnimatedBorderStoryBoard.Begin();
        }
        else
        {
            this.AnimatedBorderStoryBoard.Stop();
        }
    }

    private void OnIsInDockingModePropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not DockPanelViewModel vm || !string.Equals(
                args.PropertyName,
                nameof(DockPanelViewModel.IsInDockingMode),
                StringComparison.Ordinal))
        {
            return;
        }

        if (vm.IsInDockingMode)
        {
            this.EnterDockingMode();
        }
        else
        {
            this.LeaveDockingMode();
        }
    }

    private void ShowOverlay()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        if (!this.ViewModel.IsInDockingMode)
        {
            return;
        }

        Debug.WriteLine(
            $"{this} : Show Overlay {(this.ViewModel.IsBeingDocked ? "and I am being docked" : string.Empty)}");
        this.OverlayVisibility = Visibility.Visible;
    }

    private void HideOverlay()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        if (this.OverlayVisibility != Visibility.Visible)
        {
            return;
        }

        Debug.WriteLine(
            $"{this} : Hide Overlay {(this.ViewModel.IsBeingDocked ? "and I am being docked" : string.Empty)}");
        this.OverlayVisibility = Visibility.Collapsed;
    }

    private void LeaveDockingMode()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        this.HideOverlay();

        this.PointerEntered -= this.pointerEnterEventHandler;
        this.PointerExited -= this.pointerExitEventHandler;
    }

    private void EnterDockingMode()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        if (this.ViewModel.IsBeingDocked)
        {
            this.ShowOverlay();
        }

        this.PointerEntered += this.pointerEnterEventHandler;
        this.PointerExited += this.pointerExitEventHandler;
    }

    private Size GetActualSize() => new()
    {
        Width = double.Round(this.ActualWidth),
        Height = double.Round(this.ActualHeight),
    };
}
