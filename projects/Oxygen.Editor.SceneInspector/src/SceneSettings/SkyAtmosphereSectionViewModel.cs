// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Owns atmosphere display units, channel edits and Aerial Start validation.</summary>
public sealed partial class SkyAtmosphereSectionViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="SkyAtmosphereSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    internal SkyAtmosphereSectionViewModel(SceneEnvironmentEditOwner owner) => this.EditOwner = owner;

    [ObservableProperty]
    public partial bool SunDiskEnabled { get; set; }

    [ObservableProperty]
    public partial double PlanetRadiusKm { get; set; }

    [ObservableProperty]
    public partial double AtmosphereHeightKm { get; set; }

    [ObservableProperty]
    public partial double GroundAlbedoR { get; set; }

    [ObservableProperty]
    public partial double GroundAlbedoG { get; set; }

    [ObservableProperty]
    public partial double GroundAlbedoB { get; set; }

    [ObservableProperty]
    public partial double RayleighScaleHeightKm { get; set; }

    [ObservableProperty]
    public partial double MieScaleHeightKm { get; set; }

    [ObservableProperty]
    public partial double MieAnisotropy { get; set; }

    [ObservableProperty]
    public partial double SkyLuminanceR { get; set; }

    [ObservableProperty]
    public partial double SkyLuminanceG { get; set; }

    [ObservableProperty]
    public partial double SkyLuminanceB { get; set; }

    [ObservableProperty]
    public partial double AerialPerspectiveDistanceScale { get; set; }

    [ObservableProperty]
    public partial double AerialScatteringStrength { get; set; }

    [ObservableProperty]
    public partial double AerialPerspectiveStartDepthMeters { get; set; }

    [ObservableProperty]
    public partial double HeightFogContribution { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets authored linear albedo.</summary>
    public Vector3 GroundAlbedoColor => InspectorValues.ToVector3(this.GroundAlbedoR, this.GroundAlbedoG, this.GroundAlbedoB);

    /// <summary>Gets canonical sun-disk feedback.</summary>
    public InspectorFieldDiagnostic SunDiskEnabledDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.SunDiskEnabled.Id);

    /// <summary>Gets canonical radius feedback.</summary>
    public InspectorFieldDiagnostic PlanetRadiusKmDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.PlanetRadiusMeters.Id);

    /// <summary>Gets canonical height feedback.</summary>
    public InspectorFieldDiagnostic AtmosphereHeightKmDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AtmosphereHeightMeters.Id);

    /// <summary>Gets canonical albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoRDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.GroundAlbedo.Id);

    /// <summary>Gets shared albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoGDiagnostic => this.GroundAlbedoRDiagnostic;

    /// <summary>Gets shared albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoBDiagnostic => this.GroundAlbedoRDiagnostic;

    /// <summary>Gets canonical scattering feedback.</summary>
    public InspectorFieldDiagnostic RayleighScaleHeightKmDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.RayleighScaleHeightMeters.Id);

    /// <summary>Gets canonical scattering feedback.</summary>
    public InspectorFieldDiagnostic MieScaleHeightKmDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.MieScaleHeightMeters.Id);

    /// <summary>Gets canonical phase-function feedback.</summary>
    public InspectorFieldDiagnostic MieAnisotropyDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.MieAnisotropy.Id);

    /// <summary>Gets canonical sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceRDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.SkyLuminance.Id);

    /// <summary>Gets shared sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceGDiagnostic => this.SkyLuminanceRDiagnostic;

    /// <summary>Gets shared sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceBDiagnostic => this.SkyLuminanceRDiagnostic;

    /// <summary>Gets canonical aerial-distance feedback.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveDistanceScaleDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AerialPerspectiveDistanceScale.Id);

    /// <summary>Gets canonical aerial-scattering feedback.</summary>
    public InspectorFieldDiagnostic AerialScatteringStrengthDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AerialScatteringStrength.Id);

    /// <summary>Gets canonical start-distance feedback.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveStartDepthMetersDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id);

    /// <summary>Gets canonical fog feedback.</summary>
    public InspectorFieldDiagnostic HeightFogContributionDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.HeightFogContribution.Id);

    /// <summary>Validates the distance before the NumberBox accepts its draft.</summary>
    /// <param name="value">The proposed distance in metres.</param>
    /// <returns>Whether the authored distance is valid.</returns>
    public bool ValidateAerialStart(float value)
    {
        var result = SceneEnvironmentConstraints.ValidateAerialStart(value);
        var property = SceneProperties.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id;
        var ticket = this.EditOwner.Diagnostics.Begin([property]);
        var outcome = result.IsValid
            ? new SceneEditResult(SceneEditOutcome.Applied, [])
            : new SceneEditResult(SceneEditOutcome.Invalid, [SceneDiagnostic.Error(SceneEditCodes.PropertyOutOfRange, result.Message ?? "The distance is out of range.", property: property)]);
        this.EditOwner.Diagnostics.Complete(ticket, outcome);
        return result.IsValid;
    }

    /// <summary>Authors complete linear albedo through the captured scene owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetGroundAlbedoColor(Vector3 color) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.GroundAlbedo, color);

    /// <summary>Refreshes display adapters under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored atmosphere snapshot.</param>
    internal void Refresh(SkyAtmosphereEnvironmentData value)
    {
        this.SunDiskEnabled = value.SunDiskEnabled;
        this.PlanetRadiusKm = value.PlanetRadiusMeters / 1000f;
        this.AtmosphereHeightKm = value.AtmosphereHeightMeters / 1000f;
        this.GroundAlbedoR = value.GroundAlbedoRgb.X;
        this.GroundAlbedoG = value.GroundAlbedoRgb.Y;
        this.GroundAlbedoB = value.GroundAlbedoRgb.Z;
        this.RayleighScaleHeightKm = value.RayleighScaleHeightMeters / 1000f;
        this.MieScaleHeightKm = value.MieScaleHeightMeters / 1000f;
        this.MieAnisotropy = value.MieAnisotropy;
        this.SkyLuminanceR = value.SkyLuminanceFactorRgb.X;
        this.SkyLuminanceG = value.SkyLuminanceFactorRgb.Y;
        this.SkyLuminanceB = value.SkyLuminanceFactorRgb.Z;
        this.AerialPerspectiveDistanceScale = value.AerialPerspectiveDistanceScale;
        this.AerialScatteringStrength = value.AerialScatteringStrength;
        this.AerialPerspectiveStartDepthMeters = value.AerialPerspectiveStartDepthMeters;
        _ = this.ValidateAerialStart(value.AerialPerspectiveStartDepthMeters);
        this.HeightFogContribution = value.HeightFogContribution;
        this.OnPropertyChanged(nameof(this.GroundAlbedoColor));
    }

    partial void OnSunDiskEnabledChanged(bool value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.SunDiskEnabled, value);

    partial void OnPlanetRadiusKmChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.PlanetRadiusMeters, value * 1000f);

    partial void OnAtmosphereHeightKmChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AtmosphereHeightMeters, value * 1000f);

    partial void OnRayleighScaleHeightKmChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.RayleighScaleHeightMeters, value * 1000f);

    partial void OnMieScaleHeightKmChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.MieScaleHeightMeters, value * 1000f);

    partial void OnMieAnisotropyChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.MieAnisotropy, value);

    partial void OnGroundAlbedoRChanged(double value) => this.ApplyAlbedo(InspectorValues.ToVector3(value, this.GroundAlbedoG, this.GroundAlbedoB));

    partial void OnGroundAlbedoGChanged(double value) => this.ApplyAlbedo(InspectorValues.ToVector3(this.GroundAlbedoR, value, this.GroundAlbedoB));

    partial void OnGroundAlbedoBChanged(double value) => this.ApplyAlbedo(InspectorValues.ToVector3(this.GroundAlbedoR, this.GroundAlbedoG, value));

    partial void OnSkyLuminanceRChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.SkyLuminance, InspectorValues.ToVector3(value, this.SkyLuminanceG, this.SkyLuminanceB));

    partial void OnSkyLuminanceGChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.SkyLuminance, InspectorValues.ToVector3(this.SkyLuminanceR, value, this.SkyLuminanceB));

    partial void OnSkyLuminanceBChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.SkyLuminance, InspectorValues.ToVector3(this.SkyLuminanceR, this.SkyLuminanceG, value));

    partial void OnAerialPerspectiveDistanceScaleChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AerialPerspectiveDistanceScale, value);

    partial void OnAerialScatteringStrengthChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AerialScatteringStrength, value);

    partial void OnAerialPerspectiveStartDepthMetersChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AerialPerspectiveStartDepthMeters, value);

    partial void OnHeightFogContributionChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.HeightFogContribution, value);

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);

    private void ApplyAlbedo(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.GroundAlbedoColor));
        this.EditOwner.Apply(SceneProperties.SceneEnvironment.GroundAlbedo, color);
    }
}
