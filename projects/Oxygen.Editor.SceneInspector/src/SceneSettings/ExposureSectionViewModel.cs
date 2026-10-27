// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Microsoft.Extensions.Logging;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Pickers;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.SceneSettings;

/// <summary>Owns exposure modes, related bounds, curve policy and the scheduled texture catalog.</summary>
public sealed partial class ExposureSectionViewModel : ObservableObject, IDisposable
{
    private readonly IAssetFeed? assetProvider;
    private readonly IScheduler observerScheduler;
    private readonly ILogger logger;
    private IDisposable? assetSubscription;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ExposureSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    /// <param name="assetProvider">The shared texture catalog.</param>
    /// <param name="observerScheduler">The injected notification scheduler.</param>
    /// <param name="logger">Reports catalog refresh failures.</param>
    internal ExposureSectionViewModel(
        SceneEnvironmentEditOwner owner,
        IAssetFeed? assetProvider,
        IScheduler observerScheduler,
        ILogger logger)
    {
        this.logger = logger;
        this.EditOwner = owner;
        this.assetProvider = assetProvider;
        this.observerScheduler = observerScheduler;
        this.CurveEditor = new(value => this.AutoExposureCompensationCurve = value, () => owner.IsInputEnabled && owner.Scene is not null, owner, this.AutoExposureCompensationCurveDiagnostic);
    }

    [ObservableProperty]
    public partial ExposureMode ExposureMode { get; set; }

    [ObservableProperty]
    public partial bool ExposureEnabled { get; set; }

    [ObservableProperty]
    public partial double ManualExposureEv { get; set; }

    [ObservableProperty]
    public partial double ExposureCompensation { get; set; }

    [ObservableProperty]
    public partial double ExposureKey { get; set; }

    [ObservableProperty]
    public partial MeteringMode AutoExposureMeteringMode { get; set; }

    [ObservableProperty]
    public partial double AutoExposureMinEv { get; set; }

    [ObservableProperty]
    public partial double AutoExposureMaxEv { get; set; }

    [ObservableProperty]
    public partial double AutoExposureSpeedUp { get; set; }

    [ObservableProperty]
    public partial double AutoExposureSpeedDown { get; set; }

    [ObservableProperty]
    public partial double AutoExposureLowPercentile { get; set; }

    [ObservableProperty]
    public partial double AutoExposureHighPercentile { get; set; }

    [ObservableProperty]
    public partial double AutoExposureMinLogLuminance { get; set; }

    [ObservableProperty]
    public partial double AutoExposureLogLuminanceRange { get; set; }

    [ObservableProperty]
    public partial double AutoExposureTargetLuminance { get; set; }

    [ObservableProperty]
    public partial double AutoExposureSpotMeterRadius { get; set; }

    [ObservableProperty]
    public partial double AutoExposureBlackInfluence { get; set; }

    [ObservableProperty]
    public partial double AutoExposureTransitionDistanceEv { get; set; } = 1.5f;

    [ObservableProperty]
    public partial AssetPath? AutoExposureMeteringMask { get; set; }

    [ObservableProperty]
    public partial ImmutableArray<ExposureCompensationKeyData> AutoExposureCompensationCurve { get; set; } = [];

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the owned stable curve editor.</summary>
    public ExposureCompensationCurveEditorViewModel CurveEditor { get; }

    /// <summary>Gets existing exposure choices in their original order.</summary>
    public IReadOnlyList<ExposureMode> ExposureModes { get; } = [ExposureMode.Auto, ExposureMode.Manual, ExposureMode.ManualCamera];

    /// <summary>Gets existing metering choices.</summary>
    public IReadOnlyList<MeteringMode> MeteringModes { get; } = Enum.GetValues<MeteringMode>();

    /// <summary>Gets the textures the metering-mask picker offers, grouped as Engine, Packages and Content.</summary>
    public AssetPickerModel MeteringMaskChoices { get; } = new(static asset => AssetChoice.For(asset, "Texture", "\uE7C3"));

    /// <summary>Gets the authored reference name, None, or URI fallback.</summary>
    public string MeteringMaskDisplayName => this.AutoExposureMeteringMask is null ? "None"
        : this.MeteringMaskChoices.Find(this.AutoExposureMeteringMask)?.Name
            ?? Path.GetFileNameWithoutExtension(this.AutoExposureMeteringMask.Name);

    /// <summary>Gets a value indicating whether manual controls apply.</summary>
    public bool IsManualExposureVisible => this.ExposureMode == ExposureMode.Manual;

    /// <summary>Gets a value indicating whether automatic controls apply.</summary>
    public bool IsAutoExposureVisible => this.ExposureMode == ExposureMode.Auto;

    /// <summary>Gets canonical feedback for exposure enablement.</summary>
    public InspectorFieldDiagnostic ExposureEnabledDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.ExposureEnabled.Id);

    /// <summary>Gets canonical feedback for manual EV.</summary>
    public InspectorFieldDiagnostic ManualExposureEvDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.ManualExposureEv.Id);

    /// <summary>Gets canonical feedback for compensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.ExposureCompensation.Id);

    /// <summary>Gets canonical feedback for calibration.</summary>
    public InspectorFieldDiagnostic ExposureKeyDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.ExposureKey.Id);

    /// <summary>Gets canonical metering feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringModeDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureMeteringMode.Id);

    /// <summary>Gets related minimum-bound feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMinEvDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureMinEv.Id);

    /// <summary>Gets related maximum-bound feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMaxEvDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureMaxEv.Id);

    /// <summary>Gets adaptation feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedUpDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureSpeedUp.Id);

    /// <summary>Gets adaptation feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedDownDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureSpeedDown.Id);

    /// <summary>Gets related percentile feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureLowPercentileDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureLowPercentile.Id);

    /// <summary>Gets related percentile feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureHighPercentileDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureHighPercentile.Id);

    /// <summary>Gets histogram feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMinLogLuminanceDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureMinLogLuminance.Id);

    /// <summary>Gets histogram feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureLogLuminanceRangeDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureLogLuminanceRange.Id);

    /// <summary>Gets target feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureTargetLuminanceDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureTargetLuminance.Id);

    /// <summary>Gets spot-radius feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpotMeterRadiusDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureSpotMeterRadius.Id);

    /// <summary>Gets dark-sample feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureBlackInfluenceDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureBlackInfluence.Id);

    /// <summary>Gets transition feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureTransitionDistanceEvDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureTransitionDistanceEv.Id);

    /// <summary>Gets authored-reference feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringMaskDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureMeteringMask.Id);

    /// <summary>Gets curve feedback shared with the owned curve editor.</summary>
    public InspectorFieldDiagnostic AutoExposureCompensationCurveDiagnostic => this.Diagnostic(SceneProperties.SceneEnvironment.AutoExposureCompensationCurve.Id);

    /// <summary>Sets or clears the authored texture reference.</summary>
    /// <param name="textureUri">The authored texture identity, or null.</param>
    public void SetMeteringMask(AssetPath? textureUri) => this.AutoExposureMeteringMask = textureUri;

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.disposed = true;
        this.assetSubscription?.Dispose();
        this.assetSubscription = null;
        this.CurveEditor.Dispose();
    }

    /// <summary>Starts at most one scheduled texture feed for the owned section.</summary>
    internal void StartAssets()
    {
        if (this.disposed || this.assetProvider is null || this.assetSubscription is not null)
        {
            return;
        }

        this.assetSubscription = this.assetProvider.Items.ObserveOn(this.observerScheduler).Subscribe(this.UpdateMeteringMaskRows);
        _ = this.RefreshAssetsAsync();
    }

    /// <summary>Displays authored exposure state under the parent's model-refresh guard.</summary>
    /// <param name="value">The authored exposure snapshot.</param>
    internal void Refresh(PostProcessEnvironmentData value)
    {
        this.ExposureMode = value.ExposureMode;
        this.ExposureEnabled = value.ExposureEnabled;
        this.ManualExposureEv = value.ManualExposureEv;
        this.ExposureCompensation = value.ExposureCompensationEv;
        this.ExposureKey = value.ExposureKey;
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
        this.AutoExposureBlackInfluence = value.AutoExposureBlackInfluence;
        this.AutoExposureTransitionDistanceEv = value.AutoExposureTransitionDistanceEv;
        this.AutoExposureMeteringMask = value.AutoExposureMeteringMask;
        this.AutoExposureCompensationCurve = value.AutoExposureCompensationCurve;
        this.CurveEditor.Refresh(value.AutoExposureCompensationCurve);
    }

    partial void OnExposureModeChanged(ExposureMode value)
    {
        this.OnPropertyChanged(nameof(this.IsManualExposureVisible));
        this.OnPropertyChanged(nameof(this.IsAutoExposureVisible));
        this.EditOwner.Apply(SceneProperties.SceneEnvironment.ExposureMode, value);
    }

    partial void OnExposureEnabledChanged(bool value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.ExposureEnabled, value);

    partial void OnManualExposureEvChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.ManualExposureEv, value);

    partial void OnExposureCompensationChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.ExposureCompensation, value);

    partial void OnExposureKeyChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.ExposureKey, value);

    partial void OnAutoExposureMeteringModeChanged(MeteringMode value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureMeteringMode, value);

    partial void OnAutoExposureMinEvChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureMinEv, value);

    partial void OnAutoExposureMaxEvChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureMaxEv, value);

    partial void OnAutoExposureSpeedUpChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureSpeedUp, value);

    partial void OnAutoExposureSpeedDownChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureSpeedDown, value);

    partial void OnAutoExposureLowPercentileChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureLowPercentile, value);

    partial void OnAutoExposureHighPercentileChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureHighPercentile, value);

    partial void OnAutoExposureMinLogLuminanceChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureMinLogLuminance, value);

    partial void OnAutoExposureLogLuminanceRangeChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureLogLuminanceRange, value);

    partial void OnAutoExposureTargetLuminanceChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureTargetLuminance, value);

    partial void OnAutoExposureSpotMeterRadiusChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureSpotMeterRadius, value);

    partial void OnAutoExposureBlackInfluenceChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureBlackInfluence, value);

    partial void OnAutoExposureTransitionDistanceEvChanged(double value) => this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureTransitionDistanceEv, value);

    partial void OnAutoExposureMeteringMaskChanged(AssetPath? value)
    {
        this.OnPropertyChanged(nameof(this.MeteringMaskDisplayName));
        this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureMeteringMask, value);
    }

    partial void OnAutoExposureCompensationCurveChanged(ImmutableArray<ExposureCompensationKeyData> value)
    {
        this.EditOwner.Apply(SceneProperties.SceneEnvironment.AutoExposureCompensationCurve, value);
        this.CurveEditor.Refresh(this.AutoExposureCompensationCurve);
    }

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);

    private async Task RefreshAssetsAsync()
    {
        try
        {
            if (this.assetProvider is { } provider)
            {
                await provider.RefreshAsync().ConfigureAwait(false);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            this.LogMeteringMaskRefreshFailed(exception);
        }
    }

    private void UpdateMeteringMaskRows(IReadOnlyList<AssetEntry> assets)
    {
        if (this.disposed)
        {
            return;
        }

        this.MeteringMaskChoices.Update(AssetRule.For(AssetKind.Texture).Apply(assets));
        this.OnPropertyChanged(nameof(this.MeteringMaskDisplayName));
    }
}
