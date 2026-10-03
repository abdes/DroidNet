// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Numerics;
using System.Reactive.Concurrency;
using System.Reactive.Linq;
using CommunityToolkit.Mvvm.ComponentModel;
using CommunityToolkit.Mvvm.Input;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// Scene-level Environment inspector view model.
/// </summary>
/// <remarks>
/// Asset notifications use the supplied observer scheduler. UI composition supplies
/// its dispatcher scheduler; standalone models deliver notifications synchronously.
/// </remarks>
public partial class EnvironmentViewModel(
    ISceneDocumentCommandService? commandService = null,
    Func<SceneDocumentCommandContext?>? commandContextProvider = null,
    IContentBrowserAssetProvider? assetProvider = null,
    Func<Guid, Task>? inspectSceneNode = null,
    IScheduler? observerScheduler = null) : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private readonly InspectorFieldDiagnostics fieldDiagnostics = new();
    private readonly IContentBrowserAssetProvider? sharedAssetProvider = assetProvider;
    private readonly Func<Guid, Task>? inspectSceneNode = inspectSceneNode;
    private readonly IScheduler observerScheduler = observerScheduler ?? ImmediateScheduler.Instance;

    private InspectorEditSessionCoordinator? edits;
    private InspectorEditSessionCoordinator? lightAssignments;
    private IDisposable? meteringMaskAssetSubscription;
    private Scene? scene;
    private bool isApplyingEditorValues;
    private readonly System.Runtime.CompilerServices.ConditionalWeakTable<Scene, Dictionary<Guid, PropertyEdit>> initialAtmosphereSources = new();

    /// <summary>Gets the canonical editor for the bound primary source.</summary>
    public DirectionalLightViewModel PrimaryAtmosphereSource { get; } = new(commandService, commandContextProvider);

    /// <summary>Gets the canonical editor for the bound secondary source.</summary>
    public DirectionalLightViewModel SecondaryAtmosphereSource { get; } = new(commandService, commandContextProvider);

    /// <summary>Gets the scene identity displayed above the property search.</summary>
    public string SceneName => this.scene?.Name ?? string.Empty;

    /// <summary>Occurs when explicit diagnostic navigation requests focus in this inspector.</summary>
    public event EventHandler? FieldFocusRequested;

    [ObservableProperty]
    public partial bool AtmosphereEnabled { get; set; }

    [ObservableProperty]
    public partial SunLightOption? SelectedSun { get; set; }

    [ObservableProperty]
    public partial SunLightOption? SelectedSecondarySun { get; set; }

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
    public partial float AutoExposureBlackInfluence { get; set; }

    [ObservableProperty]
    public partial float AutoExposureTransitionDistanceEv { get; set; } = 1.5f;

    [ObservableProperty]
    public partial Uri? AutoExposureMeteringMask { get; set; }

    [ObservableProperty]
    public partial ImmutableArray<ExposureCompensationKeyData> AutoExposureCompensationCurve { get; set; } = [];

    [ObservableProperty]
    public partial string MeteringMaskSearchText { get; set; } = string.Empty;

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

    /// <summary>Gets the available secondary atmosphere light options.</summary>
    public ObservableCollection<SunLightOption> SecondarySunOptions { get; } = [];

    /// <summary>Gets whether the primary role has an inspectable source.</summary>
    public bool HasPrimaryAtmosphereSource => this.SelectedSun?.NodeId is not null;

    /// <summary>Gets whether the secondary role has an inspectable source.</summary>
    public bool HasSecondaryAtmosphereSource => this.SelectedSecondarySun?.NodeId is not null;

    /// <summary>Gets texture choices from the shared content-browser asset catalog.</summary>
    public ObservableCollection<AssetPickerRow> MeteringMaskRows { get; } = [];

    /// <summary>Gets the editable exposure compensation curve keys.</summary>
    internal ObservableCollection<ExposureCompensationKeyViewModel> AutoExposureCurveKeys { get; } = [];

    /// <summary>Gets texture picker entries matching the current search text.</summary>
    public IReadOnlyList<AssetPickerRow> FilteredMeteringMaskRows
        => string.IsNullOrWhiteSpace(this.MeteringMaskSearchText)
            ? this.MeteringMaskRows.ToArray()
            : this.MeteringMaskRows.Where(row => row.Item.Name.Contains(this.MeteringMaskSearchText, StringComparison.OrdinalIgnoreCase)
                || row.Item.DisplayPath.Contains(this.MeteringMaskSearchText, StringComparison.OrdinalIgnoreCase)).ToArray();

    /// <summary>Gets the current metering mask name or its authored URI fallback.</summary>
    public string MeteringMaskDisplayName
        => this.AutoExposureMeteringMask is null
            ? "None"
            : this.MeteringMaskRows.FirstOrDefault(row => row.Item.Uri == this.AutoExposureMeteringMask)?.Item.Name
                ?? Path.GetFileNameWithoutExtension(this.AutoExposureMeteringMask.AbsolutePath);

    /// <summary>
    /// Gets the authored linear RGB background color.
    /// </summary>
    public Vector3 BackgroundColor => new(this.BackgroundR, this.BackgroundG, this.BackgroundB);

    /// <summary>Gets the authored linear RGB ground albedo.</summary>
    public Vector3 GroundAlbedoColor => new(this.GroundAlbedoR, this.GroundAlbedoG, this.GroundAlbedoB);

    /// <summary>
    /// Gets a value indicating whether manual exposure controls apply to the current mode.
    /// </summary>
    public bool IsManualExposureVisible => this.ExposureMode == ExposureMode.Manual;

    /// <summary>
    /// Gets a value indicating whether auto exposure controls apply to the current mode.
    /// </summary>
    public bool IsAutoExposureVisible => this.ExposureMode == ExposureMode.Auto;

    /// <summary>
    /// Gets a value indicating whether tone-mapping dependent controls apply to the current mode.
    /// </summary>
    public bool IsToneMappingControlsVisible => this.ToneMapping != ToneMappingMode.None;

    /// <summary>Gets current diagnostics for AtmosphereEnabled.</summary>
    public InspectorFieldDiagnostic AtmosphereEnabledDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled.Id);

    /// <summary>Gets current feedback for the scene's sun reference.</summary>
    public InspectorFieldDiagnostic SunReferenceDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot.Id);

    /// <summary>Gets current diagnostics for ExposureEnabled.</summary>
    public InspectorFieldDiagnostic ExposureEnabledDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureEnabled.Id);

    /// <summary>Gets current diagnostics for ManualExposureEv.</summary>
    public InspectorFieldDiagnostic ManualExposureEvDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ManualExposureEv.Id);

    /// <summary>Gets current diagnostics for ExposureCompensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureCompensation.Id);

    /// <summary>Gets current diagnostics for ExposureKey.</summary>
    public InspectorFieldDiagnostic ExposureKeyDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.ExposureKey.Id);

    /// <summary>Gets current diagnostics for AutoExposureMeteringMode.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringModeDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMode.Id);

    /// <summary>Gets current diagnostics for AutoExposureMinEv.</summary>
    public InspectorFieldDiagnostic AutoExposureMinEvDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv.Id);

    /// <summary>Gets current diagnostics for AutoExposureMaxEv.</summary>
    public InspectorFieldDiagnostic AutoExposureMaxEvDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv.Id);

    /// <summary>Gets current diagnostics for AutoExposureSpeedUp.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedUpDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedUp.Id);

    /// <summary>Gets current diagnostics for AutoExposureSpeedDown.</summary>
    public InspectorFieldDiagnostic AutoExposureSpeedDownDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpeedDown.Id);

    /// <summary>Gets current diagnostics for AutoExposureLowPercentile.</summary>
    public InspectorFieldDiagnostic AutoExposureLowPercentileDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile.Id);

    /// <summary>Gets current diagnostics for AutoExposureHighPercentile.</summary>
    public InspectorFieldDiagnostic AutoExposureHighPercentileDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile.Id);

    /// <summary>Gets current diagnostics for AutoExposureMinLogLuminance.</summary>
    public InspectorFieldDiagnostic AutoExposureMinLogLuminanceDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinLogLuminance.Id);

    /// <summary>Gets current diagnostics for AutoExposureLogLuminanceRange.</summary>
    public InspectorFieldDiagnostic AutoExposureLogLuminanceRangeDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureLogLuminanceRange.Id);

    /// <summary>Gets current diagnostics for AutoExposureTargetLuminance.</summary>
    public InspectorFieldDiagnostic AutoExposureTargetLuminanceDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureTargetLuminance.Id);

    /// <summary>Gets current diagnostics for AutoExposureSpotMeterRadius.</summary>
    public InspectorFieldDiagnostic AutoExposureSpotMeterRadiusDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureSpotMeterRadius.Id);

    /// <summary>Gets current diagnostics for AutoExposureBlackInfluence.</summary>
    public InspectorFieldDiagnostic AutoExposureBlackInfluenceDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureBlackInfluence.Id);

    /// <summary>Gets current diagnostics for AutoExposureTransitionDistanceEv.</summary>
    public InspectorFieldDiagnostic AutoExposureTransitionDistanceEvDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureTransitionDistanceEv.Id);

    /// <summary>Gets current diagnostics for AutoExposureMeteringMask.</summary>
    public InspectorFieldDiagnostic AutoExposureMeteringMaskDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMask.Id);

    /// <summary>Gets current diagnostics for AutoExposureCompensationCurve.</summary>
    public InspectorFieldDiagnostic AutoExposureCompensationCurveDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AutoExposureCompensationCurve.Id);

    /// <summary>Gets current diagnostics for BloomIntensity.</summary>
    public InspectorFieldDiagnostic BloomIntensityDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BloomIntensity.Id);

    /// <summary>Gets current diagnostics for BloomThreshold.</summary>
    public InspectorFieldDiagnostic BloomThresholdDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BloomThreshold.Id);

    /// <summary>Gets current diagnostics for Saturation.</summary>
    public InspectorFieldDiagnostic SaturationDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.Saturation.Id);

    /// <summary>Gets current diagnostics for Contrast.</summary>
    public InspectorFieldDiagnostic ContrastDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.Contrast.Id);

    /// <summary>Gets current diagnostics for VignetteIntensity.</summary>
    public InspectorFieldDiagnostic VignetteIntensityDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.VignetteIntensity.Id);

    /// <summary>Gets current diagnostics for DisplayGamma.</summary>
    public InspectorFieldDiagnostic DisplayGammaDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.DisplayGamma.Id);

    /// <summary>Gets current diagnostics for PlanetRadiusKm.</summary>
    public InspectorFieldDiagnostic PlanetRadiusKmDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.PlanetRadiusMeters.Id);

    /// <summary>Gets current diagnostics for AtmosphereHeightKm.</summary>
    public InspectorFieldDiagnostic AtmosphereHeightKmDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AtmosphereHeightMeters.Id);

    /// <summary>Gets current diagnostics for GroundAlbedoR.</summary>
    public InspectorFieldDiagnostic GroundAlbedoRDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id);

    /// <summary>Gets current diagnostics for GroundAlbedoG.</summary>
    public InspectorFieldDiagnostic GroundAlbedoGDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id);

    /// <summary>Gets current diagnostics for GroundAlbedoB.</summary>
    public InspectorFieldDiagnostic GroundAlbedoBDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo.Id);

    /// <summary>Gets current diagnostics for RayleighScaleHeightKm.</summary>
    public InspectorFieldDiagnostic RayleighScaleHeightKmDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.RayleighScaleHeightMeters.Id);

    /// <summary>Gets current diagnostics for MieScaleHeightKm.</summary>
    public InspectorFieldDiagnostic MieScaleHeightKmDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.MieScaleHeightMeters.Id);

    /// <summary>Gets current diagnostics for MieAnisotropy.</summary>
    public InspectorFieldDiagnostic MieAnisotropyDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.MieAnisotropy.Id);

    /// <summary>Gets current diagnostics for SkyLuminanceR.</summary>
    public InspectorFieldDiagnostic SkyLuminanceRDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id);

    /// <summary>Gets current diagnostics for SkyLuminanceG.</summary>
    public InspectorFieldDiagnostic SkyLuminanceGDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id);

    /// <summary>Gets current diagnostics for SkyLuminanceB.</summary>
    public InspectorFieldDiagnostic SkyLuminanceBDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SkyLuminance.Id);

    /// <summary>Gets current diagnostics for AerialPerspectiveDistanceScale.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveDistanceScaleDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveDistanceScale.Id);

    /// <summary>Gets current diagnostics for AerialScatteringStrength.</summary>
    public InspectorFieldDiagnostic AerialScatteringStrengthDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialScatteringStrength.Id);

    /// <summary>Gets current diagnostics for AerialPerspectiveStartDepthMeters.</summary>
    public InspectorFieldDiagnostic AerialPerspectiveStartDepthMetersDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id);

    /// <summary>Gets current diagnostics for HeightFogContribution.</summary>
    public InspectorFieldDiagnostic HeightFogContributionDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.HeightFogContribution.Id);

    /// <summary>Gets current diagnostics for SunDiskEnabled.</summary>
    public InspectorFieldDiagnostic SunDiskEnabledDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.SunDiskEnabled.Id);

    /// <summary>Gets current diagnostics for BackgroundR.</summary>
    public InspectorFieldDiagnostic BackgroundRDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id);

    /// <summary>Gets current diagnostics for BackgroundG.</summary>
    public InspectorFieldDiagnostic BackgroundGDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id);

    /// <summary>Gets current diagnostics for BackgroundB.</summary>
    public InspectorFieldDiagnostic BackgroundBDiagnostic => this.fieldDiagnostics.Get(SceneDocumentCommandService.SceneEnvironment.BackgroundColor.Id);

    /// <inheritdoc/>
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Environment";

    /// <inheritdoc />
    public override string Description => "Scene atmosphere, sun, exposure, tone mapping, and background intent.";

    /// <summary>Gets a field awaiting view creation or layout.</summary>
    internal string? PendingFieldFocus { get; private set; }

    /// <summary>Gets completion of submitted inspector edits.</summary>
    internal Task PendingEdits => Task.WhenAll(
        this.edits?.Pending ?? Task.CompletedTask,
        this.lightAssignments?.Pending ?? Task.CompletedTask);

    /// <summary>Validates an Aerial Start candidate and exposes feedback on its field.</summary>
    /// <param name="value">The proposed distance in meters.</param>
    /// <returns>Whether the field can accept the candidate.</returns>
    public bool ValidateAerialStart(float value)
    {
        var validation = SceneEnvironmentConstraints.ValidateAerialStart(value);
        var ticket = this.fieldDiagnostics.Begin([SceneDocumentCommandService.SceneEnvironment.AerialPerspectiveStartDepthMeters.Id], revision: 0);
        this.fieldDiagnostics.Complete(ticket, new(validation.IsValid)
        {
            ValidationCode = validation.Code,
            ValidationMessage = validation.Message,
        });
        return validation.IsValid;
    }

    /// <summary>Sets the selected shared texture asset as the exposure metering mask.</summary>
    /// <param name="textureUri">The authored texture identity, or <see langword="null"/> to clear the reference.</param>
    public void SetMeteringMask(Uri? textureUri)
        => this.AutoExposureMeteringMask = textureUri;

    /// <summary>Requests inspection of an atmosphere source node.</summary>
    /// <param name="source">The selected source option.</param>
    /// <returns>The navigation task.</returns>
    public Task InspectAtmosphereSourceAsync(SunLightOption? source)
        => source?.NodeId is { } nodeId && this.inspectSceneNode is { } inspect
            ? inspect(nodeId)
            : Task.CompletedTask;

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
            this.edits = new(commandService, commandContextProvider, "Edit Environment", this.RefreshFromScene, environment: true, this.fieldDiagnostics);
            this.edits.SetInputEnabled(this.IsInputEnabled);
            this.lightAssignments = new(commandService, commandContextProvider, "Assign Atmosphere Light",
                this.RefreshFromScene, environment: false, this.fieldDiagnostics);
            this.lightAssignments.SetInputEnabled(this.IsInputEnabled);
            this.edits.Diagnostics.Relate(SceneDocumentCommandService.SceneEnvironment.AutoExposureMinEv.Id, SceneDocumentCommandService.SceneEnvironment.AutoExposureMaxEv.Id);
            this.edits.Diagnostics.Relate(SceneDocumentCommandService.SceneEnvironment.AutoExposureLowPercentile.Id, SceneDocumentCommandService.SceneEnvironment.AutoExposureHighPercentile.Id);
        }

        this.StartMeteringMaskAssetSubscription();

        this.DetachSceneObservers();
        this.edits?.Bind(value is null ? [] : [value.Id]);
        this.lightAssignments?.Bind([]);
        this.scene = value;
        if (value is not null)
        {
            _ = this.initialAtmosphereSources.GetValue(value, CaptureAtmosphereSources);
        }
        this.OnPropertyChanged(nameof(this.SceneName));
        this.AttachSceneObservers();
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
    /// Authors the complete linear RGB background color in one edit.
    /// </summary>
    /// <param name="color">The linear RGB background color.</param>
    public void SetBackgroundColor(Vector3 color)
    {
        this.isApplyingEditorValues = true;
        try
        {
            this.BackgroundR = color.X;
            this.BackgroundG = color.Y;
            this.BackgroundB = color.Z;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyBackgroundChanged();
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, color);
    }

    /// <summary>Restores the default linear background color in one undoable scene edit.</summary>
    /// <returns>Completion of the reset transaction.</returns>
    public async Task ResetBackgroundAsync()
    {
        if (!this.IsInputEnabled || this.scene is not { } targetScene || this.edits is not { } coordinator)
        {
            return;
        }

        coordinator.End(DroidNet.Controls.NumberBoxEditCompletionKind.Commit);
        await coordinator.Pending.ConfigureAwait(true);
        if (!this.IsInputEnabled || !ReferenceEquals(this.scene, targetScene) || !ReferenceEquals(this.edits, coordinator))
        {
            return;
        }

        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, new SceneEnvironmentData().BackgroundColor);
        await coordinator.Pending.ConfigureAwait(true);
    }

    /// <summary>Authors the complete linear RGB ground albedo in one edit.</summary>
    /// <param name="color">The linear RGB ground albedo.</param>
    public void SetGroundAlbedoColor(Vector3 color)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, color);

    /// <summary>Requests focus after the field is realized.</summary>
    /// <param name="property">The stable property identifier.</param>
    internal void RequestFieldFocus(string property)
    {
        this.PendingFieldFocus = property;
        this.FieldFocusRequested?.Invoke(this, EventArgs.Empty);
    }

    /// <summary>Clears a completed focus request.</summary>
    internal void AcknowledgeFieldFocus() => this.PendingFieldFocus = null;

    /// <summary>Releases the active environment edit session.</summary>
    /// <param name="disposing">Whether managed resources should be released.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (disposing)
        {
            this.DetachSceneObservers();
            this.meteringMaskAssetSubscription?.Dispose();
            this.meteringMaskAssetSubscription = null;
            this.edits?.Dispose();
            this.lightAssignments?.Dispose();
            this.PrimaryAtmosphereSource.Dispose();
            this.SecondaryAtmosphereSource.Dispose();
        }
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

        this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Primary, null);
    }

    [RelayCommand]
    private void ClearSecondarySun()
    {
        this.isApplyingEditorValues = true;
        try
        {
            if (this.SecondarySunOptions.FirstOrDefault(static option => option.NodeId is null) is { } none)
            {
                this.SelectedSecondarySun = none;
            }
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Secondary, null);
    }

    partial void OnAtmosphereEnabledChanged(bool value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AtmosphereEnabled, value);

    partial void OnSelectedSunChanged(SunLightOption? value)
    {
        this.OnPropertyChanged(nameof(this.HasPrimaryAtmosphereSource));
        if (this.isApplyingEditorValues)
        {
            return;
        }

        if (value is null)
        {
            return;
        }

        this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Primary, value.NodeId);
    }

    partial void OnSelectedSecondarySunChanged(SunLightOption? value)
    {
        this.OnPropertyChanged(nameof(this.HasSecondaryAtmosphereSource));
        if (!this.isApplyingEditorValues && value is not null)
        {
            this.ApplyAtmosphereAssignment(AtmosphereLightSlot.Secondary, value.NodeId);
        }
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

    partial void OnAutoExposureBlackInfluenceChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureBlackInfluence, value);

    partial void OnAutoExposureTransitionDistanceEvChanged(float value)
        => this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureTransitionDistanceEv, value);

    partial void OnAutoExposureMeteringMaskChanged(Uri? value)
    {
        this.OnPropertyChanged(nameof(this.MeteringMaskDisplayName));
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureMeteringMask, value);
    }

    partial void OnAutoExposureCompensationCurveChanged(ImmutableArray<ExposureCompensationKeyData> value)
    {
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.AutoExposureCompensationCurve, value);
    }

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
    {
        this.NotifyGroundAlbedoChanged();
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(value, this.GroundAlbedoG, this.GroundAlbedoB));
    }

    partial void OnGroundAlbedoGChanged(float value)
    {
        this.NotifyGroundAlbedoChanged();
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(this.GroundAlbedoR, value, this.GroundAlbedoB));
    }

    partial void OnGroundAlbedoBChanged(float value)
    {
        this.NotifyGroundAlbedoChanged();
        this.ApplyEnvironmentProperty(SceneDocumentCommandService.SceneEnvironment.GroundAlbedo, new Vector3(this.GroundAlbedoR, this.GroundAlbedoG, value));
    }

    private void NotifyGroundAlbedoChanged()
    {
        this.OnPropertyChanged(nameof(this.GroundAlbedoColor));
    }

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

    private void OnExposureCurveKeyChanged(object? sender, EventArgs args)
    {
        if (!this.isApplyingEditorValues)
        {
            this.AutoExposureCompensationCurve = [.. this.AutoExposureCurveKeys.Select(static key => key.ToData())];
        }
    }

    private void SyncExposureCurveKeys(ImmutableArray<ExposureCompensationKeyData> curve)
    {
        var keys = curve.IsDefault ? [] : curve;
        while (this.AutoExposureCurveKeys.Count > keys.Length)
        {
            var removed = this.AutoExposureCurveKeys[^1];
            removed.Changed -= this.OnExposureCurveKeyChanged;
            this.AutoExposureCurveKeys.RemoveAt(this.AutoExposureCurveKeys.Count - 1);
        }

        for (var index = 0; index < keys.Length; index++)
        {
            if (index < this.AutoExposureCurveKeys.Count)
            {
                var existing = this.AutoExposureCurveKeys[index];
                existing.Changed -= this.OnExposureCurveKeyChanged;
                existing.MeteredEv = keys[index].MeteredEv;
                existing.CompensationEv = keys[index].CompensationEv;
                existing.Changed += this.OnExposureCurveKeyChanged;
            }
            else
            {
                var key = new ExposureCompensationKeyViewModel(keys[index]);
                key.Changed += this.OnExposureCurveKeyChanged;
                this.AutoExposureCurveKeys.Add(key);
            }
        }
    }

    internal bool ValidateExposureCurveKey(ExposureCompensationKeyViewModel key, bool editMeteredEv, float candidate)
    {
        if (!float.IsFinite(candidate))
        {
            return false;
        }

        if (!editMeteredEv)
        {
            return true;
        }

        var index = this.AutoExposureCurveKeys.IndexOf(key);
        return index >= 0
            && (index == 0 || candidate > this.AutoExposureCurveKeys[index - 1].MeteredEv)
            && (index == this.AutoExposureCurveKeys.Count - 1 || candidate < this.AutoExposureCurveKeys[index + 1].MeteredEv);
    }

    [RelayCommand]
    private void AddExposureCurveKey()
    {
        if (this.AutoExposureCurveKeys.Count >= 64)
        {
            return;
        }

        var meteredEv = this.AutoExposureCurveKeys.Count == 0 ? 0 : this.AutoExposureCurveKeys[^1].MeteredEv + 1;
        var key = new ExposureCompensationKeyViewModel(new(meteredEv, 0));
        key.Changed += this.OnExposureCurveKeyChanged;
        this.AutoExposureCurveKeys.Add(key);
        this.AutoExposureCompensationCurve = [.. this.AutoExposureCurveKeys.Select(static item => item.ToData())];
    }

    [RelayCommand]
    private void RemoveExposureCurveKey(object? parameter)
    {
        if (parameter is not ExposureCompensationKeyViewModel key || !this.AutoExposureCurveKeys.Remove(key))
        {
            return;
        }

        key.Changed -= this.OnExposureCurveKeyChanged;
        this.AutoExposureCompensationCurve = [.. this.AutoExposureCurveKeys.Select(static item => item.ToData())];
    }

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
            this.RebuildSunOptions(
                this.FindAtmosphereSource(AtmosphereLightSlot.Primary)?.Id,
                this.FindAtmosphereSource(AtmosphereLightSlot.Secondary)?.Id);
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

    private SceneNode? FindAtmosphereSource(AtmosphereLightSlot slot)
        => this.scene?.AllNodes.FirstOrDefault(node => node.Components.OfType<DirectionalLightComponent>()
            .Any(light => light.AtmosphereSlot == slot));

    private static Dictionary<Guid, PropertyEdit> CaptureAtmosphereSources(Scene scene)
    {
        var result = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in scene.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { AtmosphereSlot: not AtmosphereLightSlot.None } light)
            {
                continue;
            }

            var edit = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, light.AtmosphereSlot);
            edit.Set(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians, light.AngularSizeRadians);
            edit.Set(SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittance, light.UsePerPixelAtmosphereTransmittance);
            edit.Set(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, light.AtmosphereDiskLuminanceScaleRgb);
            var rotation = TransformConverter.QuaternionToEulerDegrees(node.Components.OfType<TransformComponent>().Single().LocalRotation);
            edit.Set(SceneDocumentCommandService.Transform.RotationX, rotation.X);
            edit.Set(SceneDocumentCommandService.Transform.RotationY, rotation.Y);
            edit.Set(SceneDocumentCommandService.Transform.RotationZ, rotation.Z);
            result.Add(node.Id, edit);
        }

        return result;
    }

    /// <summary>Restores the initial source bindings and atmosphere settings as one authored transaction.</summary>
    /// <returns>Completion of the reset transaction.</returns>
    public async Task ResetAtmosphereSourcesAsync()
    {
        if (!this.IsInputEnabled || this.scene is not { } current)
        {
            return;
        }

        var initial = this.initialAtmosphereSources.GetValue(current, CaptureAtmosphereSources);
        var edits = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in current.AllNodes)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            if (initial.TryGetValue(node.Id, out var original))
            {
                edits.Add(node.Id, original.Clone());
            }
            else if (light.AtmosphereSlot != AtmosphereLightSlot.None)
            {
                edits.Add(node.Id, PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None));
            }
        }

        if (this.lightAssignments is not { } assignments || commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var rotationMask = PropertyEdit.Single(SceneDocumentCommandService.Transform.RotationX, 0f);
        rotationMask.Set(SceneDocumentCommandService.Transform.RotationY, 0f);
        rotationMask.Set(SceneDocumentCommandService.Transform.RotationZ, 0f);
        var lightMask = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.AngularSizeRadians, 0f);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittance, false);
        lightMask.Set(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, Vector3.One);
        context.History.BeginChangeSet("Reset Atmosphere Lights");
        try
        {
            assignments.Bind(edits.Keys.ToArray());
            assignments.Submit(edits.ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(lightMask)));
            await assignments.Pending.ConfigureAwait(true);
            if (!string.IsNullOrEmpty(this.SunReferenceDiagnostic.Message))
            {
                return;
            }
            var rotations = edits.Where(pair => pair.Value.Contains(SceneDocumentCommandService.Transform.RotationX.Id))
                .ToDictionary(pair => pair.Key, pair => pair.Value.IntersectIds(rotationMask));
            if (rotations.Count > 0)
            {
                assignments.Bind(rotations.Keys.ToArray());
                assignments.Submit(rotations);
                await assignments.Pending.ConfigureAwait(true);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }
    }

    private void ApplyAtmosphereAssignment(AtmosphereLightSlot slot, Guid? selected)
    {
        if (this.isApplyingEditorValues || !this.IsInputEnabled || this.scene is null)
        {
            return;
        }

        var current = this.FindAtmosphereSource(slot);
        if (current?.Id == selected)
        {
            return;
        }

        var edits = new Dictionary<Guid, PropertyEdit>();
        if (current is not null)
        {
            edits[current.Id] = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, AtmosphereLightSlot.None);
        }

        if (selected is { } selectedId)
        {
            edits[selectedId] = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereSlot, slot);
        }

        if (edits.Count == 0)
        {
            return;
        }

        this.lightAssignments?.Bind(edits.Keys.ToArray());
        this.lightAssignments?.Submit(edits);
    }

    private void RebuildSunOptions(Guid? primaryNodeId, Guid? secondaryNodeId)
    {
        this.SunOptions.Clear();
        this.SecondarySunOptions.Clear();
        var nonePrimary = new SunLightOption(NodeId: null, "None");
        var noneSecondary = new SunLightOption(NodeId: null, "None");
        this.SunOptions.Add(nonePrimary);
        this.SecondarySunOptions.Add(noneSecondary);
        this.SelectedSun = nonePrimary;
        this.SelectedSecondarySun = noneSecondary;

        if (this.scene is null)
        {
            this.RefreshAtmosphereSourceEditors();
            return;
        }

        foreach (var node in this.scene.RootNodes.SelectMany(static root => SceneTraversal.CollectNodes(root)))
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is null)
            {
                continue;
            }

            var light = node.Components.OfType<DirectionalLightComponent>().First();
            var primaryOption = new SunLightOption(
                node.Id,
                light.AtmosphereSlot == AtmosphereLightSlot.Secondary ? $"{node.Name} · Secondary" : node.Name,
                light.AtmosphereSlot != AtmosphereLightSlot.Secondary);
            var secondaryOption = new SunLightOption(
                node.Id,
                light.AtmosphereSlot == AtmosphereLightSlot.Primary ? $"{node.Name} · Primary" : node.Name,
                light.AtmosphereSlot != AtmosphereLightSlot.Primary);
            this.SunOptions.Add(primaryOption);
            this.SecondarySunOptions.Add(secondaryOption);
            if (primaryNodeId == node.Id)
            {
                this.SelectedSun = primaryOption;
            }

            if (secondaryNodeId == node.Id)
            {
                this.SelectedSecondarySun = secondaryOption;
            }
        }

        this.RefreshAtmosphereSourceEditors();
    }

    private void RefreshAtmosphereSourceEditors()
    {
        this.PrimaryAtmosphereSource.UpdateValues(this.FindAtmosphereSource(AtmosphereLightSlot.Primary) is { } primary ? [primary] : []);
        this.SecondaryAtmosphereSource.UpdateValues(this.FindAtmosphereSource(AtmosphereLightSlot.Secondary) is { } secondary ? [secondary] : []);
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
            this.AutoExposureBlackInfluence = value.AutoExposureBlackInfluence;
            this.AutoExposureTransitionDistanceEv = value.AutoExposureTransitionDistanceEv;
            this.AutoExposureMeteringMask = value.AutoExposureMeteringMask;
            this.AutoExposureCompensationCurve = value.AutoExposureCompensationCurve;
            this.SyncExposureCurveKeys(value.AutoExposureCompensationCurve);
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
        _ = this.ValidateAerialStart(value.AerialPerspectiveStartDepthMeters);
        this.HeightFogContribution = value.HeightFogContribution;
        this.SunDiskEnabled = value.SunDiskEnabled;
    }

    private void NotifyBackgroundChanged()
    {
        this.OnPropertyChanged(nameof(this.BackgroundColor));
    }

    private void StartMeteringMaskAssetSubscription()
    {
        if (this.sharedAssetProvider is null || this.meteringMaskAssetSubscription is not null)
        {
            return;
        }

        this.meteringMaskAssetSubscription = this.sharedAssetProvider.Items
            .ObserveOn(this.observerScheduler)
            .Subscribe(this.UpdateMeteringMaskRows);
        _ = this.RefreshMeteringMaskAssetsAsync();
    }

    private async Task RefreshMeteringMaskAssetsAsync()
    {
        try
        {
            if (this.sharedAssetProvider is { } provider)
            {
                await provider.RefreshAsync(AssetBrowserFilter.Default).ConfigureAwait(false);
            }
        }
        catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidDataException or ObjectDisposedException or OperationCanceledException)
        {
            Debug.WriteLine($"[EnvironmentViewModel] Metering mask asset refresh failed: {exception}");
        }
    }

    private void UpdateMeteringMaskRows(IReadOnlyList<ContentBrowserAssetItem> assets)
    {
        var textures = assets.Where(static asset => asset.Kind == AssetKind.Texture && !asset.IsBuiltin)
            .OrderBy(static asset => asset.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToArray();
        var wanted = textures.Select(static asset => asset.IdentityUri.AbsoluteUri).ToHashSet(StringComparer.OrdinalIgnoreCase);
        foreach (var removed in this.MeteringMaskRows.Where(row => !wanted.Contains(row.Item.Uri.AbsoluteUri)).ToArray())
        {
            _ = this.MeteringMaskRows.Remove(removed);
        }

        for (var index = 0; index < textures.Length; index++)
        {
            var asset = textures[index];
            var item = new AssetPickerItem(
                asset.DisplayName,
                asset.IdentityUri,
                "Texture · " + asset.PrimaryBadge,
                asset.DisplayPath,
                AssetPickerGroup.Content,
                asset.IsSelectable,
                "\uE7C3");
            var row = this.MeteringMaskRows.FirstOrDefault(candidate => candidate.Item.Uri == asset.IdentityUri);
            if (row is null)
            {
                row = new(item);
                this.MeteringMaskRows.Insert(Math.Min(index, this.MeteringMaskRows.Count), row);
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

    partial void OnMeteringMaskSearchTextChanged(string value)
        => this.OnPropertyChanged(nameof(this.FilteredMeteringMaskRows));
}
