// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Docking.Controls;

using System.ComponentModel;
using System.Diagnostics;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Input;
using Windows.Foundation;
using Windows.System;

/// <summary>A decorated panel that represents a dock.</summary>
[ViewModel(typeof(DockPanelViewModel))]
[ObservableObject]
public sealed partial class DockPanel
{
    private const double ResizeThrottleInMs = 50.0;

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

    // TODO: Rework tabs for DockPanel dockables
#if false
    private void InitializeTabView()
    {
        this.DockablesTabView.TabItems.Clear();

        if (this.ViewModel is null)
        {
            return;
        }

        foreach (var dockable in this.ViewModel.Dockables)
        {
            this.AddTab(dockable);
        }
    }

    private void Dockables_CollectionChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        if (e is { Action: NotifyCollectionChangedAction.Add, NewItems: not null })
        {
            foreach (var newItem in e.NewItems)
            {
                this.AddTab((IDockable)newItem);
            }
        }
        else if (e is { Action: NotifyCollectionChangedAction.Remove, OldItems: not null })
        {
            foreach (var oldItem in e.OldItems)
            {
                this.RemoveTab((IDockable)oldItem);
            }
        }
    }

    private void AddTab(IDockable dockable)
    {
        var tabItem = new TabViewItem
        {
            Header = dockable.Title,
            ContentTemplate = (DataTemplate)this.Resources["DockableContentTemplate"],
            Content = dockable,
            IsClosable = false,
        };
        this.DockablesTabView.TabItems.Add(tabItem);
    }

    private void RemoveTab(IDockable dockable)
    {
        var tabItem = this.DockablesTabView.TabItems
            .OfType<TabViewItem>()
            .FirstOrDefault(ti => string.Equals(ti.Header as string, dockable.Title, StringComparison.Ordinal));
        if (tabItem != null)
        {
            this.DockablesTabView.TabItems.Remove(tabItem);
        }
    }
#endif

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

            default:
                /* Do nothing */
                break;
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage(
        "Style",
        "IDE0010:Add missing cases",
        Justification = "We're only interested in the keys we manage")]
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
