// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Windows.UI;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene-level Environment inspector view model.
/// </summary>
public partial class EnvironmentViewModel(
    ISceneDocumentCommandService? commandService = null,
    Func<SceneDocumentCommandContext?>? commandContextProvider = null) : ComponentPropertyEditor
{
    private Scene? scene;
    private bool isApplyingEditorValues;

    [ObservableProperty]
    public partial bool AtmosphereEnabled { get; set; }

    [ObservableProperty]
    public partial SunLightOption? SelectedSun { get; set; }

    [ObservableProperty]
    public partial ExposureMode ExposureMode { get; set; }

    [ObservableProperty]
    public partial bool ExposureEnabled { get; set; }

    [ObservableProperty]
    public partial float ManualExposureEv { get; set; }

    [ObservableProperty]
    public partial float ExposureCompensation { get; set; }

    [ObservableProperty]
    public partial float ExposureKey { get; set; }

    [ObservableProperty]
    public partial ToneMappingMode ToneMapping { get; set; }

    [ObservableProperty]
    public partial MeteringMode AutoExposureMeteringMode { get; set; }

    [ObservableProperty]
    public partial float AutoExposureMinEv { get; set; }

    [ObservableProperty]
    public partial float AutoExposureMaxEv { get; set; }

    [ObservableProperty]
    public partial float AutoExposureSpeedUp { get; set; }

    [ObservableProperty]
    public partial float AutoExposureSpeedDown { get; set; }

    [ObservableProperty]
    public partial float AutoExposureLowPercentile { get; set; }

    [ObservableProperty]
    public partial float AutoExposureHighPercentile { get; set; }

    [ObservableProperty]
    public partial float AutoExposureMinLogLuminance { get; set; }

    [ObservableProperty]
    public partial float AutoExposureLogLuminanceRange { get; set; }

    [ObservableProperty]
    public partial float AutoExposureTargetLuminance { get; set; }

    [ObservableProperty]
    public partial float AutoExposureSpotMeterRadius { get; set; }

    [ObservableProperty]
    public partial float BloomIntensity { get; set; }

    [ObservableProperty]
    public partial float BloomThreshold { get; set; }

    [ObservableProperty]
    public partial float Saturation { get; set; }

    [ObservableProperty]
    public partial float Contrast { get; set; }

    [ObservableProperty]
    public partial float VignetteIntensity { get; set; }

    [ObservableProperty]
    public partial float DisplayGamma { get; set; }

    [ObservableProperty]
    public partial float BackgroundR { get; set; }

    [ObservableProperty]
    public partial float BackgroundG { get; set; }

    [ObservableProperty]
    public partial float BackgroundB { get; set; }

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

    [ObservableProperty]
    public partial bool SunDiskEnabled { get; set; }

    [ObservableProperty]
    public partial bool HasStaleSun { get; set; }

    [ObservableProperty]
    public partial string StaleSunText { get; set; } = string.Empty;

    /// <summary>
    /// Gets available exposure modes.
    /// </summary>
    public IReadOnlyList<ExposureMode> ExposureModes { get; } = [ExposureMode.Auto, ExposureMode.Manual, ExposureMode.ManualCamera];

    /// <summary>
    /// Gets available tone mapping modes.
    /// </summary>
    public IReadOnlyList<ToneMappingMode> ToneMappingModes { get; } =
        [ToneMappingMode.AcesFitted, ToneMappingMode.Filmic, ToneMappingMode.Reinhard, ToneMappingMode.None];

    /// <summary>
    /// Gets available auto-exposure metering modes.
    /// </summary>
    public IReadOnlyList<MeteringMode> MeteringModes { get; } = Enum.GetValues<MeteringMode>();

    /// <summary>
    /// Gets directional lights that can be bound as the scene sun.
    /// </summary>
    public ObservableCollection<SunLightOption> SunOptions { get; } = [];

    /// <summary>
    /// Gets a brush for the background swatch.
    /// </summary>
    public SolidColorBrush BackgroundBrush => new(this.BackgroundColor);

    /// <summary>
    /// Gets the background color as a WinUI color.
    /// </summary>
    public Color BackgroundColor => Color.FromArgb(255, ToByte(this.BackgroundR), ToByte(this.BackgroundG), ToByte(this.BackgroundB));

    /// <summary>
    /// Gets a value indicating whether manual exposure controls apply to the current mode.
    /// </summary>
    public bool IsManualExposureVisible => this.ExposureMode is ExposureMode.Manual or ExposureMode.ManualCamera;

    /// <summary>
    /// Gets a value indicating whether auto exposure controls apply to the current mode.
    /// </summary>
    public bool IsAutoExposureVisible => this.ExposureMode == ExposureMode.Auto;

    /// <summary>
    /// Gets a value indicating whether tone-mapping dependent controls apply to the current mode.
    /// </summary>
    public bool IsToneMappingControlsVisible => this.ToneMapping != ToneMappingMode.None;

    /// <inheritdoc />
    public override string Header => "Environment";

    /// <inheritdoc />
    public override string Description => "Scene atmosphere, sun, exposure, tone mapping, and background intent.";

    /// <summary>
    /// Sets the scene context used when no node is selected.
    /// </summary>
    public void SetScene(Scene? value)
    {
        if (ReferenceEquals(this.scene, value))
        {
            return;
        }

        this.scene = value;
        this.RefreshFromScene();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        if (items.Count > 0)
        {
            this.SetScene(null);
            return;
        }

        this.RefreshFromScene();
    }

    /// <summary>
    /// Applies the selected background color from the color picker.
    /// </summary>
    public void SetBackgroundColor(Color color)
    {
        var r = color.R / 255f;
        var g = color.G / 255f;
        var b = color.B / 255f;
        this.isApplyingEditorValues = true;
        try
        {
            this.BackgroundR = r;
            this.BackgroundG = g;
            this.BackgroundB = b;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyBackgroundChanged();
        this.ApplyEnvironmentEdit(BackgroundEdit(new Vector3(r, g, b)));
    }

    [RelayCommand]
    private void ClearSun()
    {
        this.isApplyingEditorValues = true;
        try
        {
            if (this.SunOptions.FirstOrDefault(static option => option.NodeId is null) is { } none)
            {
                this.SelectedSun = none;
            }
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.ApplyEnvironmentEdit(new SceneEnvironmentEdit(
            Optional<bool>.Unspecified,
            Optional<Guid?>.Supplied(null),
            Optional<ExposureMode>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<ToneMappingMode>.Unspecified,
            Optional<Vector3>.Unspecified,
            Optional<SkyAtmosphereEnvironmentData>.Unspecified));
    }

    partial void OnAtmosphereEnabledChanged(bool value) => this.ApplyEnvironmentEdit(new SceneEnvironmentEdit(
        Optional<bool>.Supplied(value),
        Optional<Guid?>.Unspecified,
        Optional<ExposureMode>.Unspecified,
        Optional<float>.Unspecified,
        Optional<float>.Unspecified,
        Optional<ToneMappingMode>.Unspecified,
        Optional<Vector3>.Unspecified));

    partial void OnSelectedSunChanged(SunLightOption? value)
    {
        if (this.isApplyingEditorValues)
        {
            return;
        }

        if (value is null)
        {
            return;
        }

        this.ApplyEnvironmentEdit(new SceneEnvironmentEdit(
            Optional<bool>.Unspecified,
            Optional<Guid?>.Supplied(value.NodeId),
            Optional<ExposureMode>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<ToneMappingMode>.Unspecified,
            Optional<Vector3>.Unspecified));
    }

    partial void OnExposureModeChanged(ExposureMode value)
    {
        this.OnPropertyChanged(nameof(this.IsManualExposureVisible));
        this.OnPropertyChanged(nameof(this.IsAutoExposureVisible));
        this.ApplyPostProcessEdit(post => post with { ExposureMode = value });
    }

    partial void OnExposureEnabledChanged(bool value) => this.ApplyPostProcessEdit(post => post with { ExposureEnabled = value });

    partial void OnManualExposureEvChanged(float value) => this.ApplyPostProcessEdit(post => post with { ManualExposureEv = value });

    partial void OnExposureCompensationChanged(float value) => this.ApplyPostProcessEdit(post => post with { ExposureCompensationEv = value });

    partial void OnExposureKeyChanged(float value) => this.ApplyPostProcessEdit(post => post with { ExposureKey = value });

    partial void OnToneMappingChanged(ToneMappingMode value)
    {
        this.OnPropertyChanged(nameof(this.IsToneMappingControlsVisible));
        this.ApplyPostProcessEdit(post => post with { ToneMapper = value });
    }

    partial void OnAutoExposureMeteringModeChanged(MeteringMode value) => this.ApplyPostProcessEdit(post => post with { AutoExposureMeteringMode = value });

    partial void OnAutoExposureMinEvChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureMinEv = value });

    partial void OnAutoExposureMaxEvChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureMaxEv = value });

    partial void OnAutoExposureSpeedUpChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureSpeedUp = value });

    partial void OnAutoExposureSpeedDownChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureSpeedDown = value });

    partial void OnAutoExposureLowPercentileChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureLowPercentile = value });

    partial void OnAutoExposureHighPercentileChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureHighPercentile = value });

    partial void OnAutoExposureMinLogLuminanceChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureMinLogLuminance = value });

    partial void OnAutoExposureLogLuminanceRangeChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureLogLuminanceRange = value });

    partial void OnAutoExposureTargetLuminanceChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureTargetLuminance = value });

    partial void OnAutoExposureSpotMeterRadiusChanged(float value) => this.ApplyPostProcessEdit(post => post with { AutoExposureSpotMeterRadius = value });

    partial void OnBloomIntensityChanged(float value) => this.ApplyPostProcessEdit(post => post with { BloomIntensity = value });

    partial void OnBloomThresholdChanged(float value) => this.ApplyPostProcessEdit(post => post with { BloomThreshold = value });

    partial void OnSaturationChanged(float value) => this.ApplyPostProcessEdit(post => post with { Saturation = value });

    partial void OnContrastChanged(float value) => this.ApplyPostProcessEdit(post => post with { Contrast = value });

    partial void OnVignetteIntensityChanged(float value) => this.ApplyPostProcessEdit(post => post with { VignetteIntensity = value });

    partial void OnDisplayGammaChanged(float value) => this.ApplyPostProcessEdit(post => post with { DisplayGamma = value });

    partial void OnBackgroundRChanged(float value) => this.ApplyBackgroundAxisEdit(value, this.BackgroundG, this.BackgroundB);

    partial void OnBackgroundGChanged(float value) => this.ApplyBackgroundAxisEdit(this.BackgroundR, value, this.BackgroundB);

    partial void OnBackgroundBChanged(float value) => this.ApplyBackgroundAxisEdit(this.BackgroundR, this.BackgroundG, value);

    partial void OnPlanetRadiusKmChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { PlanetRadiusMeters = value * 1000.0f });

    partial void OnAtmosphereHeightKmChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { AtmosphereHeightMeters = value * 1000.0f });

    partial void OnGroundAlbedoRChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { GroundAlbedoRgb = new Vector3(value, this.GroundAlbedoG, this.GroundAlbedoB) });

    partial void OnGroundAlbedoGChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { GroundAlbedoRgb = new Vector3(this.GroundAlbedoR, value, this.GroundAlbedoB) });

    partial void OnGroundAlbedoBChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { GroundAlbedoRgb = new Vector3(this.GroundAlbedoR, this.GroundAlbedoG, value) });

    partial void OnRayleighScaleHeightKmChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { RayleighScaleHeightMeters = value * 1000.0f });

    partial void OnMieScaleHeightKmChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { MieScaleHeightMeters = value * 1000.0f });

    partial void OnMieAnisotropyChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { MieAnisotropy = value });

    partial void OnSkyLuminanceRChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { SkyLuminanceFactorRgb = new Vector3(value, this.SkyLuminanceG, this.SkyLuminanceB) });

    partial void OnSkyLuminanceGChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { SkyLuminanceFactorRgb = new Vector3(this.SkyLuminanceR, value, this.SkyLuminanceB) });

    partial void OnSkyLuminanceBChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { SkyLuminanceFactorRgb = new Vector3(this.SkyLuminanceR, this.SkyLuminanceG, value) });

    partial void OnAerialPerspectiveDistanceScaleChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { AerialPerspectiveDistanceScale = value });

    partial void OnAerialScatteringStrengthChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { AerialScatteringStrength = value });

    partial void OnAerialPerspectiveStartDepthMetersChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { AerialPerspectiveStartDepthMeters = value });

    partial void OnHeightFogContributionChanged(float value) => this.ApplySkyAtmosphereEdit(sky => sky with { HeightFogContribution = value });

    partial void OnSunDiskEnabledChanged(bool value) => this.ApplySkyAtmosphereEdit(sky => sky with { SunDiskEnabled = value });

    private void ApplyBackgroundAxisEdit(float r, float g, float b)
    {
        this.NotifyBackgroundChanged();
        this.ApplyEnvironmentEdit(BackgroundEdit(new Vector3(r, g, b)));
    }

    private void ApplySkyAtmosphereEdit(Func<SkyAtmosphereEnvironmentData, SkyAtmosphereEnvironmentData> edit)
    {
        if (this.scene is null)
        {
            return;
        }

        var next = edit(this.scene.Environment.SkyAtmosphere ?? new());
        this.ApplyEnvironmentEdit(new SceneEnvironmentEdit(
            Optional<bool>.Unspecified,
            Optional<Guid?>.Unspecified,
            Optional<ExposureMode>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<ToneMappingMode>.Unspecified,
            Optional<Vector3>.Unspecified,
            Optional<SkyAtmosphereEnvironmentData>.Supplied(next)));
    }

    private void ApplyPostProcessEdit(Func<PostProcessEnvironmentData, PostProcessEnvironmentData> edit)
    {
        if (this.scene is null)
        {
            return;
        }

        var next = edit(this.scene.Environment.PostProcess ?? new());
        this.ApplyEnvironmentEdit(new SceneEnvironmentEdit(
            Optional<bool>.Unspecified,
            Optional<Guid?>.Unspecified,
            Optional<ExposureMode>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<ToneMappingMode>.Unspecified,
            Optional<Vector3>.Unspecified,
            Optional<SkyAtmosphereEnvironmentData>.Unspecified,
            Optional<PostProcessEnvironmentData>.Supplied(next)));
    }

    private void ApplyEnvironmentEdit(SceneEnvironmentEdit edit)
    {
        if (this.isApplyingEditorValues || this.scene is null)
        {
            return;
        }

        if (commandService is null || commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        _ = this.ApplyEnvironmentEditAsync(context, edit);
    }

    private async Task ApplyEnvironmentEditAsync(SceneDocumentCommandContext context, SceneEnvironmentEdit edit)
    {
        _ = await commandService!.EditSceneEnvironmentAsync(context, edit, EditSessionToken.OneShot).ConfigureAwait(true);
        this.RefreshFromScene();
    }

    private void RefreshFromScene()
    {
        var environment = this.scene?.Environment ?? new SceneEnvironmentData();
        this.isApplyingEditorValues = true;
        try
        {
            this.RebuildSunOptions(environment.SunNodeId);
            this.AtmosphereEnabled = environment.AtmosphereEnabled;
            this.ApplyPostProcessEditorValues(environment.PostProcess);
            this.BackgroundR = environment.BackgroundColor.X;
            this.BackgroundG = environment.BackgroundColor.Y;
            this.BackgroundB = environment.BackgroundColor.Z;
            this.ApplySkyAtmosphereEditorValues(environment.SkyAtmosphere);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyBackgroundChanged();
    }

    private void RebuildSunOptions(Guid? selectedSunNodeId)
    {
        this.SunOptions.Clear();
        var none = new SunLightOption(null, "None");
        this.SunOptions.Add(none);
        this.SelectedSun = none;

        if (this.scene is null)
        {
            this.HasStaleSun = false;
            this.StaleSunText = string.Empty;
            return;
        }

        foreach (var node in this.scene.RootNodes.SelectMany(static root => SceneTraversal.CollectNodes(root)))
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is null)
            {
                continue;
            }

            var option = new SunLightOption(node.Id, node.Name);
            this.SunOptions.Add(option);
            if (selectedSunNodeId == node.Id)
            {
                this.SelectedSun = option;
            }
        }

        this.HasStaleSun = selectedSunNodeId.HasValue && this.SunOptions.All(option => option.NodeId != selectedSunNodeId);
        this.StaleSunText = this.HasStaleSun ? $"Stale sun: {selectedSunNodeId:N}" : string.Empty;
    }

    private static SceneEnvironmentEdit BackgroundEdit(Vector3 color)
        => new(
            Optional<bool>.Unspecified,
            Optional<Guid?>.Unspecified,
            Optional<ExposureMode>.Unspecified,
            Optional<float>.Unspecified,
            Optional<float>.Unspecified,
            Optional<ToneMappingMode>.Unspecified,
            Optional<Vector3>.Supplied(color),
            Optional<SkyAtmosphereEnvironmentData>.Unspecified,
            Optional<PostProcessEnvironmentData>.Unspecified);

    private void ApplyPostProcessEditorValues(PostProcessEnvironmentData? value)
    {
        value ??= new();
        this.ExposureMode = value.ExposureMode;
        this.ExposureEnabled = value.ExposureEnabled;
        this.ManualExposureEv = value.ManualExposureEv;
        this.ExposureCompensation = value.ExposureCompensationEv;
        this.ExposureKey = value.ExposureKey;
        this.ToneMapping = value.ToneMapper;
        this.AutoExposureMeteringMode = value.AutoExposureMeteringMode;
        this.AutoExposureMinEv = value.AutoExposureMinEv;
        this.AutoExposureMaxEv = value.AutoExposureMaxEv;
        this.AutoExposureSpeedUp = value.AutoExposureSpeedUp;
        this.AutoExposureSpeedDown = value.AutoExposureSpeedDown;
        this.AutoExposureLowPercentile = value.AutoExposureLowPercentile;
        this.AutoExposureHighPercentile = value.AutoExposureHighPercentile;
        this.AutoExposureMinLogLuminance = value.AutoExposureMinLogLuminance;
        this.AutoExposureLogLuminanceRange = value.AutoExposureLogLuminanceRange;
        this.AutoExposureTargetLuminance = value.AutoExposureTargetLuminance;
        this.AutoExposureSpotMeterRadius = value.AutoExposureSpotMeterRadius;
        this.BloomIntensity = value.BloomIntensity;
        this.BloomThreshold = value.BloomThreshold;
        this.Saturation = value.Saturation;
        this.Contrast = value.Contrast;
        this.VignetteIntensity = value.VignetteIntensity;
        this.DisplayGamma = value.DisplayGamma;
    }

    private void ApplySkyAtmosphereEditorValues(SkyAtmosphereEnvironmentData? value)
    {
        value ??= new();
        this.PlanetRadiusKm = value.PlanetRadiusMeters / 1000.0f;
        this.AtmosphereHeightKm = value.AtmosphereHeightMeters / 1000.0f;
        this.GroundAlbedoR = value.GroundAlbedoRgb.X;
        this.GroundAlbedoG = value.GroundAlbedoRgb.Y;
        this.GroundAlbedoB = value.GroundAlbedoRgb.Z;
        this.RayleighScaleHeightKm = value.RayleighScaleHeightMeters / 1000.0f;
        this.MieScaleHeightKm = value.MieScaleHeightMeters / 1000.0f;
        this.MieAnisotropy = value.MieAnisotropy;
        this.SkyLuminanceR = value.SkyLuminanceFactorRgb.X;
        this.SkyLuminanceG = value.SkyLuminanceFactorRgb.Y;
        this.SkyLuminanceB = value.SkyLuminanceFactorRgb.Z;
        this.AerialPerspectiveDistanceScale = value.AerialPerspectiveDistanceScale;
        this.AerialScatteringStrength = value.AerialScatteringStrength;
        this.AerialPerspectiveStartDepthMeters = value.AerialPerspectiveStartDepthMeters;
        this.HeightFogContribution = value.HeightFogContribution;
        this.SunDiskEnabled = value.SunDiskEnabled;
    }

    private void NotifyBackgroundChanged()
    {
        this.OnPropertyChanged(nameof(this.BackgroundColor));
        this.OnPropertyChanged(nameof(this.BackgroundBrush));
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0f, 255f);
}

/// <summary>
/// Directional light option for scene sun binding.
/// </summary>
public sealed record SunLightOption(Guid? NodeId, string DisplayName);
