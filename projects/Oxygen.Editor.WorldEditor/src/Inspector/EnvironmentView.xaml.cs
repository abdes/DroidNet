// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Environment;
using Oxygen.Editor.World.Inspector.Presentation;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Composes section-owned views and orchestrates scene browsing and diagnostic navigation.</summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    private readonly InspectorSearchModel search = EnvironmentFieldCatalog.Create();
    private readonly InspectorSceneFieldRegistry registry;
    private EnvironmentViewModel? observedModel;
    private InspectorPropertyScope propertyScope;

    /// <summary>Initializes a new instance of the <see cref="EnvironmentView"/> class.</summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
        this.registry = new(this.search, (IValueConverter)this.Resources["InspectorDiagnosticVisibility"]);
        this.AtmosphereLightsView.Register(this.registry);
        this.SkyAtmosphereView.Register(this.registry);
        this.BackgroundView.Register(this.registry);
        this.ExposureView.Register(this.registry);
        this.PostProcessingView.Register(this.registry);
        _ = this.ScenePropertySearchBox.RegisterPropertyChangedCallback(TextBox.TextProperty, (_, _) => this.ApplyScenePropertyFilter());
        this.Loaded += (_, _) => this.ObserveModel();
        this.Unloaded += (_, _) => this.StopObservingModel();
        this.ViewModelChanged += (_, _) => this.ObserveModel();
        this.SkyAtmosphereView.FieldFocused += (_, _) =>
        {
            if (this.observedModel is { } model && string.Equals(model.PendingFieldFocus, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
            {
                model.AcknowledgeFieldFocus();
            }
        };
    }

    /// <summary>Resolves an explicitly registered canonical field across section boundaries.</summary>
    /// <param name="key">The stable presentation identity.</param>
    /// <returns>The original field root.</returns>
    internal FrameworkElement Field(string key) => this.registry.Fields[key];

    /// <summary>Resolves an explicitly registered section.</summary>
    /// <param name="key">The stable section identity.</param>
    /// <returns>The original native disclosure.</returns>
    internal Oxygen.Editor.Controls.PropertiesExpander Section(string key) => this.registry.Sections[key];

    /// <summary>Resolves the view owning a section's namescope.</summary>
    /// <param name="key">The stable section identity.</param>
    /// <returns>The owning composed view.</returns>
    internal UserControl SectionView(string key) => this.registry.SectionViews[key];

    private void ObserveModel()
    {
        this.StopObservingModel();
        if (this.IsLoaded && this.ViewModel is { } model)
        {
            this.observedModel = model;
            model.FieldFocusRequested += this.OnFieldFocusRequested;
            model.Exposure.PropertyChanged += this.OnSectionPropertyChanged;
            model.PostProcessing.PropertyChanged += this.OnSectionPropertyChanged;
            model.SceneReferences.PropertyChanged += this.OnSectionPropertyChanged;
            this.ApplyScenePropertyFilter();
            this.FocusPendingField();
        }
    }

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested -= this.OnFieldFocusRequested;
            model.Exposure.PropertyChanged -= this.OnSectionPropertyChanged;
            model.PostProcessing.PropertyChanged -= this.OnSectionPropertyChanged;
            model.SceneReferences.PropertyChanged -= this.OnSectionPropertyChanged;
            this.observedModel = null;
        }
    }

    private void OnSectionPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.observedModel?.SceneReferences))
        {
            this.ApplyScenePropertyFilter();
            return;
        }

        if (args.PropertyName is nameof(ExposureSectionViewModel.ExposureMode)
            or nameof(ExposureSectionViewModel.AutoExposureMeteringMode) or nameof(PostProcessingSectionViewModel.ToneMapping))
        {
            this.ApplyScenePropertyFilter();
        }
    }

    private void ClearScenePropertySearch_Click(object sender, RoutedEventArgs args) => this.ScenePropertySearchBox.Text = string.Empty;

    private void ScenePropertyScope_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (sender is CommunityToolkit.WinUI.Controls.Segmented selector)
        {
            this.propertyScope = selector.SelectedIndex switch
            {
                1 => InspectorPropertyScope.Environment,
                2 => InspectorPropertyScope.PostProcessing,
                3 => InspectorPropertyScope.SceneReferences,
                _ => InspectorPropertyScope.All,
            };
            this.ApplyScenePropertyFilter();
        }
    }

    private void ApplyScenePropertyFilter()
    {
        if (this.ViewModel is not { } model || this.registry is null)
        {
            return;
        }

        this.registry.Apply(this.ScenePropertySearchBox.Text, this.propertyScope, model.Exposure.ExposureMode, model.Exposure.AutoExposureMeteringMode, model.PostProcessing.ToneMapping);
        this.EnvironmentHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.Environment) ? Visibility.Visible : Visibility.Collapsed;
        this.PostProcessingHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.PostProcessing) ? Visibility.Visible : Visibility.Collapsed;
        var showReferences = (this.propertyScope is InspectorPropertyScope.All or InspectorPropertyScope.SceneReferences)
            && model.SceneReferences.MatchesSearch(this.ScenePropertySearchBox.Text);
        this.SceneReferencesView.Visibility = showReferences ? Visibility.Visible : Visibility.Collapsed;
        this.ClearPropertySearchButton.Visibility = this.search.IsSearching ? Visibility.Visible : Visibility.Collapsed;
        this.NoScenePropertyMatches.Visibility = this.search.HasNoMatches && !showReferences ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnFieldFocusRequested(object? sender, EventArgs args) => this.FocusPendingField();

    private void FocusPendingField()
    {
        if (string.Equals(this.observedModel?.PendingFieldFocus, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            this.ScenePropertySearchBox.Text = string.Empty;
            this.ScenePropertyScopeSelector.SelectedIndex = 0;
            this.ApplyScenePropertyFilter();
            this.SkyAtmosphereView.FocusAerialStart();
        }
    }
}
