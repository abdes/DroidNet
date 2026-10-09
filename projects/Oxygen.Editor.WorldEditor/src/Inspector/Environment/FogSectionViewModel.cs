// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns height fog and volumetric fog display adapters and their channel edits.</summary>
public sealed partial class FogSectionViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="FogSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    internal FogSectionViewModel(SceneEnvironmentEditOwner owner) => this.EditOwner = owner;

    [ObservableProperty]
    public partial bool Enabled { get; set; }

    [ObservableProperty]
    public partial bool HeightFogEnabled { get; set; }

    [ObservableProperty]
    public partial float Density { get; set; }

    [ObservableProperty]
    public partial float HeightFalloff { get; set; }

    [ObservableProperty]
    public partial float HeightOffsetMeters { get; set; }

    [ObservableProperty]
    public partial float MaxOpacity { get; set; }

    [ObservableProperty]
    public partial float InscatteringLuminanceR { get; set; }

    [ObservableProperty]
    public partial float InscatteringLuminanceG { get; set; }

    [ObservableProperty]
    public partial float InscatteringLuminanceB { get; set; }

    [ObservableProperty]
    public partial float SkyAmbientScaleR { get; set; }

    [ObservableProperty]
    public partial float SkyAmbientScaleG { get; set; }

    [ObservableProperty]
    public partial float SkyAmbientScaleB { get; set; }

    [ObservableProperty]
    public partial float SecondDensity { get; set; }

    [ObservableProperty]
    public partial float SecondHeightFalloff { get; set; }

    [ObservableProperty]
    public partial float SecondHeightOffsetMeters { get; set; }

    [ObservableProperty]
    public partial float StartDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float EndDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float CutoffDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float DirectionalInscatteringLuminanceR { get; set; }

    [ObservableProperty]
    public partial float DirectionalInscatteringLuminanceG { get; set; }

    [ObservableProperty]
    public partial float DirectionalInscatteringLuminanceB { get; set; }

    [ObservableProperty]
    public partial float DirectionalInscatteringExponent { get; set; }

    [ObservableProperty]
    public partial float DirectionalInscatteringStartDistanceMeters { get; set; }

    [ObservableProperty]
    public partial bool VolumetricFogEnabled { get; set; }

    [ObservableProperty]
    public partial float VolumetricScatteringDistribution { get; set; }

    [ObservableProperty]
    public partial float VolumetricAlbedoR { get; set; }

    [ObservableProperty]
    public partial float VolumetricAlbedoG { get; set; }

    [ObservableProperty]
    public partial float VolumetricAlbedoB { get; set; }

    [ObservableProperty]
    public partial float VolumetricEmissiveR { get; set; }

    [ObservableProperty]
    public partial float VolumetricEmissiveG { get; set; }

    [ObservableProperty]
    public partial float VolumetricEmissiveB { get; set; }

    [ObservableProperty]
    public partial float VolumetricExtinctionScale { get; set; }

    [ObservableProperty]
    public partial float VolumetricDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float VolumetricStartDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float VolumetricNearFadeInDistanceMeters { get; set; }

    [ObservableProperty]
    public partial float VolumetricStaticLightingScatteringIntensity { get; set; }

    [ObservableProperty]
    public partial bool OverrideLightColorsWithFogInscattering { get; set; }

    [ObservableProperty]
    public partial bool RenderInMainPass { get; set; }

    [ObservableProperty]
    public partial bool Holdout { get; set; }

    [ObservableProperty]
    public partial bool VisibleInReflectionCaptures { get; set; }

    [ObservableProperty]
    public partial bool VisibleInRealTimeSkyCaptures { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the authored InscatteringLuminance channels as one linear color.</summary>
    public Vector3 InscatteringLuminanceColor => new(this.InscatteringLuminanceR, this.InscatteringLuminanceG, this.InscatteringLuminanceB);

    /// <summary>Gets the authored DirectionalInscatteringLuminance channels as one linear color.</summary>
    public Vector3 DirectionalInscatteringLuminanceColor => new(this.DirectionalInscatteringLuminanceR, this.DirectionalInscatteringLuminanceG, this.DirectionalInscatteringLuminanceB);

    /// <summary>Gets the authored VolumetricAlbedo channels as one linear color.</summary>
    public Vector3 VolumetricAlbedoColor => new(this.VolumetricAlbedoR, this.VolumetricAlbedoG, this.VolumetricAlbedoB);

    /// <summary>Gets the authored VolumetricEmissive channels as one linear color.</summary>
    public Vector3 VolumetricEmissiveColor => new(this.VolumetricEmissiveR, this.VolumetricEmissiveG, this.VolumetricEmissiveB);

    /// <summary>Gets feedback for whether the scene renders fog.</summary>
    public InspectorFieldDiagnostic EnabledDiagnostic => this.Diagnostic(SceneFogFields.Enabled.Id);

    /// <summary>Gets feedback for whether the exponential height fog layers render.</summary>
    public InspectorFieldDiagnostic HeightFogEnabledDiagnostic => this.Diagnostic(SceneFogFields.HeightFogEnabled.Id);

    /// <summary>Gets feedback for the first layer's extinction coefficient at its height offset, per metre.</summary>
    public InspectorFieldDiagnostic DensityDiagnostic => this.Diagnostic(SceneFogFields.Density.Id);

    /// <summary>Gets feedback for how quickly the first layer thins with height, per metre.</summary>
    public InspectorFieldDiagnostic HeightFalloffDiagnostic => this.Diagnostic(SceneFogFields.HeightFalloff.Id);

    /// <summary>Gets feedback for the world height at which the first layer has its authored density.</summary>
    public InspectorFieldDiagnostic HeightOffsetMetersDiagnostic => this.Diagnostic(SceneFogFields.HeightOffsetMeters.Id);

    /// <summary>Gets feedback for the largest fraction of the scene the fog may hide.</summary>
    public InspectorFieldDiagnostic MaxOpacityDiagnostic => this.Diagnostic(SceneFogFields.MaxOpacity.Id);

    /// <summary>Gets feedback for the fog's own inscattered luminance.</summary>
    public InspectorFieldDiagnostic InscatteringLuminanceDiagnostic => this.Diagnostic(SceneFogFields.InscatteringLuminanceRgb.Id);

    /// <summary>Gets feedback for the scale applied to ambient light the sky atmosphere contributes to the fog.</summary>
    public InspectorFieldDiagnostic SkyAmbientScaleDiagnostic => this.Diagnostic(SceneFogFields.SkyAmbientScaleRgb.Id);

    /// <summary>Gets feedback for the second layer's extinction coefficient at its height offset, per metre.</summary>
    public InspectorFieldDiagnostic SecondDensityDiagnostic => this.Diagnostic(SceneFogFields.SecondDensity.Id);

    /// <summary>Gets feedback for how quickly the second layer thins with height, per metre.</summary>
    public InspectorFieldDiagnostic SecondHeightFalloffDiagnostic => this.Diagnostic(SceneFogFields.SecondHeightFalloff.Id);

    /// <summary>Gets feedback for the world height at which the second layer has its authored density.</summary>
    public InspectorFieldDiagnostic SecondHeightOffsetMetersDiagnostic => this.Diagnostic(SceneFogFields.SecondHeightOffsetMeters.Id);

    /// <summary>Gets feedback for the camera distance at which fog begins.</summary>
    public InspectorFieldDiagnostic StartDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.StartDistanceMeters.Id);

    /// <summary>Gets feedback for the horizontal distance beyond which fog stops accumulating; 0 is unlimited.</summary>
    public InspectorFieldDiagnostic EndDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.EndDistanceMeters.Id);

    /// <summary>Gets feedback for the distance beyond which surfaces receive no fog; 0 is unlimited.</summary>
    public InspectorFieldDiagnostic CutoffDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.CutoffDistanceMeters.Id);

    /// <summary>Gets feedback for the inscattered luminance of the lobe toward the primary atmosphere light.</summary>
    public InspectorFieldDiagnostic DirectionalInscatteringLuminanceDiagnostic => this.Diagnostic(SceneFogFields.DirectionalInscatteringLuminanceRgb.Id);

    /// <summary>Gets feedback for the sharpness of the lobe toward the primary atmosphere light.</summary>
    public InspectorFieldDiagnostic DirectionalInscatteringExponentDiagnostic => this.Diagnostic(SceneFogFields.DirectionalInscatteringExponent.Id);

    /// <summary>Gets feedback for the camera distance at which the directional lobe begins.</summary>
    public InspectorFieldDiagnostic DirectionalInscatteringStartDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.DirectionalInscatteringStartDistanceMeters.Id);

    /// <summary>Gets feedback for whether lights scatter through a volumetric fog grid.</summary>
    public InspectorFieldDiagnostic VolumetricFogEnabledDiagnostic => this.Diagnostic(SceneFogFields.VolumetricFogEnabled.Id);

    /// <summary>Gets feedback for the phase anisotropy; positive values scatter forward.</summary>
    public InspectorFieldDiagnostic VolumetricScatteringDistributionDiagnostic => this.Diagnostic(SceneFogFields.VolumetricScatteringDistribution.Id);

    /// <summary>Gets feedback for the fraction of light the volumetric fog scatters rather than absorbs.</summary>
    public InspectorFieldDiagnostic VolumetricAlbedoDiagnostic => this.Diagnostic(SceneFogFields.VolumetricAlbedoRgb.Id);

    /// <summary>Gets feedback for the luminance the volumetric fog emits.</summary>
    public InspectorFieldDiagnostic VolumetricEmissiveDiagnostic => this.Diagnostic(SceneFogFields.VolumetricEmissiveRgb.Id);

    /// <summary>Gets feedback for the scale applied to height fog extinction inside the volumetric grid.</summary>
    public InspectorFieldDiagnostic VolumetricExtinctionScaleDiagnostic => this.Diagnostic(SceneFogFields.VolumetricExtinctionScale.Id);

    /// <summary>Gets feedback for the far end of the volumetric grid; 0 uses the camera far plane.</summary>
    public InspectorFieldDiagnostic VolumetricDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.VolumetricDistanceMeters.Id);

    /// <summary>Gets feedback for the near end of the volumetric grid.</summary>
    public InspectorFieldDiagnostic VolumetricStartDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.VolumetricStartDistanceMeters.Id);

    /// <summary>Gets feedback for the distance over which volumetric fog fades in from its start.</summary>
    public InspectorFieldDiagnostic VolumetricNearFadeInDistanceMetersDiagnostic => this.Diagnostic(SceneFogFields.VolumetricNearFadeInDistanceMeters.Id);

    /// <summary>Gets feedback for the scale applied to sky lighting scattered by the volumetric fog.</summary>
    public InspectorFieldDiagnostic VolumetricStaticLightingScatteringIntensityDiagnostic => this.Diagnostic(SceneFogFields.VolumetricStaticLightingScatteringIntensity.Id);

    /// <summary>Gets feedback for whether lights scatter with the fog's inscattering colors instead of their own.</summary>
    public InspectorFieldDiagnostic OverrideLightColorsWithFogInscatteringDiagnostic => this.Diagnostic(SceneFogFields.OverrideLightColorsWithFogInscattering.Id);

    /// <summary>Gets feedback for whether the fog renders in the main scene pass.</summary>
    public InspectorFieldDiagnostic RenderInMainPassDiagnostic => this.Diagnostic(SceneFogFields.RenderInMainPass.Id);

    /// <summary>Gets feedback for whether the fog cuts out the scene behind it without adding its own color.</summary>
    public InspectorFieldDiagnostic HoldoutDiagnostic => this.Diagnostic(SceneFogFields.Holdout.Id);

    /// <summary>Gets feedback for whether reflection captures include the fog.</summary>
    public InspectorFieldDiagnostic VisibleInReflectionCapturesDiagnostic => this.Diagnostic(SceneFogFields.VisibleInReflectionCaptures.Id);

    /// <summary>Gets feedback for whether sky light captures include the fog.</summary>
    public InspectorFieldDiagnostic VisibleInRealTimeSkyCapturesDiagnostic => this.Diagnostic(SceneFogFields.VisibleInRealTimeSkyCaptures.Id);

    /// <summary>Authors the complete linear color through the scene edit owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetInscatteringLuminanceColor(Vector3 color) => this.EditOwner.Apply(SceneFogFields.InscatteringLuminanceRgb, color);

    /// <summary>Authors the complete linear color through the scene edit owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetDirectionalInscatteringLuminanceColor(Vector3 color) => this.EditOwner.Apply(SceneFogFields.DirectionalInscatteringLuminanceRgb, color);

    /// <summary>Authors the complete linear color through the scene edit owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetVolumetricAlbedoColor(Vector3 color) => this.EditOwner.Apply(SceneFogFields.VolumetricAlbedoRgb, color);

    /// <summary>Authors the complete linear color through the scene edit owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetVolumetricEmissiveColor(Vector3 color) => this.EditOwner.Apply(SceneFogFields.VolumetricEmissiveRgb, color);

    /// <summary>Refreshes display adapters under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored fog snapshot.</param>
    internal void Refresh(FogEnvironmentData value)
    {
        this.Enabled = value.Enabled;
        this.HeightFogEnabled = value.HeightFogEnabled;
        this.Density = value.Density;
        this.HeightFalloff = value.HeightFalloff;
        this.HeightOffsetMeters = value.HeightOffsetMeters;
        this.MaxOpacity = value.MaxOpacity;
        this.InscatteringLuminanceR = value.InscatteringLuminanceRgb.X;
        this.InscatteringLuminanceG = value.InscatteringLuminanceRgb.Y;
        this.InscatteringLuminanceB = value.InscatteringLuminanceRgb.Z;
        this.SkyAmbientScaleR = value.SkyAmbientScaleRgb.X;
        this.SkyAmbientScaleG = value.SkyAmbientScaleRgb.Y;
        this.SkyAmbientScaleB = value.SkyAmbientScaleRgb.Z;
        this.SecondDensity = value.SecondDensity;
        this.SecondHeightFalloff = value.SecondHeightFalloff;
        this.SecondHeightOffsetMeters = value.SecondHeightOffsetMeters;
        this.StartDistanceMeters = value.StartDistanceMeters;
        this.EndDistanceMeters = value.EndDistanceMeters;
        this.CutoffDistanceMeters = value.CutoffDistanceMeters;
        this.DirectionalInscatteringLuminanceR = value.DirectionalInscatteringLuminanceRgb.X;
        this.DirectionalInscatteringLuminanceG = value.DirectionalInscatteringLuminanceRgb.Y;
        this.DirectionalInscatteringLuminanceB = value.DirectionalInscatteringLuminanceRgb.Z;
        this.DirectionalInscatteringExponent = value.DirectionalInscatteringExponent;
        this.DirectionalInscatteringStartDistanceMeters = value.DirectionalInscatteringStartDistanceMeters;
        this.VolumetricFogEnabled = value.VolumetricFogEnabled;
        this.VolumetricScatteringDistribution = value.VolumetricScatteringDistribution;
        this.VolumetricAlbedoR = value.VolumetricAlbedoRgb.X;
        this.VolumetricAlbedoG = value.VolumetricAlbedoRgb.Y;
        this.VolumetricAlbedoB = value.VolumetricAlbedoRgb.Z;
        this.VolumetricEmissiveR = value.VolumetricEmissiveRgb.X;
        this.VolumetricEmissiveG = value.VolumetricEmissiveRgb.Y;
        this.VolumetricEmissiveB = value.VolumetricEmissiveRgb.Z;
        this.VolumetricExtinctionScale = value.VolumetricExtinctionScale;
        this.VolumetricDistanceMeters = value.VolumetricDistanceMeters;
        this.VolumetricStartDistanceMeters = value.VolumetricStartDistanceMeters;
        this.VolumetricNearFadeInDistanceMeters = value.VolumetricNearFadeInDistanceMeters;
        this.VolumetricStaticLightingScatteringIntensity = value.VolumetricStaticLightingScatteringIntensity;
        this.OverrideLightColorsWithFogInscattering = value.OverrideLightColorsWithFogInscattering;
        this.RenderInMainPass = value.RenderInMainPass;
        this.Holdout = value.Holdout;
        this.VisibleInReflectionCaptures = value.VisibleInReflectionCaptures;
        this.VisibleInRealTimeSkyCaptures = value.VisibleInRealTimeSkyCaptures;
        this.OnPropertyChanged(nameof(this.InscatteringLuminanceColor));
        this.OnPropertyChanged(nameof(this.DirectionalInscatteringLuminanceColor));
        this.OnPropertyChanged(nameof(this.VolumetricAlbedoColor));
        this.OnPropertyChanged(nameof(this.VolumetricEmissiveColor));
    }

    partial void OnEnabledChanged(bool value) => this.EditOwner.Apply(SceneFogFields.Enabled, value);

    partial void OnHeightFogEnabledChanged(bool value) => this.EditOwner.Apply(SceneFogFields.HeightFogEnabled, value);

    partial void OnDensityChanged(float value) => this.EditOwner.Apply(SceneFogFields.Density, value);

    partial void OnHeightFalloffChanged(float value) => this.EditOwner.Apply(SceneFogFields.HeightFalloff, value);

    partial void OnHeightOffsetMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.HeightOffsetMeters, value);

    partial void OnMaxOpacityChanged(float value) => this.EditOwner.Apply(SceneFogFields.MaxOpacity, value);

    partial void OnInscatteringLuminanceRChanged(float value) => this.ApplyInscatteringLuminance(new(value, this.InscatteringLuminanceG, this.InscatteringLuminanceB));

    partial void OnInscatteringLuminanceGChanged(float value) => this.ApplyInscatteringLuminance(new(this.InscatteringLuminanceR, value, this.InscatteringLuminanceB));

    partial void OnInscatteringLuminanceBChanged(float value) => this.ApplyInscatteringLuminance(new(this.InscatteringLuminanceR, this.InscatteringLuminanceG, value));

    partial void OnSkyAmbientScaleRChanged(float value) => this.EditOwner.Apply(SceneFogFields.SkyAmbientScaleRgb, new Vector3(value, this.SkyAmbientScaleG, this.SkyAmbientScaleB));

    partial void OnSkyAmbientScaleGChanged(float value) => this.EditOwner.Apply(SceneFogFields.SkyAmbientScaleRgb, new Vector3(this.SkyAmbientScaleR, value, this.SkyAmbientScaleB));

    partial void OnSkyAmbientScaleBChanged(float value) => this.EditOwner.Apply(SceneFogFields.SkyAmbientScaleRgb, new Vector3(this.SkyAmbientScaleR, this.SkyAmbientScaleG, value));

    partial void OnSecondDensityChanged(float value) => this.EditOwner.Apply(SceneFogFields.SecondDensity, value);

    partial void OnSecondHeightFalloffChanged(float value) => this.EditOwner.Apply(SceneFogFields.SecondHeightFalloff, value);

    partial void OnSecondHeightOffsetMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.SecondHeightOffsetMeters, value);

    partial void OnStartDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.StartDistanceMeters, value);

    partial void OnEndDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.EndDistanceMeters, value);

    partial void OnCutoffDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.CutoffDistanceMeters, value);

    partial void OnDirectionalInscatteringLuminanceRChanged(float value) => this.ApplyDirectionalInscatteringLuminance(new(value, this.DirectionalInscatteringLuminanceG, this.DirectionalInscatteringLuminanceB));

    partial void OnDirectionalInscatteringLuminanceGChanged(float value) => this.ApplyDirectionalInscatteringLuminance(new(this.DirectionalInscatteringLuminanceR, value, this.DirectionalInscatteringLuminanceB));

    partial void OnDirectionalInscatteringLuminanceBChanged(float value) => this.ApplyDirectionalInscatteringLuminance(new(this.DirectionalInscatteringLuminanceR, this.DirectionalInscatteringLuminanceG, value));

    partial void OnDirectionalInscatteringExponentChanged(float value) => this.EditOwner.Apply(SceneFogFields.DirectionalInscatteringExponent, value);

    partial void OnDirectionalInscatteringStartDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.DirectionalInscatteringStartDistanceMeters, value);

    partial void OnVolumetricFogEnabledChanged(bool value) => this.EditOwner.Apply(SceneFogFields.VolumetricFogEnabled, value);

    partial void OnVolumetricScatteringDistributionChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricScatteringDistribution, value);

    partial void OnVolumetricAlbedoRChanged(float value) => this.ApplyVolumetricAlbedo(new(value, this.VolumetricAlbedoG, this.VolumetricAlbedoB));

    partial void OnVolumetricAlbedoGChanged(float value) => this.ApplyVolumetricAlbedo(new(this.VolumetricAlbedoR, value, this.VolumetricAlbedoB));

    partial void OnVolumetricAlbedoBChanged(float value) => this.ApplyVolumetricAlbedo(new(this.VolumetricAlbedoR, this.VolumetricAlbedoG, value));

    partial void OnVolumetricEmissiveRChanged(float value) => this.ApplyVolumetricEmissive(new(value, this.VolumetricEmissiveG, this.VolumetricEmissiveB));

    partial void OnVolumetricEmissiveGChanged(float value) => this.ApplyVolumetricEmissive(new(this.VolumetricEmissiveR, value, this.VolumetricEmissiveB));

    partial void OnVolumetricEmissiveBChanged(float value) => this.ApplyVolumetricEmissive(new(this.VolumetricEmissiveR, this.VolumetricEmissiveG, value));

    partial void OnVolumetricExtinctionScaleChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricExtinctionScale, value);

    partial void OnVolumetricDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricDistanceMeters, value);

    partial void OnVolumetricStartDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricStartDistanceMeters, value);

    partial void OnVolumetricNearFadeInDistanceMetersChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricNearFadeInDistanceMeters, value);

    partial void OnVolumetricStaticLightingScatteringIntensityChanged(float value) => this.EditOwner.Apply(SceneFogFields.VolumetricStaticLightingScatteringIntensity, value);

    partial void OnOverrideLightColorsWithFogInscatteringChanged(bool value) => this.EditOwner.Apply(SceneFogFields.OverrideLightColorsWithFogInscattering, value);

    partial void OnRenderInMainPassChanged(bool value) => this.EditOwner.Apply(SceneFogFields.RenderInMainPass, value);

    partial void OnHoldoutChanged(bool value) => this.EditOwner.Apply(SceneFogFields.Holdout, value);

    partial void OnVisibleInReflectionCapturesChanged(bool value) => this.EditOwner.Apply(SceneFogFields.VisibleInReflectionCaptures, value);

    partial void OnVisibleInRealTimeSkyCapturesChanged(bool value) => this.EditOwner.Apply(SceneFogFields.VisibleInRealTimeSkyCaptures, value);

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);

    private void ApplyInscatteringLuminance(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.InscatteringLuminanceColor));
        this.EditOwner.Apply(SceneFogFields.InscatteringLuminanceRgb, color);
    }

    private void ApplyDirectionalInscatteringLuminance(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.DirectionalInscatteringLuminanceColor));
        this.EditOwner.Apply(SceneFogFields.DirectionalInscatteringLuminanceRgb, color);
    }

    private void ApplyVolumetricAlbedo(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.VolumetricAlbedoColor));
        this.EditOwner.Apply(SceneFogFields.VolumetricAlbedoRgb, color);
    }

    private void ApplyVolumetricEmissive(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.VolumetricEmissiveColor));
        this.EditOwner.Apply(SceneFogFields.VolumetricEmissiveRgb, color);
    }
}
