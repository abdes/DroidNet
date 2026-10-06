// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector.Environment;

/// <summary>Owns exposure modes, related bounds, curve policy and the scheduled texture catalog.</summary>
public sealed partial class ExposureSectionViewModel : ObservableObject, IDisposable
{
    private readonly IContentBrowserAssetProvider? assetProvider;
    private readonly IScheduler observerScheduler;
    private IDisposable? assetSubscription;
    private bool disposed;

    /// <summary>Initializes a new instance of the <see cref="ExposureSectionViewModel"/> class.</summary>
    /// <param name="owner">The borrowed scene edit lifetime.</param>
    /// <param name="assetProvider">The shared texture catalog.</param>
    /// <param name="observerScheduler">The injected notification scheduler.</param>
    internal ExposureSectionViewModel(
        SceneEnvironmentEditOwner owner,
        IContentBrowserAssetProvider? assetProvider,
        IScheduler observerScheduler)
    {
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
    public partial float ManualExposureEv { get; set; }

    [ObservableProperty]
    public partial float ExposureCompensation { get; set; }

    [ObservableProperty]
    public partial float ExposureKey { get; set; }

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
    public partial float AutoExposureBlackInfluence { get; set; }

    [ObservableProperty]
    public partial float AutoExposureTransitionDistanceEv { get; set; } = 1.5f;

    [ObservableProperty]
    public partial Uri? AutoExposureMeteringMask { get; set; }

    [ObservableProperty]
    public partial ImmutableArray<ExposureCompensationKeyData> AutoExposureCompensationCurve { get; set; } = [];

    [ObservableProperty]
    public partial string MeteringMaskSearchText { get; set; } = string.Empty;

    /// <summary>Gets the borrowed scene edit owner.</summary>
    public SceneEnvironmentEditOwner EditOwner { get; }

    /// <summary>Gets the owned stable curve editor.</summary>
    public ExposureCompensationCurveEditorViewModel CurveEditor { get; }

    /// <summary>Gets existing exposure choices in their original order.</summary>
    public IReadOnlyList<ExposureMode> ExposureModes { get; } = [ExposureMode.Auto, ExposureMode.Manual, ExposureMode.ManualCamera];

    /// <summary>Gets existing metering choices.</summary>
    public IReadOnlyList<MeteringMode> MeteringModes { get; } = Enum.GetValues<MeteringMode>();

    /// <summary>Gets stable texture rows retained across status updates.</summary>
    public ObservableCollection<AssetPickerRow> MeteringMaskRows { get; } = [];

    /// <summary>Gets texture rows matching the current query without replacing their identities.</summary>
    public IReadOnlyList<AssetPickerRow> FilteredMeteringMaskRows => string.IsNullOrWhiteSpace(this.MeteringMaskSearchText)
        ? this.MeteringMaskRows.ToArray()
        : this.MeteringMaskRows.Where(row => row.Item.Name.Contains(this.MeteringMaskSearchText, StringComparison.OrdinalIgnoreCase)
            || row.Item.DisplayPath.Contains(this.MeteringMaskSearchText, StringComparison.OrdinalIgnoreCase)).ToArray();

    /// <summary>Gets the authored reference name, None, or URI fallback.</summary>
    public string MeteringMaskDisplayName => this.AutoExposureMeteringMask is null ? "None"
        : this.MeteringMaskRows.FirstOrDefault(row => row.Item.Uri == this.AutoExposureMeteringMask)?.Item.Name
            ?? Path.GetFileNameWithoutExtension(this.AutoExposureMeteringMask.AbsolutePath);

    /// <summary>Gets a value indicating whether manual controls apply.</summary>
    public bool IsManualExposureVisible => this.ExposureMode == ExposureMode.Manual;

    /// <summary>Gets a value indicating whether automatic controls apply.</summary>
    public bool IsAutoExposureVisible => this.ExposureMode == ExposureMode.Auto;

    /// <summary>Gets canonical feedback for exposure enablement.</summary>
    public InspectorFieldDiagnostic ExposureEnabledDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled.Id);

    /// <summary>Gets canonical feedback for manual EV.</summary>
    public InspectorFieldDiagnostic ManualExposureEvDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv.Id);

    /// <summary>Gets canonical feedback for compensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation.Id);

    /// <summary>Gets canonical feedback for calibration.</summary>
    public InspectorFieldDiagnostic ExposureKeyDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.ExposureKey.Id);

    /// <summary>Gets canonical metering feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringModeDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode.Id);

    /// <summary>Gets related minimum-bound feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMinEvDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv.Id);

    /// <summary>Gets related maximum-bound feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMaxEvDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv.Id);

    /// <summary>Gets adaptation feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedUpDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp.Id);

    /// <summary>Gets adaptation feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedDownDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown.Id);

    /// <summary>Gets related percentile feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureLowPercentileDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile.Id);

    /// <summary>Gets related percentile feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureHighPercentileDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile.Id);

    /// <summary>Gets histogram feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMinLogLuminanceDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance.Id);

    /// <summary>Gets histogram feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureLogLuminanceRangeDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange.Id);

    /// <summary>Gets target feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureTargetLuminanceDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance.Id);

    /// <summary>Gets spot-radius feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureSpotMeterRadiusDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius.Id);

    /// <summary>Gets dark-sample feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureBlackInfluenceDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureBlackInfluence.Id);

    /// <summary>Gets transition feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureTransitionDistanceEvDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureTransitionDistanceEv.Id);

    /// <summary>Gets authored-reference feedback.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringMaskDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMask.Id);

    /// <summary>Gets curve feedback shared with the owned curve editor.</summary>
    public InspectorFieldDiagnostic AutoExposureCompensationCurveDiagnostic => this.Diagnostic(SceneDocumentCommandService.SceneEnvironment.AutoExposureCompensationCurve.Id);

    /// <summary>Sets or clears the authored texture reference.</summary>
    /// <param name="textureUri">The authored texture identity, or null.</param>
    public void SetMeteringMask(Uri? textureUri) => this.AutoExposureMeteringMask = textureUri;

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
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.ExposureMode, value);
    }

    partial void OnExposureEnabledChanged(bool value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled, value);

    partial void OnManualExposureEvChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv, value);

    partial void OnExposureCompensationChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation, value);

    partial void OnExposureKeyChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.ExposureKey, value);

    partial void OnAutoExposureMeteringModeChanged(MeteringMode value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode, value);

    partial void OnAutoExposureMinEvChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv, value);

    partial void OnAutoExposureMaxEvChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv, value);

    partial void OnAutoExposureSpeedUpChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp, value);

    partial void OnAutoExposureSpeedDownChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown, value);

    partial void OnAutoExposureLowPercentileChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile, value);

    partial void OnAutoExposureHighPercentileChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile, value);

    partial void OnAutoExposureMinLogLuminanceChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance, value);

    partial void OnAutoExposureLogLuminanceRangeChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange, value);

    partial void OnAutoExposureTargetLuminanceChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance, value);

    partial void OnAutoExposureSpotMeterRadiusChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius, value);

    partial void OnAutoExposureBlackInfluenceChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureBlackInfluence, value);

    partial void OnAutoExposureTransitionDistanceEvChanged(float value) => this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureTransitionDistanceEv, value);

    partial void OnAutoExposureMeteringMaskChanged(Uri? value)
    {
        this.OnPropertyChanged(nameof(this.MeteringMaskDisplayName));
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMask, value);
    }

    partial void OnAutoExposureCompensationCurveChanged(ImmutableArray<ExposureCompensationKeyData> value)
    {
        this.EditOwner.Apply(SceneDocumentCommandService.SceneEnvironment.AutoExposureCompensationCurve, value);
        this.CurveEditor.Refresh(this.AutoExposureCompensationCurve);
    }

    partial void OnMeteringMaskSearchTextChanged(string value) => this.OnPropertyChanged(nameof(this.FilteredMeteringMaskRows));

    private InspectorFieldDiagnostic Diagnostic(PropertyId property) => this.EditOwner.Diagnostics.Get(property);

    private async Task RefreshAssetsAsync()
    {
        try
        {
            if (this.assetProvider is { } provider)
            {
                await provider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(false);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            Debug.WriteLine($"[ExposureSectionViewModel] Metering mask asset refresh failed: {exception}");
        }
    }

    private void UpdateMeteringMaskRows(IReadOnlyList<ContentBrowserAssetItem> assets)
    {
        if (this.disposed)
        {
            return;
        }

        var textures = assets.Where(static asset => asset.Kind == AssetKind.Texture && !asset.IsBuiltin)
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase).ToArray();
        var wanted = textures.Select(static asset => asset.IdentityUri.AbsoluteUri).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var removed in this.MeteringMaskRows.Where(row => !wanted.Contains(row.Item.Uri.AbsoluteUri)).ToArray())
        {
            _ = this.MeteringMaskRows.Remove(removed);
        }

        for (var index = 0; index < textures.Length; index++)
        {
            var asset = textures[index];
            var item = new AssetPickerItem(asset.DisplayName, asset.IdentityUri, "Texture · " + asset.PrimaryBadge, asset.DisplayPath, AssetPickerGroup.Content, asset.IsSelectable, "\uE7C3");
            var row = this.MeteringMaskRows.FirstOrDefault(candidate => candidate.Item.Uri == asset.IdentityUri);
            if (row is null)
            {
                this.MeteringMaskRows.Insert(Math.Min(index, this.MeteringMaskRows.Count), new(item));
            }
            else
            {
                row.Update(item);
                var currentIndex = this.MeteringMaskRows.IndexOf(row);
                if (currentIndex != index)
                {
                    this.MeteringMaskRows.Move(currentIndex, Math.Min(index, this.MeteringMaskRows.Count - 1));
                }
            }
        }

        this.OnPropertyChanged(nameof(this.FilteredMeteringMaskRows));
        this.OnPropertyChanged(nameof(this.MeteringMaskDisplayName));
    }
}
