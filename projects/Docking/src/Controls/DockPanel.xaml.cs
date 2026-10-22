// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
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
public sealed partial class DockPanel
{
    /// <summary>Identifies the visibility of the docking overlay.</summary>
    public static readonly DependencyProperty OverlayVisibilityProperty = DependencyProperty.Register(
        nameof(OverlayVisibility), typeof(Visibility), typeof(DockPanel), new PropertyMetadata(Visibility.Collapsed));

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

    private const string VmToViewConverterResourceKey = "VmToViewConverter";

    private readonly HostedViews views = new();
    private readonly PointerEventHandler pointerEnterEventHandler;
    private readonly PointerEventHandler pointerExitEventHandler;

    private IDisposable? sizeChangedSubscription;

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
                args.OldValue.PropertyChanged -= this.OnActiveDockablePropertyChanged;
                ((INotifyCollectionChanged)args.OldValue.Dockables).CollectionChanged -= this.OnDockablesChanged;
                args.OldValue.IsActive = false;
            }

            if (this.ViewModel is not null)
            {
                this.ViewModel.PropertyChanged += this.OnIsInDockingModePropertyChanged;
                this.ViewModel.PropertyChanged += this.OnActiveDockablePropertyChanged;
                ((INotifyCollectionChanged)this.ViewModel.Dockables).CollectionChanged += this.OnDockablesChanged;
                this.ViewModel.IsActive = true;
            }

            this.ShowActiveDockable();
            this.ReleaseDepartedViews();
        };

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;

        this.pointerEnterEventHandler = (_, _) => this.ShowOverlay();
        this.pointerExitEventHandler = (_, _) => this.HideOverlay();
    }

    /// <summary>Gets or sets the visibility of the docking overlay.</summary>
    public Visibility OverlayVisibility
    {
        get => (Visibility)this.GetValue(OverlayVisibilityProperty);
        set => this.SetValue(OverlayVisibilityProperty, value);
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
    /// Gets or sets the converter that makes the view of a dockable's view model.
    /// </summary>
    /// <remarks>
    /// Setting this property on a DockPanel is particularly important when the docking workspace uses
    /// a child IoC container, in which the view models and views are registered. The global converter
    /// in this situation will not be able to resolve view models to views correctly.
    /// <para>
    /// When it is <see langword="null"/>, the panel uses the Application resource with the key
    /// "VmToViewConverter". The panel converts each dockable's view model once and keeps that view
    /// while the dockable stays in its dock, so a view already made is kept when the converter changes.
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
        _ = args;

        if (d is DockPanel dockPanel)
        {
            dockPanel.ShowActiveDockable();
        }
    }

    /// <summary>
    /// Shows the view of the active dockable: the one this panel already made for its view model, or a new one from
    /// the converter. A dockable that already left the dock shows nothing.
    /// </summary>
    private void ShowActiveDockable()
    {
        var active = this.ViewModel?.ActiveDockable;
        var viewModel = active is not null && this.ViewModel!.Dockables.Contains(active) ? active.ViewModel : null;
        this.ActiveDockableContent.Content = viewModel is null
            ? null
            : this.views.GetOrCreate(viewModel, this.VmToViewConverter ?? DefaultVmToViewConverter());

        static IValueConverter? DefaultVmToViewConverter()
            => Application.Current.Resources.TryGetValue(VmToViewConverterResourceKey, out var converter)
                ? converter as IValueConverter
                : null;
    }

    /// <summary>
    /// Releases the views of dockables that are no longer in the dock, such as a closed dockable or one moved to
    /// another dock, which makes its own view there.
    /// </summary>
    private void ReleaseDepartedViews()
        => this.views.RetainOnly(this.ViewModel?.Dockables.Select(dockable => dockable.ViewModel) ?? []);

    private void OnActiveDockablePropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (args.PropertyName is nameof(DockPanelViewModel.ActiveDockable) or null or "")
        {
            this.ShowActiveDockable();
        }
    }

    private void OnDockablesChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        // The active dockable may be the one that left: replace it on screen before its view is released.
        this.ShowActiveDockable();
        this.ReleaseDepartedViews();
    }

    private void OnLoaded(object o, RoutedEventArgs routedEventArgs)
    {
        _ = this.ViewModel?.IsActive = true;

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
        _ = this.ViewModel?.IsActive = false;
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

            default:
                /* Do nothing */
                break;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0010:Add missing cases", Justification = "We're only interested in the keys we manage")]
    private void HandleRootDockingAccelerator(KeyboardAccelerator accelerator)
    {
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

            default:
                /* Do nothing */
                break;
        }
    }
}
