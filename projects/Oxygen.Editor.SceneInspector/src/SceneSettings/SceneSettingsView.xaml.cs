// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.SceneInspector.Presentation;
using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Composes section-owned views and orchestrates scene browsing and diagnostic navigation.</summary>
[ViewModel(typeof(SceneSettingsViewModel))]
public sealed partial class SceneSettingsView
{
    private readonly InspectorSearchModel search = EnvironmentFieldCatalog.Create();
    private readonly InspectorSceneFieldRegistry registry;
    private SceneSettingsViewModel? observedModel;
    private InspectorPropertyScope propertyScope;

    /// <summary>Initializes a new instance of the <see cref="SceneSettingsView"/> class.</summary>
    public SceneSettingsView()
    {
        this.InitializeComponent();
        this.registry = new(this.search, (IValueConverter)this.Resources["InspectorDiagnosticVisibility"]);
        this.AtmosphereLightsView.Register(this.registry);
        this.BackdropView.Register(this.registry);
        this.SkyAtmosphereView.Register(this.registry);
        this.SkyLightView.Register(this.registry);
        this.FogView.Register(this.registry);
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
            model.PropertyChanged += this.OnSectionPropertyChanged;
            model.Exposure.PropertyChanged += this.OnSectionPropertyChanged;
            model.PostProcessing.PropertyChanged += this.OnSectionPropertyChanged;
            model.Backdrop.PropertyChanged += this.OnSectionPropertyChanged;
            model.SkyLight.PropertyChanged += this.OnSectionPropertyChanged;
            model.SceneReferences.PropertyChanged += this.OnSectionPropertyChanged;
            model.Play.PropertyChanged += this.OnSectionPropertyChanged;
            this.ShowApplicableScope();
            this.FocusPendingField();
        }
    }

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested -= this.OnFieldFocusRequested;
            model.PropertyChanged -= this.OnSectionPropertyChanged;
            model.Exposure.PropertyChanged -= this.OnSectionPropertyChanged;
            model.PostProcessing.PropertyChanged -= this.OnSectionPropertyChanged;
            model.Backdrop.PropertyChanged -= this.OnSectionPropertyChanged;
            model.SkyLight.PropertyChanged -= this.OnSectionPropertyChanged;
            model.SceneReferences.PropertyChanged -= this.OnSectionPropertyChanged;
            model.Play.PropertyChanged -= this.OnSectionPropertyChanged;
            this.observedModel = null;
        }
    }

    private void OnSectionPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (ReferenceEquals(sender, this.observedModel) && args.PropertyName is nameof(SceneSettingsViewModel.HasSceneOwnSettings))
        {
            this.ShowApplicableScope();
            return;
        }

        if (ReferenceEquals(sender, this.observedModel?.SceneReferences) || ReferenceEquals(sender, this.observedModel?.Play))
        {
            this.ApplyScenePropertyFilter();
            return;
        }

        if (args.PropertyName is nameof(ExposureSectionViewModel.ExposureMode)
            or nameof(ExposureSectionViewModel.AutoExposureMeteringMode) or nameof(PostProcessingSectionViewModel.ToneMapping)
            or nameof(BackdropSectionViewModel.Backdrop) or nameof(BackdropSectionViewModel.SolidColorLightsScene)
            or nameof(SkyLightSectionViewModel.Source))
        {
            this.ApplyScenePropertyFilter();
        }
    }

    private void ClearScenePropertySearch_Click(object sender, RoutedEventArgs args)
    {
        _ = sender;
        _ = args;

        this.ScenePropertySearchBox.Text = string.Empty;
    }

    private void ScenePropertyScope_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (sender is CommunityToolkit.WinUI.Controls.Segmented selector)
        {
            this.propertyScope = selector.SelectedIndex switch
            {
                1 => InspectorPropertyScope.Play,
                2 => InspectorPropertyScope.Environment,
                3 => InspectorPropertyScope.PostProcessing,
                4 => InspectorPropertyScope.SceneReferences,
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

        var modes = new InspectorApplicabilityContext(
            model.Exposure.ExposureMode,
            model.Exposure.AutoExposureMeteringMode,
            model.PostProcessing.ToneMapping,
            model.Backdrop.Backdrop,
            model.Backdrop.SolidColorLightsScene,
            model.SkyLight.Source);
        this.registry.Apply(this.ScenePropertySearchBox.Text, this.propertyScope, modes);
        this.EnvironmentHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.Environment) ? Visibility.Visible : Visibility.Collapsed;
        this.PostProcessingHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.PostProcessing) ? Visibility.Visible : Visibility.Collapsed;
        // A model's preview has no Play settings or references of its own: their sections never show.
        var showReferences = model.HasSceneOwnSettings
            && (this.propertyScope is InspectorPropertyScope.All or InspectorPropertyScope.SceneReferences)
            && model.SceneReferences.MatchesSearch(this.ScenePropertySearchBox.Text);
        this.SceneReferencesView.Visibility = showReferences ? Visibility.Visible : Visibility.Collapsed;
        var showPlay = model.HasSceneOwnSettings
            && (this.propertyScope is InspectorPropertyScope.All or InspectorPropertyScope.Play)
            && model.Play.MatchesSearch(this.ScenePropertySearchBox.Text);
        this.PlayView.Visibility = showPlay ? Visibility.Visible : Visibility.Collapsed;
        this.ClearPropertySearchButton.Visibility = this.search.IsSearching ? Visibility.Visible : Visibility.Collapsed;
        this.NoScenePropertyMatches.Visibility = this.search.HasNoMatches && !showReferences && !showPlay ? Visibility.Visible : Visibility.Collapsed;
    }

    // The Play and References scopes hide on a model's preview, which has neither: a scope that hides while selected
    // gives way to All, whose selection applies the filter.
    private void ShowApplicableScope()
    {
        if (this.observedModel is { HasSceneOwnSettings: false }
            && this.propertyScope is InspectorPropertyScope.Play or InspectorPropertyScope.SceneReferences)
        {
            this.ScenePropertyScopeSelector.SelectedIndex = 0;
            return;
        }

        this.ApplyScenePropertyFilter();
    }

    private void OnFieldFocusRequested(object? sender, EventArgs args) => this.FocusPendingField();

    private void FocusPendingField()
    {
        if (this.observedModel is not { PendingFieldFocus: { } property } model)
        {
            return;
        }

        if (string.Equals(property, SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            this.ShowAllSceneProperties();
            this.SkyAtmosphereView.FocusAerialStart();
        }
        else if (SceneStartingPropertyPaths.IsStartingProperty(property))
        {
            this.ShowAllSceneProperties();
            if (this.PlayView.FocusField(property))
            {
                model.AcknowledgeFieldFocus();
            }
        }
    }

    private void ShowAllSceneProperties()
    {
        this.ScenePropertySearchBox.Text = string.Empty;
        this.ScenePropertyScopeSelector.SelectedIndex = 0;
        this.ApplyScenePropertyFilter();
    }
}
