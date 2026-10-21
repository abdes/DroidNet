// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Reactive.Concurrency;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns the sky light's image-based lighting fields and their edits.</summary>
public sealed partial class SkyLightSectionViewModel : ObservableObject, IDisposable
{
    private int authoredSourceIndex;

    /// <summary>Initializes a new instance of the <see cref="SkyLightSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    /// <param name="cubeTextures">The inspector's shared cube texture feed.</param>
    /// <param name="observerScheduler">The catalog notification scheduler.</param>
    internal SkyLightSectionViewModel(SceneEnvironmentEditOwner owner, IObservable<IReadOnlyList<CubeTextureAsset>>? cubeTextures, IScheduler observerScheduler)
    {
        this.EditOwner = owner;
        this.Cubemaps = new(cubeTextures, observerScheduler);
        this.Cubemaps.ChoicesUpdated += (_, _) => this.OnPropertyChanged(nameof(this.CubemapDisplayName));
    }

    [ObservableProperty]
    public partial bool Enabled { get; set; }

    /// <summary>Gets or sets the source as a segment index: 0 captured sky, 1 cubemap.</summary>
    [ObservableProperty]
    public partial int SourceIndex { get; set; }

    [ObservableProperty]
    public partial AssetPath? Cubemap { get; set; }

    /// <summary>Gets or sets the cubemap rotation, in degrees.</summary>
    [ObservableProperty]
    public partial double CubemapAngleDegrees { get; set; }

    /// <summary>Gets or sets the sky light brightness, in exposure stops over the sky's radiance.</summary>
    [ObservableProperty]
    public partial double IntensityEv { get; set; }

    /// <summary>Gets or sets the illuminance, in lux, the specified cubemap is calibrated to deliver; 0 keeps it raw.</summary>
    [ObservableProperty]
    public partial double CubemapIlluminanceLux { get; set; }

    [ObservableProperty]
    public partial double TintR { get; set; }

    [ObservableProperty]
    public partial double TintG { get; set; }

    [ObservableProperty]
    public partial double TintB { get; set; }

    [ObservableProperty]
    public partial double DiffuseIntensity { get; set; }

    [ObservableProperty]
    public partial double SpecularIntensity { get; set; }

    [ObservableProperty]
    public partial bool LowerHemisphereIsSolidColor { get; set; }

    [ObservableProperty]
    public partial double LowerHemisphereR { get; set; }

    [ObservableProperty]
    public partial double LowerHemisphereG { get; set; }

    [ObservableProperty]
    public partial double LowerHemisphereB { get; set; }

    [ObservableProperty]
    public partial double LowerHemisphereBlendAlpha { get; set; }

    [ObservableProperty]
    public partial double VolumetricScatteringIntensity { get; set; }

    [ObservableProperty]
    public partial bool AffectReflections { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the cube texture choices.</summary>
    public CubemapPickerModel Cubemaps { get; }

    /// <summary>Gets the authored source.</summary>
    public SkyLightSource Source => this.SourceIndex == 1 ? SkyLightSource.SpecifiedCubemap : SkyLightSource.CapturedScene;

    /// <summary>Gets the chosen cubemap's display name.</summary>
    public string CubemapDisplayName => this.Cubemaps.DisplayName(this.Cubemap);

    /// <summary>Gets the tint in linear RGB.</summary>
    public Vector3 Tint => InspectorValues.ToVector3(this.TintR, this.TintG, this.TintB);

    /// <summary>Gets the lower hemisphere color in linear RGB.</summary>
    public Vector3 LowerHemisphereColor => InspectorValues.ToVector3(this.LowerHemisphereR, this.LowerHemisphereG, this.LowerHemisphereB);

    /// <summary>Gets cubemap feedback.</summary>
    public InspectorFieldDiagnostic CubemapDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightCubemap.Id);

    /// <summary>Gets cubemap rotation feedback.</summary>
    public InspectorFieldDiagnostic CubemapAngleDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightCubemapAngleRadians.Id);

    /// <summary>Gets intensity feedback.</summary>
    public InspectorFieldDiagnostic IntensityDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightIntensity.Id);

    /// <summary>Gets the cubemap illuminance diagnostic.</summary>
    public InspectorFieldDiagnostic CubemapIlluminanceDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightCubemapIlluminanceLux.Id);

    /// <summary>Gets tint feedback.</summary>
    public InspectorFieldDiagnostic TintDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightTintRgb.Id);

    /// <summary>Gets diffuse multiplier feedback.</summary>
    public InspectorFieldDiagnostic DiffuseIntensityDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightDiffuseIntensity.Id);

    /// <summary>Gets specular multiplier feedback.</summary>
    public InspectorFieldDiagnostic SpecularIntensityDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightSpecularIntensity.Id);

    /// <summary>Gets lower hemisphere color feedback.</summary>
    public InspectorFieldDiagnostic LowerHemisphereColorDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightLowerHemisphereColor.Id);

    /// <summary>Gets lower hemisphere blend feedback.</summary>
    public InspectorFieldDiagnostic LowerHemisphereBlendAlphaDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightLowerHemisphereBlendAlpha.Id);

    /// <summary>Gets volumetric scattering feedback.</summary>
    public InspectorFieldDiagnostic VolumetricScatteringIntensityDiagnostic => this.Diagnostic(SceneSkyFields.SkyLightVolumetricScatteringIntensity.Id);

    /// <summary>Chooses a cubemap, or clears it.</summary>
    /// <param name="cubemap">The cube texture, or null for none.</param>
    public void SetCubemap(AssetPath? cubemap) => this.Cubemap = cubemap;

    /// <inheritdoc />
    public void Dispose()
    {
        this.StopRuntimeWatch();
        this.Cubemaps.Dispose();
    }

    /// <summary>Starts the cube texture feed.</summary>
    internal void StartAssets() => this.Cubemaps.Start();

    /// <summary>Displays the authored sky light under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored sky light.</param>
    internal void Refresh(SkyLightEnvironmentData value)
    {
        this.Enabled = value.Enabled;
        this.SourceIndex = value.Source == SkyLightSource.SpecifiedCubemap ? 1 : 0;
        this.authoredSourceIndex = this.SourceIndex;
        this.Cubemap = value.Cubemap;
        this.CubemapAngleDegrees = float.RadiansToDegrees(value.CubemapAngleRadians);
        this.IntensityEv = RadianceExposure.ToEv(value.Intensity);
        this.CubemapIlluminanceLux = value.CubemapIlluminanceLux;
        this.TintR = value.TintRgb.X;
        this.TintG = value.TintRgb.Y;
        this.TintB = value.TintRgb.Z;
        this.DiffuseIntensity = value.DiffuseIntensity;
        this.SpecularIntensity = value.SpecularIntensity;
        this.LowerHemisphereIsSolidColor = value.LowerHemisphereIsSolidColor;
        this.LowerHemisphereR = value.LowerHemisphereColor.X;
        this.LowerHemisphereG = value.LowerHemisphereColor.Y;
        this.LowerHemisphereB = value.LowerHemisphereColor.Z;
        this.LowerHemisphereBlendAlpha = value.LowerHemisphereBlendAlpha;
        this.VolumetricScatteringIntensity = value.VolumetricScatteringIntensity;
        this.AffectReflections = value.AffectReflections;
        this.OnPropertyChanged(nameof(this.Tint));
        this.OnPropertyChanged(nameof(this.LowerHemisphereColor));
    }

    partial void OnEnabledChanged(bool value) => this.EditOwner.Apply(SceneSkyFields.SkyLightEnabled, value);

    partial void OnSourceIndexChanged(int value)
    {
        this.OnPropertyChanged(nameof(this.Source));

        // The segmented control clears and restores its selection while it
        // loads; only choosing the other source is an edit.
        if (value >= 0 && value != this.authoredSourceIndex)
        {
            this.EditOwner.Apply(SceneSkyFields.SkyLightSource, this.Source);
        }
    }

    partial void OnCubemapChanged(AssetPath? value)
    {
        this.OnPropertyChanged(nameof(this.CubemapDisplayName));
        this.EditOwner.Apply(SceneSkyFields.SkyLightCubemap, value);
    }

    partial void OnCubemapAngleDegreesChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightCubemapAngleRadians, double.DegreesToRadians(value));

    partial void OnIntensityEvChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightIntensity, RadianceExposure.ToScale((float)value));

    partial void OnCubemapIlluminanceLuxChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightCubemapIlluminanceLux, value);

    partial void OnTintRChanged(double value) => this.ApplyTint(InspectorValues.ToVector3(value, this.TintG, this.TintB));

    partial void OnTintGChanged(double value) => this.ApplyTint(InspectorValues.ToVector3(this.TintR, value, this.TintB));

    partial void OnTintBChanged(double value) => this.ApplyTint(InspectorValues.ToVector3(this.TintR, this.TintG, value));

    partial void OnDiffuseIntensityChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightDiffuseIntensity, value);

    partial void OnSpecularIntensityChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightSpecularIntensity, value);

    partial void OnLowerHemisphereIsSolidColorChanged(bool value) => this.EditOwner.Apply(SceneSkyFields.SkyLightLowerHemisphereIsSolidColor, value);

    partial void OnLowerHemisphereRChanged(double value) => this.ApplyLowerHemisphere(InspectorValues.ToVector3(value, this.LowerHemisphereG, this.LowerHemisphereB));

    partial void OnLowerHemisphereGChanged(double value) => this.ApplyLowerHemisphere(InspectorValues.ToVector3(this.LowerHemisphereR, value, this.LowerHemisphereB));

    partial void OnLowerHemisphereBChanged(double value) => this.ApplyLowerHemisphere(InspectorValues.ToVector3(this.LowerHemisphereR, this.LowerHemisphereG, value));

    partial void OnLowerHemisphereBlendAlphaChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightLowerHemisphereBlendAlpha, value);

    partial void OnVolumetricScatteringIntensityChanged(double value) => this.EditOwner.Apply(SceneSkyFields.SkyLightVolumetricScatteringIntensity, value);

    partial void OnAffectReflectionsChanged(bool value) => this.EditOwner.Apply(SceneSkyFields.SkyLightAffectReflections, value);

    private void ApplyTint(Vector3 tint)
    {
        this.OnPropertyChanged(nameof(this.Tint));
        this.EditOwner.Apply(SceneSkyFields.SkyLightTintRgb, tint);
    }

    private void ApplyLowerHemisphere(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.LowerHemisphereColor));
        this.EditOwner.Apply(SceneSkyFields.SkyLightLowerHemisphereColor, color);
    }

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);
}
