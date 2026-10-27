// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;

namespace Oxygen.Editor.SceneEditor;

/// <summary>
///     Represents the view for editing scenes in the Oxygen World Editor.
/// </summary>
[ViewModel(typeof(SceneEditorViewModel))]
public sealed partial class SceneEditorView : UserControl, IDocumentEditorView
{
    private readonly Dictionary<ViewportViewModel, Viewport> viewportControls = [];
    private readonly SemaphoreSlim viewportControlsGate = new(initialCount: 1, maxCount: 1);
    private SceneEditorViewModel? currentViewModel;
    private Flyout? instanceSourceFlyout;
    private AssetPicker? instanceSourcePicker;
    private long lifecycleGeneration;
    private bool isDeactivated = true;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneEditorView"/> class.
    /// </summary>
    public SceneEditorView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        _ = this.RegisterPropertyChangedCallback(ViewModelProperty, this.OnViewModelDependencyPropertyChanged);
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        var generation = Interlocked.Increment(ref this.lifecycleGeneration);
        this.isDeactivated = false;

        await this.RunViewportControlsOperationAsync(
            generation,
            async () =>
            {
                await this.AttachToViewModelCoreAsync(this.ViewModel as SceneEditorViewModel ?? this.currentViewModel).ConfigureAwait(true);
                this.Bindings.Update();
                await this.RebuildLayoutCoreAsync().ConfigureAwait(true);
            }).ConfigureAwait(true);
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        // A layout that moves the view, as the workspace does when it rebuilds its docks, raises Unloaded and Loaded
        // in either order. Only a view still out of the tree once that layout settles has left, and releases its
        // viewports; a moved view keeps them.
        _ = this.DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, this.DeactivateIfLeft);
    }

    private async void DeactivateIfLeft()
    {
        if (this.IsLoaded || this.isDeactivated)
        {
            return;
        }

        await this.DeactivateAsync().ConfigureAwait(true);
    }

    /// <summary>
    /// Detaches and disposes all viewport controls owned by this view.
    /// </summary>
    /// <returns>A task that completes when active viewport controls are detached.</returns>
    public async Task DeactivateAsync()
    {
        _ = Interlocked.Increment(ref this.lifecycleGeneration);
        this.isDeactivated = true;

        await this.viewportControlsGate.WaitAsync().ConfigureAwait(true);
        try
        {
            this.DetachFromCurrentViewModel();
            await this.ClearViewportControlsCoreAsync().ConfigureAwait(true);
        }
        finally
        {
            this.viewportControlsGate.Release();
        }
    }

    private async void OnSceneEditorViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!string.Equals(e.PropertyName, nameof(SceneEditorViewModel.CurrentLayout), System.StringComparison.Ordinal)
            && !string.Equals(e.PropertyName, nameof(SceneEditorViewModel.MaximizedViewport), System.StringComparison.Ordinal))
        {
            return;
        }

        var generation = Volatile.Read(ref this.lifecycleGeneration);
        await this.RunViewportControlsOperationAsync(generation, this.RebuildLayoutCoreAsync).ConfigureAwait(true);
    }

    private async Task AttachToViewModelCoreAsync(SceneEditorViewModel? viewModel)
    {
        if (ReferenceEquals(this.currentViewModel, viewModel))
        {
            return;
        }

        this.DetachFromCurrentViewModel();
        await this.ClearViewportControlsCoreAsync().ConfigureAwait(true);
        this.currentViewModel = viewModel;

        if (this.currentViewModel != null)
        {
            this.currentViewModel.PropertyChanged += this.OnSceneEditorViewModelPropertyChanged;
            this.currentViewModel.Viewports.CollectionChanged += this.OnViewportsChanged;
            this.currentViewModel.InstanceSourcePickerRequested += this.OnInstanceSourcePickerRequested;
        }
    }

    private void DetachFromCurrentViewModel()
    {
        if (this.currentViewModel == null)
        {
            return;
        }

        this.currentViewModel.PropertyChanged -= this.OnSceneEditorViewModelPropertyChanged;
        this.currentViewModel.Viewports.CollectionChanged -= this.OnViewportsChanged;
        this.currentViewModel.InstanceSourcePickerRequested -= this.OnInstanceSourcePickerRequested;
        this.QuickAddButton.Closed -= this.OnQuickAddClosed;
        this.instanceSourceFlyout?.Hide();
        this.currentViewModel = null;
    }

    private async void OnViewModelDependencyPropertyChanged(DependencyObject sender, DependencyProperty dp)
    {
        _ = sender;
        _ = dp;

        var generation = Volatile.Read(ref this.lifecycleGeneration);
        await this.RunViewportControlsOperationAsync(
            generation,
            async () =>
            {
                await this.AttachToViewModelCoreAsync(this.ViewModel).ConfigureAwait(true);
                this.Bindings.Update();
                await this.RebuildLayoutCoreAsync().ConfigureAwait(true);
            }).ConfigureAwait(true);
    }

    private void OnViewportsChanged(object? sender, NotifyCollectionChangedEventArgs e)
    {
        var generation = Volatile.Read(ref this.lifecycleGeneration);
        _ = this.DispatcherQueue?.TryEnqueue(() => _ = this.RunViewportControlsOperationAsync(generation, this.RebuildLayoutCoreAsync));
    }

    private async Task ClearViewportControlsCoreAsync()
    {
        var controls = this.viewportControls.Values.ToList();
        this.viewportControls.Clear();

        foreach (var control in controls)
        {
            control.PointerPressed -= this.OnViewportPointerPressed;
            control.GotFocus -= this.OnViewportGotFocus;
            _ = this.ViewportGrid.Children.Remove(control);
            await control.DisposeAsync().ConfigureAwait(true);
        }
    }

    private async Task RebuildLayoutCoreAsync()
    {
        var viewModel = this.currentViewModel ?? this.ViewModel as SceneEditorViewModel;
        if (viewModel == null)
        {
            return;
        }

        // A maximized pane is shown alone; the other panes release their views but keep their state.
        if (viewModel.MaximizedViewport is { } maximized)
        {
            this.ConfigureGrid(1, 1);
            await this.SyncViewportControlsCoreAsync([maximized], [(0, 0, 1, 1)]).ConfigureAwait(true);
            return;
        }

        var (rows, cols) = SceneLayoutHelpers.GetGridDimensions(viewModel.CurrentLayout);
        this.ConfigureGrid(rows, cols);
        var placements = SceneLayoutHelpers.GetPlacements(viewModel.CurrentLayout);
        await this.SyncViewportControlsCoreAsync(viewModel.Viewports, placements).ConfigureAwait(true);
    }

    private void ConfigureGrid(int rows, int cols)
    {
        this.ViewportGrid.RowDefinitions.Clear();
        this.ViewportGrid.ColumnDefinitions.Clear();

        for (var i = 0; i < rows; i++)
        {
            this.ViewportGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        }

        for (var i = 0; i < cols; i++)
        {
            this.ViewportGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        }
    }

    private async Task SyncViewportControlsCoreAsync(IReadOnlyList<ViewportViewModel> viewports, IReadOnlyList<(int row, int column, int rowspan, int colspan)> placements)
    {
        var used = new HashSet<ViewportViewModel>();
        var count = Math.Min(placements.Count, viewports.Count);

        for (var i = 0; i < count; i++)
        {
            var viewportVm = viewports[i];
            _ = used.Add(viewportVm);

            if (!this.viewportControls.TryGetValue(viewportVm, out var viewportControl))
            {
                viewportControl = new Viewport { ViewModel = viewportVm };

                viewportControl.PointerPressed += this.OnViewportPointerPressed;
                viewportControl.GotFocus += this.OnViewportGotFocus;

                this.viewportControls[viewportVm] = viewportControl;
                this.ViewportGrid.Children.Add(viewportControl);
            }
            else if (!this.ViewportGrid.Children.Contains(viewportControl))
            {
                this.ViewportGrid.Children.Add(viewportControl);
            }

            var (row, column, rowspan, colspan) = placements[i];
            Grid.SetRow(viewportControl, row);
            Grid.SetColumn(viewportControl, column);
            Grid.SetRowSpan(viewportControl, rowspan);
            Grid.SetColumnSpan(viewportControl, colspan);
        }

        var toRemove = new List<ViewportViewModel>();

        foreach (var kvp in this.viewportControls)
        {
            if (used.Contains(kvp.Key))
            {
                continue;
            }

            var control = kvp.Value;
            control.PointerPressed -= this.OnViewportPointerPressed;
            control.GotFocus -= this.OnViewportGotFocus;
            _ = this.ViewportGrid.Children.Remove(control);
            await control.DisposeAsync().ConfigureAwait(true);
            toRemove.Add(kvp.Key);
        }

        foreach (var key in toRemove)
        {
            _ = this.viewportControls.Remove(key);
        }
    }

    private async Task RunViewportControlsOperationAsync(long generation, Func<Task> operation)
    {
        await this.viewportControlsGate.WaitAsync().ConfigureAwait(true);
        try
        {
            if (this.isDeactivated || generation != Volatile.Read(ref this.lifecycleGeneration))
            {
                return;
            }

            await operation().ConfigureAwait(true);
        }
        catch (Exception ex)
        {
            System.Diagnostics.Debug.WriteLine($"SceneEditorView viewport operation failed: {ex}");
        }
        finally
        {
            this.viewportControlsGate.Release();
        }
    }

    private void OnAlignCameraToViewInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender;
        args.Handled = true;
        var viewModel = this.currentViewModel ?? this.ViewModel as SceneEditorViewModel;
        viewModel?.AlignSelectedCameraToViewCommand.Execute(parameter: null);
    }

    private void OnViewportPointerPressed(object sender, PointerRoutedEventArgs e)
    {
        _ = e;

        if (sender is not Viewport viewport)
        {
            return;
        }

        // Ensure the control becomes focusable via mouse interaction.
        _ = viewport.Focus(FocusState.Pointer);

        var viewModel = this.currentViewModel ?? this.ViewModel as SceneEditorViewModel;
        if (viewModel is null || viewport.ViewModel is null)
        {
            return;
        }

        viewModel.SetFocusedViewport(viewport.ViewModel);
    }

    // Quick Add's Model… and Scene… open the source picker under the Add button, once the menu has closed.
    private void OnInstanceSourcePickerRequested(object? sender, EventArgs e)
    {
        if (sender is not SceneEditorViewModel { InstanceSourceChoices: { } choices } viewModel)
        {
            return;
        }

        if (this.instanceSourcePicker is null)
        {
            this.instanceSourcePicker = new AssetPicker();
            this.instanceSourcePicker.ChoiceInvoked += this.OnInstanceSourceInvoked;
            this.instanceSourceFlyout = new Flyout { Content = this.instanceSourcePicker, Placement = FlyoutPlacementMode.BottomEdgeAlignedLeft };
        }

        this.instanceSourcePicker.Model = choices;
        this.instanceSourcePicker.EmptyText = viewModel.InstanceSourceEmptyText;

        // An item runs while its menu is still open; the menu's closing would dismiss a flyout shown now.
        if (this.QuickAddButton.IsMenuOpen)
        {
            this.QuickAddButton.Closed += this.OnQuickAddClosed;
        }
        else
        {
            this.instanceSourceFlyout?.ShowAt(this.QuickAddButton);
        }
    }

    private void OnQuickAddClosed(object? sender, EventArgs e)
    {
        this.QuickAddButton.Closed -= this.OnQuickAddClosed;
        this.instanceSourceFlyout?.ShowAt(this.QuickAddButton);
    }

    private async void OnInstanceSourceInvoked(object? sender, AssetChoice choice)
    {
        this.instanceSourceFlyout?.Hide();
        if (this.currentViewModel is { } viewModel && choice.Asset is { } source)
        {
            _ = await viewModel.AddInstanceAsync(source).ConfigureAwait(true);
        }
    }

    private void OnViewportGotFocus(object sender, RoutedEventArgs e)
    {
        _ = e;

        if (sender is not Viewport viewport)
        {
            return;
        }

        var viewModel = this.currentViewModel ?? this.ViewModel as SceneEditorViewModel;
        if (viewModel is null || viewport.ViewModel is null)
        {
            return;
        }

        viewModel.SetFocusedViewport(viewport.ViewModel);
    }
}
