// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Oxygen.Editor.LevelEditor;
using Oxygen.Editor.WorldEditor.SceneEditor;

namespace Oxygen.Editor.World.SceneEditor;

/// <summary>
///     Represents the view for editing scenes in the Oxygen World Editor.
/// </summary>
[ViewModel(typeof(SceneEditorViewModel))]
public sealed partial class SceneEditorView : UserControl
{
    private readonly Dictionary<ViewportViewModel, Viewport> viewportControls = [];
    private readonly SemaphoreSlim viewportControlsGate = new(initialCount: 1, maxCount: 1);
    private SceneEditorViewModel? currentViewModel;
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

    private async void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

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
        if (!string.Equals(e.PropertyName, nameof(SceneEditorViewModel.CurrentLayout), System.StringComparison.Ordinal))
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

        this.ConfigureGrid(viewModel);
        var placements = SceneLayoutHelpers.GetPlacements(viewModel.CurrentLayout);
        await this.SyncViewportControlsCoreAsync(viewModel, placements).ConfigureAwait(true);
    }

    private void ConfigureGrid(SceneEditorViewModel viewModel)
    {
        this.ViewportGrid.RowDefinitions.Clear();
        this.ViewportGrid.ColumnDefinitions.Clear();

        var (rows, cols) = SceneLayoutHelpers.GetGridDimensions(viewModel.CurrentLayout);

        for (var i = 0; i < rows; i++)
        {
            this.ViewportGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        }

        for (var i = 0; i < cols; i++)
        {
            this.ViewportGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        }
    }

    private async Task SyncViewportControlsCoreAsync(SceneEditorViewModel viewModel, IReadOnlyList<(int row, int column, int rowspan, int colspan)> placements)
    {
        var viewports = viewModel.Viewports;
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
