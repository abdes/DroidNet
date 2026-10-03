// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Data;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector.Presentation;
using EditorPropertiesExpander = Oxygen.Editor.Controls.PropertiesExpander;
using Expander = Microsoft.UI.Xaml.Controls.Expander;
using InspectorNumberField = Oxygen.Editor.Controls.InspectorNumberField;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene environment inspector view.
/// </summary>
[ViewModel(typeof(EnvironmentViewModel))]
public sealed partial class EnvironmentView
{
    private readonly InspectorSearchModel search = EnvironmentFieldCatalog.Create();
    private readonly Dictionary<string, FrameworkElement> fields = new(StringComparer.Ordinal);
    private readonly Dictionary<string, EditorPropertiesExpander> sections = new(StringComparer.Ordinal);
    private readonly Dictionary<string, Expander> disclosures = new(StringComparer.Ordinal);
    private EnvironmentViewModel? observedModel;
    private InspectorPropertyScope propertyScope;

    /// <summary>
    /// Initializes a new instance of the <see cref="EnvironmentView"/> class.
    /// </summary>
    public EnvironmentView()
    {
        this.InitializeComponent();
        this.RegisterPropertyCards();
        _ = this.ScenePropertySearchBox.RegisterPropertyChangedCallback(TextBox.TextProperty, (_, _) => this.ApplyScenePropertyFilter());
        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
        this.ViewModelChanged += (_, _) => this.ObserveModel();
        this.AerialStartInput.Loaded += (_, _) => this.TryFocusAerialStart();
    }

    private void RegisterPropertyCards()
    {
        this.sections.Add("AtmosphereLights", this.AtmosphereLightsSection);
        this.sections.Add("SkyAtmosphere", this.SkyAtmosphereSection);
        this.sections.Add("Background", this.BackgroundSection);
        this.sections.Add("Exposure", this.ExposureSection);
        this.sections.Add("ToneMapping", this.ToneMappingSection);
        this.sections.Add("ColorGrading", this.ColorGradingSection);
        this.sections.Add("Bloom", this.BloomSection);
        this.disclosures.Add("PlanetGround", this.PlanetGroundDisclosure);
        this.disclosures.Add("Scattering", this.ScatteringDisclosure);
        this.disclosures.Add("AerialPerspective", this.AerialPerspectiveDisclosure);
        this.disclosures.Add("MeteringLimits", this.MeteringLimitsDisclosure);
        this.disclosures.Add("Adaptation", this.AdaptationDisclosure);
        this.disclosures.Add("HistogramCalibration", this.HistogramCalibrationDisclosure);
        this.disclosures.Add("ExposureShaping", this.ExposureShapingDisclosure);
        this.RegisterEnvironmentFields();
        this.RegisterPostProcessingFields();
    }

    private void RegisterEnvironmentFields()
    {
        this.RegisterField(this.SourcesCard, "Sources");
        this.RegisterField(this.AtmosphereEnabledCard, "AtmosphereEnabled");
        this.RegisterField(this.SunDiskEnabledCard, "SunDiskEnabled");
        this.RegisterField(this.SkyLuminanceCard, "SkyLuminance");
        this.RegisterField(this.PlanetRadiusKmCard, "PlanetRadiusKm");
        this.RegisterField(this.AtmosphereHeightKmCard, "AtmosphereHeightKm");
        this.RegisterField(this.GroundAlbedoCard, "GroundAlbedo");
        this.RegisterField(this.RayleighScaleHeightKmCard, "RayleighScaleHeightKm");
        this.RegisterField(this.MieScaleHeightKmCard, "MieScaleHeightKm");
        this.RegisterField(this.MieAnisotropyCard, "MieAnisotropy");
        this.RegisterField(this.AerialPerspectiveDistanceScaleCard, "AerialPerspectiveDistanceScale");
        this.RegisterField(this.AerialScatteringStrengthCard, "AerialScatteringStrength");
        this.RegisterField(this.AerialStartCard, "AerialPerspectiveStartDepthMeters");
        this.RegisterField(this.HeightFogContributionCard, "HeightFogContribution");
        this.RegisterField(this.BackgroundColorCard, "BackgroundColor");
    }

    private void RegisterPostProcessingFields()
    {
        this.RegisterField(this.ExposureEnabledCard, "ExposureEnabled");
        this.RegisterField(this.ExposureModeCard, "ExposureMode");
        this.RegisterField(this.ManualExposureEvCard, "ManualExposureEv");
        this.RegisterField(this.ExposureCompensationCard, "ExposureCompensation");
        this.RegisterField(this.AutoExposureMeteringModeCard, "AutoExposureMeteringMode", this.AutoExposureMeteringModeApplicabilityNote);
        this.RegisterField(this.AutoExposureMinEvCard, "AutoExposureMinEv");
        this.RegisterField(this.AutoExposureMaxEvCard, "AutoExposureMaxEv");
        this.RegisterField(this.AutoExposureTargetLuminanceCard, "AutoExposureTargetLuminance");
        this.RegisterField(this.AutoExposureSpotMeterRadiusCard, "AutoExposureSpotMeterRadius");
        this.RegisterField(this.AutoExposureSpeedUpCard, "AutoExposureSpeedUp");
        this.RegisterField(this.AutoExposureSpeedDownCard, "AutoExposureSpeedDown");
        this.RegisterField(this.AutoExposureTransitionDistanceEvCard, "AutoExposureTransitionDistanceEv");
        this.RegisterField(this.ExposureKeyCard, "ExposureKey");
        this.RegisterField(this.AutoExposureLowPercentileCard, "AutoExposureLowPercentile");
        this.RegisterField(this.AutoExposureHighPercentileCard, "AutoExposureHighPercentile");
        this.RegisterField(this.AutoExposureMinLogLuminanceCard, "AutoExposureMinLogLuminance");
        this.RegisterField(this.AutoExposureLogLuminanceRangeCard, "AutoExposureLogLuminanceRange");
        this.RegisterField(this.AutoExposureBlackInfluenceCard, "AutoExposureBlackInfluence");
        this.RegisterField(this.AutoExposureMeteringMaskCard, "AutoExposureMeteringMask", this.AutoExposureMeteringMaskApplicabilityNote);
        this.RegisterField(this.AutoExposureCompensationCurveCard, "AutoExposureCompensationCurve", this.AutoExposureCompensationCurveApplicabilityNote);
        this.RegisterField(this.ToneMapperCard, "ToneMapper");
        this.RegisterField(this.DisplayGammaCard, "DisplayGamma");
        this.RegisterField(this.SaturationCard, "Saturation");
        this.RegisterField(this.ContrastCard, "Contrast");
        this.RegisterField(this.VignetteIntensityCard, "VignetteIntensity");
        this.RegisterField(this.BloomIntensityCard, "BloomIntensity");
        this.RegisterField(this.BloomThresholdCard, "BloomThreshold");
    }

    private void RegisterField(FrameworkElement card, string key, TextBlock? note = null)
    {
        var field = this.search.Fields[key];
        this.fields.Add(key, card);
        card.SetBinding(VisibilityProperty, new Binding
        {
            Source = field,
            Path = new PropertyPath(nameof(InspectorFieldPresentation.IsVisible)),
            Mode = BindingMode.OneWay,
            Converter = (IValueConverter)this.Resources["InspectorDiagnosticVisibility"],
        });
        if (card is InspectorNumberField numeric)
        {
            numeric.SetBinding(InspectorNumberField.ApplicabilityTextProperty, new Binding
            {
                Source = field,
                Path = new PropertyPath(nameof(InspectorFieldPresentation.ApplicabilityText)),
                Mode = BindingMode.OneWay,
            });
        }

        if (note is null)
        {
            return;
        }

        note.SetBinding(TextBlock.TextProperty, new Binding
        {
            Source = field,
            Path = new PropertyPath(nameof(InspectorFieldPresentation.ApplicabilityText)),
            Mode = BindingMode.OneWay,
        });
        note.SetBinding(VisibilityProperty, new Binding
        {
            Source = field,
            Path = new PropertyPath(nameof(InspectorFieldPresentation.HasApplicabilityText)),
            Mode = BindingMode.OneWay,
            Converter = (IValueConverter)this.Resources["InspectorDiagnosticVisibility"],
        });
    }

    private void OnLoaded(object sender, RoutedEventArgs args)
        => this.ObserveModel();

    private void ObserveModel()
    {
        this.StopObservingModel();
        if (!this.IsLoaded)
        {
            return;
        }

        this.observedModel = this.ViewModel;
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested += this.OnFieldFocusRequested;
            model.PropertyChanged += this.OnEnvironmentPropertyChanged;
            this.FocusPendingField();
            this.ApplyScenePropertyFilter();
        }
    }

    private void OnUnloaded(object sender, RoutedEventArgs args)
        => this.StopObservingModel();

    private void StopObservingModel()
    {
        if (this.observedModel is { } model)
        {
            model.FieldFocusRequested -= this.OnFieldFocusRequested;
            model.PropertyChanged -= this.OnEnvironmentPropertyChanged;
            this.observedModel = null;
        }
    }

    private void OnEnvironmentPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (args.PropertyName is nameof(EnvironmentViewModel.ExposureMode) or nameof(EnvironmentViewModel.ToneMapping) or nameof(EnvironmentViewModel.AutoExposureMeteringMode))
        {
            this.ApplyScenePropertyFilter();
        }
    }

    private void ClearScenePropertySearch_Click(object sender, RoutedEventArgs args)
        => this.ScenePropertySearchBox.Text = string.Empty;

    private async void ResetAtmosphereSources_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetAtmosphereSourcesAsync().ConfigureAwait(true);
        }
    }

    private async void ResetBackground_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.ResetBackgroundAsync().ConfigureAwait(true);
        }
    }

    private void ClearMeteringMask_Click(object sender, RoutedEventArgs args)
        => this.ViewModel?.SetMeteringMask(null);

    private void MeteringMaskItem_Click(object sender, RoutedEventArgs args)
    {
        if (sender is FrameworkElement { DataContext: AssetPickerRow row } && row.Item.IsEnabled)
        {
            this.ViewModel?.SetMeteringMask(row.Item.Uri);
            this.MeteringMaskFlyout.Hide();
        }
    }

    private async void InspectPrimarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSun).ConfigureAwait(true);
        }
    }

    private async void InspectSecondarySource_Click(object sender, RoutedEventArgs args)
    {
        if (this.ViewModel is { } model)
        {
            await model.InspectAtmosphereSourceAsync(model.SelectedSecondarySun).ConfigureAwait(true);
        }
    }

    private void ScenePropertyScope_Changed(object sender, SelectionChangedEventArgs args)
    {
        if (sender is CommunityToolkit.WinUI.Controls.Segmented selector)
        {
            this.propertyScope = selector.SelectedIndex switch
            {
                1 => InspectorPropertyScope.Environment,
                2 => InspectorPropertyScope.PostProcessing,
                _ => InspectorPropertyScope.All,
            };
            this.ApplyScenePropertyFilter();
        }
    }

    private void ApplyScenePropertyFilter()
    {
        if (this.ViewModel is not { } model || this.ScenePropertySearchBox is null || this.fields.Count == 0)
        {
            return;
        }

        foreach (var (key, section) in this.sections)
        {
            this.search.RecordExpansion(key, section.IsExpanded);
        }

        foreach (var (key, disclosure) in this.disclosures)
        {
            this.search.RecordExpansion(key, disclosure.IsExpanded);
        }

        this.search.RecordExpansion("PrimarySource", this.PrimarySourceDisclosure.IsExpanded);
        this.search.RecordExpansion("SecondarySource", this.SecondarySourceDisclosure.IsExpanded);
        this.search.Update(this.ScenePropertySearchBox.Text, this.propertyScope, model.ExposureMode, model.AutoExposureMeteringMode, model.ToneMapping);
        foreach (var (key, section) in this.sections)
        {
            section.Visibility = this.search.IsGroupVisible(key) ? Visibility.Visible : Visibility.Collapsed;
            section.IsExpanded = this.search.IsExpanded(key);
        }

        foreach (var (key, disclosure) in this.disclosures)
        {
            disclosure.Visibility = this.search.IsGroupVisible(key) ? Visibility.Visible : Visibility.Collapsed;
            disclosure.IsExpanded = this.search.IsExpanded(key);
        }

        var revealSources = this.search.IsSearching && this.search.IsGroupVisible("AtmosphereLights");
        this.PrimarySourceDisclosure.IsExpanded = revealSources || this.search.IsExpanded("PrimarySource");
        this.SecondarySourceDisclosure.IsExpanded = revealSources || this.search.IsExpanded("SecondarySource");
        this.EnvironmentHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.Environment) ? Visibility.Visible : Visibility.Collapsed;
        this.PostProcessingHeading.Visibility = this.search.IsScopeVisible(InspectorPropertyScope.PostProcessing) ? Visibility.Visible : Visibility.Collapsed;
        this.ClearPropertySearchButton.Visibility = this.search.IsSearching ? Visibility.Visible : Visibility.Collapsed;
        this.NoScenePropertyMatches.Visibility = this.search.HasNoMatches ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OnFieldFocusRequested(object? sender, EventArgs args) => this.FocusPendingField();

    private void FocusPendingField()
    {
        if (!string.Equals(this.observedModel?.PendingFieldFocus, Oxygen.Editor.Schemas.SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            return;
        }

        this.ScenePropertySearchBox.Text = string.Empty;
        this.ScenePropertyScopeSelector.SelectedIndex = 0;
        this.ApplyScenePropertyFilter();
        this.AerialPerspectiveDisclosure.IsExpanded = true;
        _ = this.SkyAtmosphereSection.BringItemIntoView(this.AerialPerspectiveDisclosure);
        this.TryFocusAerialStart();
        _ = this.DispatcherQueue.TryEnqueue(this.TryFocusAerialStart);
    }

    private void TryFocusAerialStart()
    {
        if (this.IsLoaded && this.AerialStartInput.IsLoaded && this.observedModel is { } model
            && string.Equals(model.PendingFieldFocus, Oxygen.Editor.Schemas.SceneEnvironmentConstraints.AerialStartPropertyPath, StringComparison.Ordinal))
        {
            this.AerialStartInput.StartBringIntoView();
            if (this.AerialStartInput.Focus(FocusState.Programmatic))
            {
                model.AcknowledgeFieldFocus();
            }
        }
    }

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        if (args.Owner is EnvironmentViewModel model && sender is FrameworkElement { Tag: string field })
        {
            if (string.Equals(field, "GroundAlbedo", StringComparison.Ordinal))
            {
                model.SetGroundAlbedoColor(args.Color);
            }
            else if (string.Equals(field, "BackgroundColor", StringComparison.Ordinal))
            {
                model.SetBackgroundColor(args.Color);
            }
        }
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(args);

    private void AerialStartValidate(object? sender, ValidationEventArgs<float> args)
        => args.IsValid = this.ViewModel?.ValidateAerialStart(args.NewValue) == true;

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.BeginEditSession($"{field}.{args.Component}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args)
        => this.ViewModel?.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
