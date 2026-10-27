// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Viewports;

namespace Oxygen.Editor.ModelEditor;

/// <summary>The view of a model document: the toolbar, the viewport that shows the model, and the import settings
/// panel beside it.</summary>
/// <remarks>
/// The viewport control holds an engine view, so the view makes it while it is in the window and disposes it once it
/// leaves; the viewport's view model keeps the camera across.
/// </remarks>
[ViewModel(typeof(ModelEditorViewModel))]
public sealed partial class ModelEditorView : UserControl, IDocumentEditorView
{
    private ModelEditorViewModel? observed;
    private Viewport? viewport;
    private bool isActive;

    /// <summary>Initializes a new instance of the <see cref="ModelEditorView"/> class.</summary>
    public ModelEditorView()
    {
        this.InitializeComponent();
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.ViewModelChanged += (_, _) =>
        {
            this.Observe(this.ViewModel);
            this.Bindings.Update();
        };
    }

    /// <summary>Detaches the viewport control and releases its engine view.</summary>
    /// <returns>A task that completes when the control is released.</returns>
    public async Task DeactivateAsync()
    {
        this.isActive = false;
        await this.ReleaseViewportAsync().ConfigureAwait(true);
    }

    /// <summary>Gets the import settings panel's column width: the user's width, or the header alone when collapsed.</summary>
    /// <param name="width">The user's panel width.</param>
    /// <param name="isCollapsed">Whether the panel is collapsed.</param>
    /// <returns>The column width.</returns>
    internal static GridLength PanelWidthOf(double width, bool isCollapsed) => isCollapsed ? GridLength.Auto : new GridLength(width);

    /// <summary>Gets the visibility of a part that hides while a condition holds.</summary>
    /// <param name="condition">The condition.</param>
    /// <returns>Collapsed while the condition holds.</returns>
    internal static Visibility CollapsedWhen(bool condition) => condition ? Visibility.Collapsed : Visibility.Visible;

    // The splitter sizes the panel's column; the width the user leaves is kept for every model document.
    private void OnSettingsPaneSizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (this.ViewModel is { Layout.IsPanelCollapsed: false } model && e.NewSize.Width > 0)
        {
            model.Layout.PanelWidth = e.NewSize.Width;
        }
    }

    private void OnLoaded(object sender, RoutedEventArgs e)
    {
        this.isActive = true;
        this.Observe(this.ViewModel);
        this.ShowViewport();
    }

    // A layout that moves the view raises Unloaded and Loaded in either order: only a view still out of the tree once
    // the layout settles has left, and releases its viewport.
    private void OnUnloaded(object sender, RoutedEventArgs e)
        => _ = this.DispatcherQueue.TryEnqueue(Microsoft.UI.Dispatching.DispatcherQueuePriority.Low, this.DeactivateIfLeft);

    private async void DeactivateIfLeft()
    {
        if (!this.IsLoaded && this.isActive)
        {
            await this.DeactivateAsync().ConfigureAwait(true);
        }
    }

    private void Observe(ModelEditorViewModel? viewModel)
    {
        if (ReferenceEquals(this.observed, viewModel))
        {
            return;
        }

        this.observed?.PropertyChanged -= this.OnViewModelPropertyChanged;
        this.observed = viewModel;
        this.observed?.PropertyChanged += this.OnViewModelPropertyChanged;
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (string.Equals(e.PropertyName, nameof(ModelEditorViewModel.Viewport), StringComparison.Ordinal))
        {
            this.ShowViewport();
        }
    }

    private async void ShowViewport()
    {
        var model = this.ViewModel?.Viewport;
        if (ReferenceEquals(this.viewport?.ViewModel, model) && this.viewport is not null)
        {
            return;
        }

        await this.ReleaseViewportAsync().ConfigureAwait(true);
        if (!this.isActive || model is null)
        {
            return;
        }

        this.viewport = new Viewport { ViewModel = model };
        this.ViewportHost.Children.Add(this.viewport);
    }

    private async Task ReleaseViewportAsync()
    {
        if (this.viewport is not { } control)
        {
            return;
        }

        this.viewport = null;
        _ = this.ViewportHost.Children.Remove(control);
        await control.DisposeAsync().ConfigureAwait(true);
    }
}
