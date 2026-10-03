// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns atmosphere display units, channel edits and Aerial Start validation.</summary>
public sealed partial class SkyAtmosphereSectionViewModel : ObservableObject
{
    /// <summary>Initializes a new instance of the <see cref="SkyAtmosphereSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    internal SkyAtmosphereSectionViewModel(SceneEnvironmentEditOwner owner) => this.EditOwner = owner;

    [ObservableProperty]
    public partial bool AtmosphereEnabled { get; set; }

    [ObservableProperty]
    public partial bool SunDiskEnabled { get; set; }

    [ObservableProperty]
    public partial float PlanetRadiusKm { get; set; }

    [ObservableProperty]
    public partial float AtmosphereHeightKm { get; set; }

    [ObservableProperty]
    public partial float GroundAlbedoR { get; set; }

    [ObservableProperty]
    public partial float GroundAlbedoG { get; set; }

    [ObservableProperty]
    public partial float GroundAlbedoB { get; set; }

    [ObservableProperty]
    public partial float RayleighScaleHeightKm { get; set; }

    [ObservableProperty]
    public partial float MieScaleHeightKm { get; set; }

    [ObservableProperty]
    public partial float MieAnisotropy { get; set; }

    [ObservableProperty]
    public partial float SkyLuminanceR { get; set; }

    [ObservableProperty]
    public partial float SkyLuminanceG { get; set; }

    [ObservableProperty]
    public partial float SkyLuminanceB { get; set; }

    [ObservableProperty]
    public partial float AerialPerspectiveDistanceScale { get; set; }

    [ObservableProperty]
    public partial float AerialScatteringStrength { get; set; }

    [ObservableProperty]
    public partial float AerialPerspectiveStartDepthMeters { get; set; }

    [ObservableProperty]
    public partial float HeightFogContribution { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets authored linear albedo.</summary>
    public Vector3 GroundAlbedoColor => new(this.GroundAlbedoR, this.GroundAlbedoG, this.GroundAlbedoB);

    /// <summary>Gets canonical enablement feedback.</summary>
    public InspectorFieldDiagnostic AtmosphereEnabledDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled.Id);

    /// <summary>Gets canonical sun-disk feedback.</summary>
    public InspectorFieldDiagnostic SunDiskEnabledDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled.Id);

    /// <summary>Gets canonical radius feedback.</summary>
    public InspectorFieldDiagnostic PlanetRadiusKmDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters.Id);

    /// <summary>Gets canonical height feedback.</summary>
    public InspectorFieldDiagnostic AtmosphereHeightKmDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters.Id);

    /// <summary>Gets canonical albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoRDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id);

    /// <summary>Gets shared albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoGDiagnostic => this.GroundAlbedoRDiagnostic;

    /// <summary>Gets shared albedo feedback.</summary>
    public InspectorFieldDiagnostic GroundAlbedoBDiagnostic => this.GroundAlbedoRDiagnostic;

    /// <summary>Gets canonical scattering feedback.</summary>
    public InspectorFieldDiagnostic RayleighScaleHeightKmDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters.Id);

    /// <summary>Gets canonical scattering feedback.</summary>
    public InspectorFieldDiagnostic MieScaleHeightKmDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters.Id);

    /// <summary>Gets canonical phase-function feedback.</summary>
    public InspectorFieldDiagnostic MieAnisotropyDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy.Id);

    /// <summary>Gets canonical sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceRDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id);

    /// <summary>Gets shared sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceGDiagnostic => this.SkyLuminanceRDiagnostic;

    /// <summary>Gets shared sky-multiplier feedback.</summary>
    public InspectorFieldDiagnostic SkyLuminanceBDiagnostic => this.SkyLuminanceRDiagnostic;

    /// <summary>Gets canonical aerial-distance feedback.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveDistanceScaleDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale.Id);

    /// <summary>Gets canonical aerial-scattering feedback.</summary>
    public InspectorFieldDiagnostic AerialScatteringStrengthDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength.Id);

    /// <summary>Gets canonical start-distance feedback.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveStartDepthMetersDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id);

    /// <summary>Gets canonical fog feedback.</summary>
    public InspectorFieldDiagnostic HeightFogContributionDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution.Id);

    /// <summary>Validates the distance before the NumberBox accepts its draft.</summary>
    /// <param name="value">The proposed distance in metres.</param>
    /// <returns>Whether the authored distance is valid.</returns>
    public bool ValidateAerialStart(float value)
    {
        var result = SceneEnvironmentConstraints.ValidateAerialStart(value);
        var ticket = this.EditOwner.Diagnostics.Begin([SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id], revision: 0);
        this.EditOwner.Diagnostics.Complete(ticket, new(result.IsValid) { ValidationCode = result.Code, ValidationMessage = result.Message });
        return result.IsValid;
    }

    /// <summary>Authors complete linear albedo through the captured scene owner.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetGroundAlbedoColor(Vector3 color) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, color);

    /// <summary>Refreshes display adapters under the parent's model-refresh guard.</summary>
    /// <param name="enabled">The atmosphere enablement.</param>
    /// <param name="value">The authored atmosphere snapshot.</param>
    internal void Refresh(bool enabled, SkyAtmosphereEnvironmentData value)
    {
        this.AtmosphereEnabled = enabled;
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

    partial void OnAtmosphereEnabledChanged(bool value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled, value);

    partial void OnSunDiskEnabledChanged(bool value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled, value);

    partial void OnPlanetRadiusKmChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters, value * 1000f);

    partial void OnAtmosphereHeightKmChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters, value * 1000f);

    partial void OnRayleighScaleHeightKmChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters, value * 1000f);

    partial void OnMieScaleHeightKmChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters, value * 1000f);

    partial void OnMieAnisotropyChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy, value);

    partial void OnGroundAlbedoRChanged(float value) => this.ApplyAlbedo(new(value, this.GroundAlbedoG, this.GroundAlbedoB));

    partial void OnGroundAlbedoGChanged(float value) => this.ApplyAlbedo(new(this.GroundAlbedoR, value, this.GroundAlbedoB));

    partial void OnGroundAlbedoBChanged(float value) => this.ApplyAlbedo(new(this.GroundAlbedoR, this.GroundAlbedoG, value));

    partial void OnSkyLuminanceRChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(value, this.SkyLuminanceG, this.SkyLuminanceB));

    partial void OnSkyLuminanceGChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(this.SkyLuminanceR, value, this.SkyLuminanceB));

    partial void OnSkyLuminanceBChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(this.SkyLuminanceR, this.SkyLuminanceG, value));

    partial void OnAerialPerspectiveDistanceScaleChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale, value);

    partial void OnAerialScatteringStrengthChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength, value);

    partial void OnAerialPerspectiveStartDepthMetersChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters, value);

    partial void OnHeightFogContributionChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution, value);

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);

    private void ApplyAlbedo(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.GroundAlbedoColor));
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, color);
    }
}
