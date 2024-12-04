// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Data;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;

namespace DroidNet.Docking.Controls;

/// <summary>A decorated panel that represents a dock.</summary>
[ViewModel(typeof(DockPanelViewModel))]
[ObservableObject]
public sealed partial class DockPanel
{
    /// <summary>
    /// Identifies the <see cref="IconConverter"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty IconConverterProperty =
        DependencyProperty.Register(
            nameof(IconConverter),
            typeof(IValueConverter),
            typeof(DockableTabsBar),
            new PropertyMetadata(defaultValue: null));

    /// <summary>
    /// Identifies the <see cref="VmToViewConverter"/> dependency property.
    /// </summary>
    public static readonly DependencyProperty VmToViewConverterProperty =
        DependencyProperty.Register(
            nameof(VmToViewConverter),
            typeof(IValueConverter),
            typeof(DockPanel),
            new PropertyMetadata(defaultValue: null, OnVmToViewConverterChange));

    private const double ResizeThrottleInMs = 50.0;

    private readonly PointerEventHandler pointerEnterEventHandler;
    private readonly PointerEventHandler pointerExitEventHandler;

    private IDisposable? sizeChangedSubscription;

    [ObservableProperty]
    private Visibility overlayVisibility = Visibility.Collapsed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DockPanel"/> class.
    /// </summary>
    public DockPanel()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, args) =>
        {
            if (args.OldValue is not null)
            {
                args.OldValue.PropertyChanged -= this.OnIsInDockingModePropertyChanged;
                args.OldValue.IsActive = false;
            }

            if (this.ViewModel is null)
            {
                return;
            }

            this.ViewModel.PropertyChanged += this.OnIsInDockingModePropertyChanged;
            this.ViewModel.IsActive = true;
        };

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;

        this.pointerEnterEventHandler = (_, _) => this.ShowOverlay();
        this.pointerExitEventHandler = (_, _) => this.HideOverlay();
    }

    /// <summary>
    /// Gets or sets the converter used to get an icon for the dockable entity.
    /// </summary>
    public IValueConverter IconConverter
    {
        get => (IValueConverter)this.GetValue(IconConverterProperty);
        set => this.SetValue(IconConverterProperty, value);
    }

    /// <summary>
    /// Gets or sets the converter used to get an icon for the dockable entity.
    /// </summary>
    /// <remarks>
    /// Setting this property on a DockPanel is particularly important when the docking workspace uses
    /// a child IoC container, in which the view models and views are registered. The global converter
    /// in this situation will not be able to resolve view models to views correctly.
    /// <para>
    /// When a value is set, it is also set as a resource in the control's resources dictionary with
    /// the key "VmToViewConverter". If the value set is <see langword="null"/>, the resource falls
    /// back to the Application resource with the same key.
    /// </para>
    /// </remarks>
    public IValueConverter VmToViewConverter
    {
        get => (IValueConverter)this.GetValue(VmToViewConverterProperty);
        set => this.SetValue(VmToViewConverterProperty, value);
    }

    /// <inheritdoc/>
    public override string ToString() => $"{nameof(DockPanel)} [{this.ViewModel?.Title ?? string.Empty}]";

    private static void OnVmToViewConverterChange(DependencyObject d, DependencyPropertyChangedEventArgs args)
    {
        if (d is not DockPanel dockPanel)
        {
            return;
        }

        var converter = args.NewValue as IValueConverter ?? Application.Current.Resources["VmToViewConverter"];
        if (converter is not null)
        {
            dockPanel.Resources["VmToViewConverter"] = converter;
        }
    }

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

        // We should always set the initial size in the view model after the view is loaded, and every time the view
        // model changes.
        this.ViewModel?.OnSizeChanged(this.GetActualSize());
        this.ViewModelChanged += this.OnViewModelChanged;

        // this.InitializeTabView();
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<DockPanelViewModel> args)
    {
        if (args.OldValue != null)
        {
            // Unsubscribe
        }

        if (this.ViewModel is not null)
        {
            // Subscribe
            // ((INotifyCollectionChanged)this.ViewModel.Dockables).CollectionChanged += this.Dockables_CollectionChanged;
        }

        this.UpdateViewModelWithInitialSize();
    }

    private void UpdateViewModelWithInitialSize()
        => this.ViewModel?.OnSizeChanged(this.GetActualSize());

    private void OnUnloaded(object o, RoutedEventArgs routedEventArgs)
    {
        this.ViewModelChanged -= this.OnViewModelChanged;
        this.sizeChangedSubscription?.Dispose();
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

        // $"{this} : Show Overlay {(this.ViewModel.IsBeingDocked ? "and I am being docked" : string.Empty)}"
        this.OverlayVisibility = Visibility.Visible;
    }

    private void HideOverlay()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        if (this.OverlayVisibility != Visibility.Visible)
        {
            return;
        }

        // "{this} : Hide Overlay {(this.ViewModel.IsBeingDocked ? "and I am being docked" : string.Empty)}"
        this.OverlayVisibility = Visibility.Collapsed;
    }

    private void LeaveDockingMode()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        this.AnimatedBorderStoryBoard.Stop();
        this.HideOverlay();

        this.PointerEntered -= this.pointerEnterEventHandler;
        this.PointerExited -= this.pointerExitEventHandler;
    }

    private void EnterDockingMode()
    {
        Debug.Assert(this.ViewModel is not null, "expecting the ViewModel to be not null");

        if (this.ViewModel.IsBeingDocked)
        {
            this.AnimatedBorderStoryBoard.Begin();
            this.Overlay.ContentTemplate = (DataTemplate)this.Resources["RootDockingOverlayTemplate"];
            this.ShowOverlay();
        }
        else
        {
            this.Overlay.ContentTemplate = (DataTemplate)this.Resources["RelativeDockingOverlayTemplate"];
        }

        this.PointerEntered += this.pointerEnterEventHandler;
        this.PointerExited += this.pointerExitEventHandler;
    }

    private Size GetActualSize() => new()
    {
        Width = double.Round(this.ActualWidth),
        Height = double.Round(this.ActualHeight),
    };

    private void OnAcceleratorInvoked(KeyboardAccelerator accelerator, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = args;

        if (this.ViewModel!.IsBeingDocked)
        {
            this.HandleRootDockingAccelerator(accelerator);
        }
        else
        {
            this.HandleRelativeDockingAccelerator(accelerator);
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Style",
        "IDE0010:Add missing cases",
        Justification = "We're only interested in the keys we manage")]
    private void HandleRelativeDockingAccelerator(KeyboardAccelerator accelerator)
    {
        switch (accelerator.Key)
        {
            case VirtualKey.Escape:
                this.ViewModel?.ToggleDockingModeCommand.Execute(this);
                break;

            case VirtualKey.H:
                this.ViewModel?.AcceptDockBeingDockedCommand.Execute(nameof(AnchorPosition.Left));
                break;

            case VirtualKey.J:
                this.ViewModel?.AcceptDockBeingDockedCommand.Execute(nameof(AnchorPosition.Bottom));
                break;

            case VirtualKey.K:
                this.ViewModel?.AcceptDockBeingDockedCommand.Execute(nameof(AnchorPosition.Top));
                break;

            case VirtualKey.L:
                this.ViewModel?.AcceptDockBeingDockedCommand.Execute(nameof(AnchorPosition.Right));
                break;

            case VirtualKey.O:
                this.ViewModel?.AcceptDockBeingDockedCommand.Execute(nameof(AnchorPosition.With));
                break;

            // ReSharper disable once RedundantEmptySwitchSection
            default:
                /* Do nothing */
                break;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0010:Add missing cases", Justification = "We're only interested in the keys we manage")]
    private void HandleRootDockingAccelerator(KeyboardAccelerator accelerator)
    {
        // ReSharper disable once SwitchStatementHandlesSomeKnownEnumValuesWithDefault
        switch (accelerator.Key)
        {
            case VirtualKey.Escape:
                this.ViewModel?.ToggleDockingModeCommand.Execute(this);
                break;

            case VirtualKey.H:
                this.ViewModel?.DockToRootCommand.Execute(nameof(AnchorPosition.Left));
                break;

            case VirtualKey.J:
                this.ViewModel?.DockToRootCommand.Execute(nameof(AnchorPosition.Bottom));
                break;

            case VirtualKey.K:
                this.ViewModel?.DockToRootCommand.Execute(nameof(AnchorPosition.Top));
                break;

            case VirtualKey.L:
                this.ViewModel?.DockToRootCommand.Execute(nameof(AnchorPosition.Right));
                break;

            // ReSharper disable once RedundantEmptySwitchSection
            default:
                /* Do nothing */
                break;
        }
    }
}
