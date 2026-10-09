// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Oxygen.Editor.World.Inspector.Controls;
using Oxygen.Editor.World.Inspector.Presentation;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns fog composition and its numeric and color adapters.</summary>
[ViewModel(typeof(FogSectionViewModel))]
public sealed partial class FogSectionView
{
    /// <summary>Initializes a new instance of the <see cref="FogSectionView"/> class.</summary>
    public FogSectionView() => this.InitializeComponent();

    /// <summary>Registers this view's controls and disclosures by stable browsing identity.</summary>
    /// <param name="registry">The host's presentation registry.</param>
    internal void Register(InspectorSceneFieldRegistry registry)
    {
        registry.Section("Fog", this, this.FogSection);
        registry.Disclosures.Add("SecondLayer", this.SecondLayerDisclosure);
        registry.Disclosures.Add("Distances", this.DistancesDisclosure);
        registry.Disclosures.Add("Directional", this.DirectionalDisclosure);
        registry.Disclosures.Add("Volumetric", this.VolumetricDisclosure);
        registry.Disclosures.Add("Rendering", this.RenderingDisclosure);
        registry.Field("Enabled", this.EnabledCard);
        registry.Field("HeightFogEnabled", this.HeightFogEnabledCard);
        registry.Field("Density", this.DensityCard);
        registry.Field("HeightFalloff", this.HeightFalloffCard);
        registry.Field("HeightOffsetMeters", this.HeightOffsetMetersCard);
        registry.Field("MaxOpacity", this.MaxOpacityCard);
        registry.Field("InscatteringLuminanceRgb", this.InscatteringLuminanceRgbCard);
        registry.Field("SkyAmbientScaleRgb", this.SkyAmbientScaleRgbCard);
        registry.Field("SecondDensity", this.SecondDensityCard);
        registry.Field("SecondHeightFalloff", this.SecondHeightFalloffCard);
        registry.Field("SecondHeightOffsetMeters", this.SecondHeightOffsetMetersCard);
        registry.Field("StartDistanceMeters", this.StartDistanceMetersCard);
        registry.Field("EndDistanceMeters", this.EndDistanceMetersCard);
        registry.Field("CutoffDistanceMeters", this.CutoffDistanceMetersCard);
        registry.Field("DirectionalInscatteringLuminanceRgb", this.DirectionalInscatteringLuminanceRgbCard);
        registry.Field("DirectionalInscatteringExponent", this.DirectionalInscatteringExponentCard);
        registry.Field("DirectionalInscatteringStartDistanceMeters", this.DirectionalInscatteringStartDistanceMetersCard);
        registry.Field("VolumetricFogEnabled", this.VolumetricFogEnabledCard);
        registry.Field("VolumetricScatteringDistribution", this.VolumetricScatteringDistributionCard);
        registry.Field("VolumetricAlbedoRgb", this.VolumetricAlbedoRgbCard);
        registry.Field("VolumetricEmissiveRgb", this.VolumetricEmissiveRgbCard);
        registry.Field("VolumetricExtinctionScale", this.VolumetricExtinctionScaleCard);
        registry.Field("VolumetricDistanceMeters", this.VolumetricDistanceMetersCard);
        registry.Field("VolumetricStartDistanceMeters", this.VolumetricStartDistanceMetersCard);
        registry.Field("VolumetricNearFadeInDistanceMeters", this.VolumetricNearFadeInDistanceMetersCard);
        registry.Field("VolumetricStaticLightingScatteringIntensity", this.VolumetricStaticLightingScatteringIntensityCard);
        registry.Field("OverrideLightColorsWithFogInscattering", this.OverrideLightColorsWithFogInscatteringCard);
        registry.Field("RenderInMainPass", this.RenderInMainPassCard);
        registry.Field("Holdout", this.HoldoutCard);
        registry.Field("VisibleInReflectionCaptures", this.VisibleInReflectionCapturesCard);
        registry.Field("VisibleInRealTimeSkyCaptures", this.VisibleInRealTimeSkyCapturesCard);
    }

    private void OnRgbColorPicked(object? sender, InspectorRgbColorPickedEventArgs args)
    {
        var property = (sender as FrameworkElement)?.Tag switch
        {
            "InscatteringLuminanceRgb" => SceneFogFields.InscatteringLuminanceRgb,
            "DirectionalInscatteringLuminanceRgb" => SceneFogFields.DirectionalInscatteringLuminanceRgb,
            "VolumetricAlbedoRgb" => SceneFogFields.VolumetricAlbedoRgb,
            "VolumetricEmissiveRgb" => SceneFogFields.VolumetricEmissiveRgb,
            _ => null,
        };
        if (property is not null && args.Owner is SceneEnvironmentEditOwner owner)
        {
            owner.Apply(property, args.Color);
        }
    }

    private void NumberEditStarted(object? sender, NumberBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession(field, args.InteractionKind);
        }
    }

    private void NumberEditCompleted(object? sender, NumberBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(args);

    private void VectorEditStarted(object? sender, VectorBoxEditSessionEventArgs args)
    {
        if (sender is FrameworkElement { Tag: string field })
        {
            this.ViewModel?.EditOwner.BeginEditSession($"{field}.{args.Component}", args.InteractionKind);
        }
    }

    private void VectorEditCompleted(object? sender, VectorBoxEditSessionEventArgs args) => this.ViewModel?.EditOwner.CompleteEditSession(new(args.InteractionKind, args.CompletionKind));
}
