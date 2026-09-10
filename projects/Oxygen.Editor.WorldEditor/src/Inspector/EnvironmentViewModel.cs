// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Schemas;
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
    Func<SceneDocumentCommandContext?>? commandContextProvider = null) : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();

    private InspectorEditSessionCoordinator? edits;
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

    /// <summary>Gets current diagnostics for AtmosphereEnabled.</summary>
    public InspectorFieldDiagnostic AtmosphereEnabledDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ExposureEnabled.</summary>
    public InspectorFieldDiagnostic ExposureEnabledDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ManualExposureEv.</summary>
    public InspectorFieldDiagnostic ManualExposureEvDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ExposureCompensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ExposureKey.</summary>
    public InspectorFieldDiagnostic ExposureKeyDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureKey.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureMeteringMode.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringModeDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureMinEv.</summary>
    public InspectorFieldDiagnostic AutoExposureMinEvDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureMaxEv.</summary>
    public InspectorFieldDiagnostic AutoExposureMaxEvDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureSpeedUp.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedUpDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureSpeedDown.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedDownDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureLowPercentile.</summary>
    public InspectorFieldDiagnostic AutoExposureLowPercentileDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureHighPercentile.</summary>
    public InspectorFieldDiagnostic AutoExposureHighPercentileDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureMinLogLuminance.</summary>
    public InspectorFieldDiagnostic AutoExposureMinLogLuminanceDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureLogLuminanceRange.</summary>
    public InspectorFieldDiagnostic AutoExposureLogLuminanceRangeDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureTargetLuminance.</summary>
    public InspectorFieldDiagnostic AutoExposureTargetLuminanceDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AutoExposureSpotMeterRadius.</summary>
    public InspectorFieldDiagnostic AutoExposureSpotMeterRadiusDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for BloomIntensity.</summary>
    public InspectorFieldDiagnostic BloomIntensityDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BloomIntensity.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for BloomThreshold.</summary>
    public InspectorFieldDiagnostic BloomThresholdDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BloomThreshold.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for Saturation.</summary>
    public InspectorFieldDiagnostic SaturationDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.Saturation.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for Contrast.</summary>
    public InspectorFieldDiagnostic ContrastDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.Contrast.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for VignetteIntensity.</summary>
    public InspectorFieldDiagnostic VignetteIntensityDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.VignetteIntensity.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for DisplayGamma.</summary>
    public InspectorFieldDiagnostic DisplayGammaDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.DisplayGamma.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for PlanetRadiusKm.</summary>
    public InspectorFieldDiagnostic PlanetRadiusKmDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AtmosphereHeightKm.</summary>
    public InspectorFieldDiagnostic AtmosphereHeightKmDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for GroundAlbedoR.</summary>
    public InspectorFieldDiagnostic GroundAlbedoRDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for GroundAlbedoG.</summary>
    public InspectorFieldDiagnostic GroundAlbedoGDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for GroundAlbedoB.</summary>
    public InspectorFieldDiagnostic GroundAlbedoBDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for RayleighScaleHeightKm.</summary>
    public InspectorFieldDiagnostic RayleighScaleHeightKmDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for MieScaleHeightKm.</summary>
    public InspectorFieldDiagnostic MieScaleHeightKmDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for MieAnisotropy.</summary>
    public InspectorFieldDiagnostic MieAnisotropyDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SkyLuminanceR.</summary>
    public InspectorFieldDiagnostic SkyLuminanceRDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SkyLuminanceG.</summary>
    public InspectorFieldDiagnostic SkyLuminanceGDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SkyLuminanceB.</summary>
    public InspectorFieldDiagnostic SkyLuminanceBDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AerialPerspectiveDistanceScale.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveDistanceScaleDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AerialScatteringStrength.</summary>
    public InspectorFieldDiagnostic AerialScatteringStrengthDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AerialPerspectiveStartDepthMeters.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveStartDepthMetersDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for HeightFogContribution.</summary>
    public InspectorFieldDiagnostic HeightFogContributionDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SunDiskEnabled.</summary>
    public InspectorFieldDiagnostic SunDiskEnabledDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for BackgroundR.</summary>
    public InspectorFieldDiagnostic BackgroundRDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for BackgroundG.</summary>
    public InspectorFieldDiagnostic BackgroundGDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for BackgroundB.</summary>
    public InspectorFieldDiagnostic BackgroundBDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id) ?? this.unboundDiagnostic;

    /// <inheritdoc/>
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Environment";

    /// <inheritdoc />
    public override string Description => "Scene atmosphere, sun, exposure, tone mapping, and background intent.";

    /// <summary>Gets completion of submitted inspector edits.</summary>
    internal Task PendingEdits => this.edits?.Pending ?? Task.CompletedTask;

    /// <inheritdoc/>
    public void BeginEditSession(string field, DroidNet.Controls.NumberBoxEditInteractionKind interaction)
        => this.edits?.Begin(field, interaction);

    /// <inheritdoc/>
    public void CompleteEditSession(DroidNet.Controls.NumberBoxEditSessionEventArgs args)
        => this.edits?.Complete(args);

    /// <inheritdoc/>
    public void EndEditSession(DroidNet.Controls.NumberBoxEditCompletionKind completion)
        => this.edits?.End(completion);

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Sets the scene context used when no node is selected.
    /// </summary>
    /// <param name="value">The scene to edit, or <see langword="null"/> when a node selection owns the inspector.</param>
    public void SetScene(Scene? value)
    {
        if (ReferenceEquals(this.scene, value))
        {
            return;
        }

        if (this.edits is null && commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Environment", this.RefreshFromScene, environment: true);
        }

        this.edits?.Bind(value is null ? [] : [value.Id]);
        this.scene = value;
        this.RefreshFromScene();
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        if (items.Count > 0)
        {
            this.SetScene(value: null);
            return;
        }

        this.RefreshFromScene();
    }

    /// <summary>
    /// Applies the selected background color from the color picker.
    /// </summary>
    /// <param name="color">The selected background color.</param>
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
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, new Vector3(r, g, b));
    }

    /// <summary>Releases the active environment edit session.</summary>
    /// <param name="disposing">Whether managed resources should be released.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.edits?.Dispose();
        }
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0f, 255f);

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

        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SunNodeId, (Guid?)null);
    }

    partial void OnAtmosphereEnabledChanged(bool value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled, value);

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

        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SunNodeId, value.NodeId);
    }

    partial void OnExposureModeChanged(ExposureMode value)
    {
        this.OnPropertyChanged(nameof(this.IsManualExposureVisible));
        this.OnPropertyChanged(nameof(this.IsAutoExposureVisible));
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ExposureMode, value);
    }

    partial void OnExposureEnabledChanged(bool value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled, value);

    partial void OnManualExposureEvChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv, value);

    partial void OnExposureCompensationChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation, value);

    partial void OnExposureKeyChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ExposureKey, value);

    partial void OnToneMappingChanged(ToneMappingMode value)
    {
        this.OnPropertyChanged(nameof(this.IsToneMappingControlsVisible));
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.ToneMapping, value);
    }

    partial void OnAutoExposureMeteringModeChanged(MeteringMode value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode, value);

    partial void OnAutoExposureMinEvChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv, value);

    partial void OnAutoExposureMaxEvChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv, value);

    partial void OnAutoExposureSpeedUpChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp, value);

    partial void OnAutoExposureSpeedDownChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown, value);

    partial void OnAutoExposureLowPercentileChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile, value);

    partial void OnAutoExposureHighPercentileChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile, value);

    partial void OnAutoExposureMinLogLuminanceChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance, value);

    partial void OnAutoExposureLogLuminanceRangeChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange, value);

    partial void OnAutoExposureTargetLuminanceChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance, value);

    partial void OnAutoExposureSpotMeterRadiusChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius, value);

    partial void OnBloomIntensityChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BloomIntensity, value);

    partial void OnBloomThresholdChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BloomThreshold, value);

    partial void OnSaturationChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.Saturation, value);

    partial void OnContrastChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.Contrast, value);

    partial void OnVignetteIntensityChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.VignetteIntensity, value);

    partial void OnDisplayGammaChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.DisplayGamma, value);

    partial void OnBackgroundRChanged(float value) => this.ApplyBackgroundAxisEdit(value, this.BackgroundG, this.BackgroundB);

    partial void OnBackgroundGChanged(float value) => this.ApplyBackgroundAxisEdit(this.BackgroundR, value, this.BackgroundB);

    partial void OnBackgroundBChanged(float value) => this.ApplyBackgroundAxisEdit(this.BackgroundR, this.BackgroundG, value);

    partial void OnPlanetRadiusKmChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters, value * 1000.0f);

    partial void OnAtmosphereHeightKmChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters, value * 1000.0f);

    partial void OnGroundAlbedoRChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(value, this.GroundAlbedoG, this.GroundAlbedoB));

    partial void OnGroundAlbedoGChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(this.GroundAlbedoR, value, this.GroundAlbedoB));

    partial void OnGroundAlbedoBChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(this.GroundAlbedoR, this.GroundAlbedoG, value));

    partial void OnRayleighScaleHeightKmChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters, value * 1000.0f);

    partial void OnMieScaleHeightKmChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters, value * 1000.0f);

    partial void OnMieAnisotropyChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy, value);

    partial void OnSkyLuminanceRChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(value, this.SkyLuminanceG, this.SkyLuminanceB));

    partial void OnSkyLuminanceGChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(this.SkyLuminanceR, value, this.SkyLuminanceB));

    partial void OnSkyLuminanceBChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SkyLuminance, new Vector3(this.SkyLuminanceR, this.SkyLuminanceG, value));

    partial void OnAerialPerspectiveDistanceScaleChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale, value);

    partial void OnAerialScatteringStrengthChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength, value);

    partial void OnAerialPerspectiveStartDepthMetersChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters, value);

    partial void OnHeightFogContributionChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution, value);

    partial void OnSunDiskEnabledChanged(bool value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled, value);

    private void ApplyBackgroundAxisEdit(float r, float g, float b)
    {
        this.NotifyBackgroundChanged();
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, new Vector3(r, g, b));
    }

    private void ApplyEnvironmentProperty<T>(PropertyId<T> propertyId, T value)
    {
        if (this.isApplyingEditorValues)
        {
            this.edits?.ModelChanged(propertyId.Id);
        }
        else if (this.scene is not null)
        {
            this.edits?.Submit(PropertyEdit.Single(propertyId, value));
        }
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
        var none = new SunLightOption(NodeId: null, "None");
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
}
