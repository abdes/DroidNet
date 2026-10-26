// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Specialized;
using System.ComponentModel;
using CommunityToolkit.WinUI;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Controls.Primitives;
using Microsoft.UI.Xaml.Input;

namespace Oxygen.Editor.SceneInspector;

/// <summary>
///     Represents the view for editing a scene node in the properties editor.
/// </summary>
[ViewModel(typeof(SceneInspectorViewModel))]
public sealed partial class SceneInspectorView : UserControl
{
    private readonly HostedViews sectionViews = new();
    private SceneInspectorViewModel? observedModel;
    private SceneInspectorViewModel? sectionsOwner;

    /// <summary>
    ///     Initializes a new instance of the <see cref="SceneInspectorView" /> class.
    /// </summary>
    public SceneInspectorView()
    {
        this.InitializeComponent();

        this.ViewModelChanged += (_, _) =>
        {
            this.ObserveModel();
            this.HostSections();
        };
        this.Loaded += (_, _) => this.ObserveModel();
        this.Unloaded += (_, _) => this.StopObservingModel();
    }

    // A section's fields take input only while the selection can be authored: a locked node, or one an instance brings
    // in, shows its values disabled.
    private static void EnableInput(UIElement view, bool enabled)
    {
        if (view is Control control)
        {
            control.IsEnabled = enabled;
        }
    }

    /// <summary>
    ///     Follows the sections of the current model. The model keeps its sections for its whole life and only hides the
    ///     ones that do not apply, so a section's view is kept while hidden and shown again when the section returns; the
    ///     views of a previous model's sections are released.
    /// </summary>
    private void HostSections()
    {
        this.sectionsOwner?.PropertyEditors.CollectionChanged -= this.OnPropertyEditorsChanged;
        this.sectionsOwner = this.ViewModel;
        this.sectionsOwner?.PropertyEditors.CollectionChanged += this.OnPropertyEditorsChanged;

        this.ShowSections();
        this.sectionViews.RetainOnly(this.sectionsOwner?.PropertyEditors ?? []);
    }

    private void OnPropertyEditorsChanged(object? sender, NotifyCollectionChangedEventArgs args) => this.ShowSections();

    /// <summary>Puts the views of the shown sections in the panel, in the model's order.</summary>
    private void ShowSections()
    {
        var children = this.PropertySections.Children;
        var converter = this.sectionsOwner?.VmToViewConverter;
        var index = 0;
        foreach (var section in this.sectionsOwner?.PropertyEditors ?? [])
        {
            if (this.sectionViews.GetOrCreate(section, converter) is not UIElement view)
            {
                continue;
            }

            EnableInput(view, this.sectionsOwner?.CanEditSelectedNodes ?? false);

            var current = children.IndexOf(view);
            if (current != index)
            {
                if (current >= 0)
                {
                    children.RemoveAt(current);
                }

                children.Insert(index, view);
            }

            ++index;
        }

        while (children.Count > index)
        {
            children.RemoveAt(children.Count - 1);
        }
    }

    private void ObserveModel()
    {
        this.StopObservingModel();
        if (this.IsLoaded && this.ViewModel is { } model)
        {
            this.observedModel = model;
            model.PropertyChanged += this.OnModelPropertyChanged;
            model.ComponentFilterChanging += this.OnComponentFilterChanging;
        }
    }

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.PropertyChanged -= this.OnModelPropertyChanged;
            model.ComponentFilterChanging -= this.OnComponentFilterChanging;
            this.observedModel = null;
        }
    }

    private void OnModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.Equals(args.PropertyName, nameof(SceneInspectorViewModel.SelectedComponentType), StringComparison.Ordinal))
        {
            _ = this.PropertyScroll.ChangeView(horizontalOffset: null, verticalOffset: 0, zoomFactor: null, disableAnimation: true);
        }
        else if (string.Equals(args.PropertyName, nameof(SceneInspectorViewModel.CanEditSelectedNodes), StringComparison.Ordinal)
            && sender is SceneInspectorViewModel model)
        {
            foreach (var view in this.PropertySections.Children)
            {
                EnableInput(view, model.CanEditSelectedNodes);
            }
        }
    }

    private void OnComponentFilterChanging(object? sender, EventArgs args)
    {
        foreach (var number in this.PropertySections.FindDescendants().OfType<DroidNet.Controls.NumberBox>())
        {
            number.CompletePendingTextEdit();
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
}
