// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.Controls;

namespace Oxygen.Editor.WorldEditor.Editors.Scene;

/// <summary>
///     Represents the view for editing scenes in the Oxygen World Editor.
/// </summary>
[ViewModel(typeof(SceneEditorViewModel))]
public sealed partial class SceneEditorView : UserControl
{
    private readonly Dictionary<ViewportViewModel, Viewport> viewportControls = [];
    private SceneEditorViewModel? currentViewModel;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneEditorView"/> class.
    /// </summary>
    public SceneEditorView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        this.AttachToViewModel(this.ViewModel as SceneEditorViewModel ?? this.currentViewModel);
        this.RebuildLayout();
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        _ = sender;
        _ = e;

        this.DetachFromCurrentViewModel();
        this.ClearViewportControls();
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (!string.Equals(e.PropertyName, nameof(SceneEditorViewModel.CurrentLayout), System.StringComparison.Ordinal))
        {
            return;
        }

        this.RebuildLayout();
    }

    private void AttachToViewModel(SceneEditorViewModel? viewModel)
    {
        if (ReferenceEquals(this.currentViewModel, viewModel))
        {
            return;
        }

        this.DetachFromCurrentViewModel();
        this.ClearViewportControls();
        this.currentViewModel = viewModel;

        if (this.currentViewModel != null)
        {
            this.currentViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
            this.currentViewModel.Viewports.CollectionChanged += this.OnViewportsChanged;
        }
    }

    private void DetachFromCurrentViewModel()
    {
        if (this.currentViewModel == null)
        {
            return;
        }

        this.currentViewModel.PropertyChanged -= this.OnViewModelPropertyChanged;
        this.currentViewModel.Viewports.CollectionChanged -= this.OnViewportsChanged;
        this.currentViewModel = null;
    }

    private void OnViewportsChanged(object? sender, NotifyCollectionChangedEventArgs e)
        => _ = this.DispatcherQueue?.TryEnqueue(this.RebuildLayout);

    private void ClearViewportControls()
    {
        foreach (var control in this.viewportControls.Values)
        {
            _ = this.ViewportGrid.Children.Remove(control);
        }

        this.viewportControls.Clear();
    }

    private void RebuildLayout()
    {
        var viewModel = this.currentViewModel ?? this.ViewModel as SceneEditorViewModel;
        if (viewModel == null)
        {
            return;
        }

        this.ConfigureGrid(viewModel);
        var placements = SceneLayoutHelpers.GetPlacements(viewModel.CurrentLayout);
        this.SyncViewportControls(viewModel, placements);
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

    private void SyncViewportControls(SceneEditorViewModel viewModel, IReadOnlyList<(int row, int column, int rowspan, int colspan)> placements)
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

            _ = this.ViewportGrid.Children.Remove(kvp.Value);
            toRemove.Add(kvp.Key);
        }

        foreach (var key in toRemove)
        {
            _ = this.viewportControls.Remove(key);
        }
    }
}
