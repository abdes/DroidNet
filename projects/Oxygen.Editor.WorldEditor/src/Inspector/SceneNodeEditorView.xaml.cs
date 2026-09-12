// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
///     Represents the view for editing a scene node in the properties editor.
/// </summary>
[ViewModel(typeof(SceneNodeEditorViewModel))]
public sealed partial class SceneNodeEditorView : UserControl
{
    private SceneNodeEditorViewModel? observedModel;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneNodeEditorView" /> class.
    /// </summary>
    public SceneNodeEditorView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, _) =>
        {
            this.ObserveModel();
            if (this.ViewModel is null)
            {
                return;
            }

            this.Resources["VmToViewConverter"] = this.ViewModel.VmToViewConverter;
        };
        this.Loaded += (_, _) => this.ObserveModel();
        this.Unloaded += (_, _) => this.StopObservingModel();
    }

    private void ObserveModel()
    {
        this.StopObservingModel();
        if (this.IsLoaded && this.ViewModel is { } model)
        {
            this.observedModel = model;
            model.PropertyChanged += this.OnModelPropertyChanged;
        }
    }

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.PropertyChanged -= this.OnModelPropertyChanged;
            this.observedModel = null;
        }
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(SceneNodeEditorViewModel.SelectedComponentType), StringComparison.Ordinal))
        {
            _ = this.PropertyScroll.ChangeView(horizontalOffset: null, verticalOffset: 0, zoomFactor: null, disableAnimation: true);
        }
    }

    private void OnComponentFilterChanged(object sender, RoutedEventArgs args)
    {
        if (sender is not ToggleButton { Tag: InspectorComponentFilter option } button || this.ViewModel is not { } model)
        {
            return;
        }

        if (button.IsChecked == true && model.SelectedComponentType != option.ComponentType)
        {
            model.SelectComponentFilter(option.ComponentType);
        }
        else if (button.IsChecked != true && model.SelectedComponentType == option.ComponentType)
        {
            model.SelectComponentFilter(componentType: null);
        }
    }

    private void OnAllComponentsChanged(object sender, RoutedEventArgs args)
    {
        if (sender is not ToggleButton button || this.ViewModel is not { } model)
        {
            return;
        }

        if (button.IsChecked == true && !model.IsAllComponentsSelected)
        {
            model.SelectComponentFilter(componentType: null);
        }
        else if (button.IsChecked != true && model.IsAllComponentsSelected)
        {
            button.IsChecked = true;
        }
    }

    private void OnAllComponentsRequested(object? sender, EventArgs args) => this.ViewModel?.SelectComponentFilter(componentType: null);

    private void OnComponentsKeyDown(object sender, KeyRoutedEventArgs args)
    {
        if (args.Key == Windows.System.VirtualKey.Delete)
        {
            args.Handled = true;
            if (this.ViewModel?.IsSingleItemSelected == true)
            {
                _ = this.NodeDetails.DeleteSelectedComponent();
            }
        }
        else if (args.Key == Windows.System.VirtualKey.Escape && this.ViewModel?.IsAllComponentsSelected == false)
        {
            this.ViewModel.SelectComponentFilter(componentType: null);
            args.Handled = true;
        }
    }

    private void OnInspectorSizeChanged(object sender, SizeChangedEventArgs args)
    {
        if (this.ComponentScroll is not null)
        {
            var rows = Math.Clamp((int)Math.Floor((args.NewSize.Height - 120) / 32), 1, 4);
            this.ComponentScroll.MaxHeight = rows * 32;
        }
    }

    private async void UndoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused

        var vm = this.ViewModel;
        if (vm is null)
        {
            return;
        }

        var historyKeeper = vm.History;
        if (historyKeeper.UndoStack.Count == 0)
        {
            return;
        }

        args.Handled = true;
        await historyKeeper.UndoAsync().ConfigureAwait(true);
        vm.RefreshPropertyEditorValues();
    }

    private async void RedoInvoked(KeyboardAccelerator sender, KeyboardAcceleratorInvokedEventArgs args)
    {
        _ = sender; // unused

        var vm = this.ViewModel;
        if (vm is null)
        {
            return;
        }

        var historyKeeper = vm.History;
        if (historyKeeper.RedoStack.Count == 0)
        {
            return;
        }

        args.Handled = true;
        await historyKeeper.RedoAsync().ConfigureAwait(true);
        vm.RefreshPropertyEditorValues();
    }
}
