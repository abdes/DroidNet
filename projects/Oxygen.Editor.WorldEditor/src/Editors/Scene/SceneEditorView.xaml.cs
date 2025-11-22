// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

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
        if (this.ViewModel != null)
        {
            this.ViewModel.PropertyChanged += this.OnViewModelPropertyChanged;
            this.RebuildLayout();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs e)
    {
        if (this.ViewModel != null)
        {
            this.ViewModel.PropertyChanged -= this.OnViewModelPropertyChanged;
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(SceneEditorViewModel.CurrentLayout), System.StringComparison.Ordinal))
        {
            this.RebuildLayout();
        }
    }

    private void RebuildLayout()
    {
        if (this.ViewModel == null)
        {
            return;
        }

        this.LayoutGrid.Children.Clear();
        this.LayoutGrid.RowDefinitions.Clear();
        this.LayoutGrid.ColumnDefinitions.Clear();

        var (rows, cols) = SceneLayoutHelpers.GetGridDimensions(this.ViewModel.CurrentLayout);

        for (var i = 0; i < rows; i++)
        {
            this.LayoutGrid.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star) });
        }

        for (var i = 0; i < cols; i++)
        {
            this.LayoutGrid.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });
        }

        var viewports = this.ViewModel.Viewports;
        for (var i = 0; i < viewports.Count; i++)
        {
            var viewportVm = viewports[i];
            var viewportControl = new Viewport { ViewModel = viewportVm };

            var row = i / cols;
            var col = i % cols;

            Grid.SetRow(viewportControl, row);
            Grid.SetColumn(viewportControl, col);

            this.LayoutGrid.Children.Add(viewportControl);
        }
    }
}
