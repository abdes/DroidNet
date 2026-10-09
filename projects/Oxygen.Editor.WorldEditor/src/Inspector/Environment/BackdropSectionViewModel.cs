// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Reactive.Concurrency;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>
/// Owns the one backdrop choice (atmosphere, cubemap or solid color) and the
/// sky sphere and background fields it shows.
/// </summary>
public sealed partial class BackdropSectionViewModel : ObservableObject, IDisposable
{
    private int authoredBackdropIndex = -1;

    /// <summary>Initializes a new instance of the <see cref="BackdropSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    /// <param name="cubeTextures">The inspector's shared cube texture feed.</param>
    /// <param name="observerScheduler">The catalog notification scheduler.</param>
    internal BackdropSectionViewModel(SceneEnvironmentEditOwner owner, IObservable<IReadOnlyList<ContentBrowserAssetItem>>? cubeTextures, IScheduler observerScheduler)
    {
        this.EditOwner = owner;
        this.Cubemaps = new(cubeTextures, observerScheduler);
        this.Cubemaps.PropertyChanged += (_, args) =>
        {
            if (string.Equals(args.PropertyName, nameof(CubemapPickerModel.FilteredRows), StringComparison.Ordinal))
            {
                this.OnPropertyChanged(nameof(this.CubemapDisplayName));
            }
        };
    }

    /// <summary>Gets or sets the backdrop as a segment index: 0 atmosphere, 1 cubemap, 2 solid color, -1 none.</summary>
    [ObservableProperty]
    public partial int BackdropIndex { get; set; } = -1;

    /// <summary>Gets or sets a value indicating whether the solid color also lights the scene.</summary>
    [ObservableProperty]
    public partial bool SolidColorLightsScene { get; set; }

    [ObservableProperty]
    public partial float SolidColorR { get; set; }

    [ObservableProperty]
    public partial float SolidColorG { get; set; }

    [ObservableProperty]
    public partial float SolidColorB { get; set; }

    /// <summary>Gets or sets the sky sphere cubemap.</summary>
    [ObservableProperty]
    public partial Uri? Cubemap { get; set; }

    /// <summary>Gets or sets the sky sphere rotation, in degrees.</summary>
    [ObservableProperty]
    public partial float RotationDegrees { get; set; }

    /// <summary>Gets or sets the sky sphere brightness, in exposure stops over its authored radiance.</summary>
    [ObservableProperty]
    public partial float IntensityEv { get; set; }

    [ObservableProperty]
    public partial float TintR { get; set; }

    [ObservableProperty]
    public partial float TintG { get; set; }

    [ObservableProperty]
    public partial float TintB { get; set; }

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the cube texture choices.</summary>
    public CubemapPickerModel Cubemaps { get; }

    /// <summary>Gets the shown backdrop.</summary>
    public EnvironmentBackdrop Backdrop => this.BackdropIndex switch
    {
        0 => EnvironmentBackdrop.Atmosphere,
        1 => EnvironmentBackdrop.Cubemap,
        2 => EnvironmentBackdrop.SolidColor,
        _ => EnvironmentBackdrop.None,
    };

    /// <summary>Gets a value indicating whether the atmosphere is off because another backdrop shows.</summary>
    public bool IsAtmosphereOff => this.Backdrop != EnvironmentBackdrop.Atmosphere;

    /// <summary>Gets the chosen cubemap's display name.</summary>
    public string CubemapDisplayName => this.Cubemaps.DisplayName(this.Cubemap);

    /// <summary>Gets the solid color in linear RGB.</summary>
    public Vector3 SolidColor => new(this.SolidColorR, this.SolidColorG, this.SolidColorB);

    /// <summary>Gets the sky sphere tint in linear RGB.</summary>
    public Vector3 Tint => new(this.TintR, this.TintG, this.TintB);

    /// <summary>Gets backdrop choice feedback.</summary>
    public InspectorFieldDiagnostic BackdropDiagnostic => this.Diagnostic(SceneSkyFields.Backdrop.Id);

    /// <summary>Gets solid color feedback.</summary>
    public InspectorFieldDiagnostic SolidColorDiagnostic => this.Diagnostic(SceneSkyFields.SolidColor.Id);

    /// <summary>Gets cubemap feedback.</summary>
    public InspectorFieldDiagnostic CubemapDiagnostic => this.Diagnostic(SceneSkyFields.SkySphereCubemap.Id);

    /// <summary>Gets rotation feedback.</summary>
    public InspectorFieldDiagnostic RotationDiagnostic => this.Diagnostic(SceneSkyFields.SkySphereRotationRadians.Id);

    /// <summary>Gets intensity feedback.</summary>
    public InspectorFieldDiagnostic IntensityDiagnostic => this.Diagnostic(SceneSkyFields.SkySphereIntensity.Id);

    /// <summary>Gets tint feedback.</summary>
    public InspectorFieldDiagnostic TintDiagnostic => this.Diagnostic(SceneSkyFields.SkySphereTintRgb.Id);

    /// <summary>Authors a complete linear solid color as one scene edit.</summary>
    /// <param name="color">The authored linear color.</param>
    public void SetSolidColor(Vector3 color)
    {
        this.EditOwner.Refresh(() =>
        {
            this.SolidColorR = color.X;
            this.SolidColorG = color.Y;
            this.SolidColorB = color.Z;
        });
        this.ApplySolidColor(color);
    }

    /// <summary>Chooses a cubemap, or clears it.</summary>
    /// <param name="cubemap">The cube texture, or null for none.</param>
    public void SetCubemap(Uri? cubemap) => this.Cubemap = cubemap;

    /// <inheritdoc />
    public void Dispose() => this.Cubemaps.Dispose();

    /// <summary>Starts the cube texture feed.</summary>
    internal void StartAssets() => this.Cubemaps.Start();

    /// <summary>Displays the authored backdrop under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored environment.</param>
    internal void Refresh(SceneEnvironmentData value)
    {
        this.BackdropIndex = SceneSkyFields.ResolveBackdrop(value) switch
        {
            EnvironmentBackdrop.Atmosphere => 0,
            EnvironmentBackdrop.Cubemap => 1,
            EnvironmentBackdrop.SolidColor => 2,
            _ => -1,
        };
        this.authoredBackdropIndex = this.BackdropIndex;
        this.SolidColorLightsScene = SceneSkyFields.SolidColorLightsSceneOf(value);
        var color = SceneSkyFields.SolidColorOf(value);
        this.SolidColorR = color.X;
        this.SolidColorG = color.Y;
        this.SolidColorB = color.Z;
        this.Cubemap = value.SkySphere.Cubemap;
        this.RotationDegrees = float.RadiansToDegrees(value.SkySphere.RotationRadians);
        this.IntensityEv = RadianceExposure.ToEv(value.SkySphere.Intensity);
        this.TintR = value.SkySphere.TintRgb.X;
        this.TintG = value.SkySphere.TintRgb.Y;
        this.TintB = value.SkySphere.TintRgb.Z;
        this.OnPropertyChanged(nameof(this.SolidColor));
        this.OnPropertyChanged(nameof(this.Tint));
    }

    partial void OnBackdropIndexChanged(int value)
    {
        this.OnPropertyChanged(nameof(this.Backdrop));
        this.OnPropertyChanged(nameof(this.IsAtmosphereOff));

        // The segmented control clears and restores its selection while it
        // loads; only choosing another backdrop is an edit.
        if (value >= 0 && value != this.authoredBackdropIndex)
        {
            this.EditOwner.Apply(SceneSkyFields.Backdrop, this.Backdrop);
        }
    }

    partial void OnSolidColorLightsSceneChanged(bool value) => this.EditOwner.Apply(SceneSkyFields.SolidColorLightsScene, value);

    partial void OnSolidColorRChanged(float value) => this.ApplySolidColor(new(value, this.SolidColorG, this.SolidColorB));

    partial void OnSolidColorGChanged(float value) => this.ApplySolidColor(new(this.SolidColorR, value, this.SolidColorB));

    partial void OnSolidColorBChanged(float value) => this.ApplySolidColor(new(this.SolidColorR, this.SolidColorG, value));

    partial void OnCubemapChanged(Uri? value)
    {
        this.OnPropertyChanged(nameof(this.CubemapDisplayName));
        this.EditOwner.Apply(SceneSkyFields.SkySphereCubemap, value);
    }

    partial void OnRotationDegreesChanged(float value) => this.EditOwner.Apply(SceneSkyFields.SkySphereRotationRadians, float.DegreesToRadians(value));

    partial void OnIntensityEvChanged(float value) => this.EditOwner.Apply(SceneSkyFields.SkySphereIntensity, RadianceExposure.ToScale(value));

    partial void OnTintRChanged(float value) => this.ApplyTint(new(value, this.TintG, this.TintB));

    partial void OnTintGChanged(float value) => this.ApplyTint(new(this.TintR, value, this.TintB));

    partial void OnTintBChanged(float value) => this.ApplyTint(new(this.TintR, this.TintG, value));

    private void ApplySolidColor(Vector3 color)
    {
        this.OnPropertyChanged(nameof(this.SolidColor));
        this.EditOwner.Apply(SceneSkyFields.SolidColor, color);
    }

    private void ApplyTint(Vector3 tint)
    {
        this.OnPropertyChanged(nameof(this.Tint));
        this.EditOwner.Apply(SceneSkyFields.SkySphereTintRgb, tint);
    }

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);
}
