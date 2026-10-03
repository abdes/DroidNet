// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Inspector.Editing;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for directional light inspector editing.
/// </summary>
public sealed partial class DirectionalLightViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;

    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();

    private readonly InspectorEditSessionCoordinator? edits;
    private readonly PropertyBinding<Vector3> colorBinding = new(SceneDocumentCommandService.DirectionalLight.ColorDescriptor);
    private readonly PropertyBinding<float> intensityLuxBinding = new(SceneDocumentCommandService.DirectionalLight.IntensityLuxDescriptor);
    private readonly PropertyBinding<AtmosphereLightSlot> atmosphereSlotBinding = new(SceneDocumentCommandService.DirectionalLight.AtmosphereSlotDescriptor);
    private readonly PropertyBinding<bool> usePerPixelAtmosphereTransmittanceBinding = new(SceneDocumentCommandService.DirectionalLight.UsePerPixelAtmosphereTransmittanceDescriptor);
    private readonly PropertyBinding<bool> castsShadowsBinding = new(SceneDocumentCommandService.DirectionalLight.CastsShadowsDescriptor);
    private readonly PropertyBinding<bool> affectsWorldBinding = new(SceneDocumentCommandService.DirectionalLight.AffectsWorldDescriptor);
    private readonly PropertyBinding<float> angularSizeRadiansBinding = new(SceneDocumentCommandService.DirectionalLight.AngularSizeRadiansDescriptor);
    private readonly PropertyBinding<float> exposureCompensationBinding = new(SceneDocumentCommandService.DirectionalLight.ExposureCompensationDescriptor);
    private readonly PropertyBinding<Vector3> atmosphereDiskLuminanceScaleRgbBinding = new(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgbDescriptor);
    private readonly PropertyBinding<float> shadowBiasBinding = new(SceneDocumentCommandService.DirectionalLight.ShadowBiasDescriptor);
    private readonly PropertyBinding<float> shadowNormalBiasBinding = new(SceneDocumentCommandService.DirectionalLight.ShadowNormalBiasDescriptor);
    private readonly PropertyBinding<bool> contactShadowsBinding = new(SceneDocumentCommandService.DirectionalLight.ContactShadowsDescriptor);
    private readonly PropertyBinding<ShadowResolutionHint> shadowResolutionHintBinding = new(SceneDocumentCommandService.DirectionalLight.ShadowResolutionHintDescriptor);
    private readonly PropertyBinding<int> cascadeCountBinding = new(SceneDocumentCommandService.DirectionalLight.CascadeCountDescriptor);
    private readonly PropertyBinding<DirectionalCsmSplitMode> splitModeBinding = new(SceneDocumentCommandService.DirectionalLight.SplitModeDescriptor);
    private readonly PropertyBinding<float> maxShadowDistanceBinding = new(SceneDocumentCommandService.DirectionalLight.MaxShadowDistanceDescriptor);
    private readonly PropertyBinding<float> cascadeDistance0Binding = new(SceneDocumentCommandService.DirectionalLight.CascadeDistance0Descriptor);
    private readonly PropertyBinding<float> cascadeDistance1Binding = new(SceneDocumentCommandService.DirectionalLight.CascadeDistance1Descriptor);
    private readonly PropertyBinding<float> cascadeDistance2Binding = new(SceneDocumentCommandService.DirectionalLight.CascadeDistance2Descriptor);
    private readonly PropertyBinding<float> cascadeDistance3Binding = new(SceneDocumentCommandService.DirectionalLight.CascadeDistance3Descriptor);
    private readonly PropertyBinding<float> distributionExponentBinding = new(SceneDocumentCommandService.DirectionalLight.DistributionExponentDescriptor);
    private readonly PropertyBinding<float> transitionFractionBinding = new(SceneDocumentCommandService.DirectionalLight.TransitionFractionDescriptor);
    private readonly PropertyBinding<float> distanceFadeoutFractionBinding = new(SceneDocumentCommandService.DirectionalLight.DistanceFadeoutFractionDescriptor);
    private readonly List<IDisposable> registrations = [];
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightViewModel"/> class.
    /// </summary>
    /// <param name="commandService">Optional command service used to apply light edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public DirectionalLightViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
    {
        if (commandService is not null && commandContextProvider is not null)
        {
            this.edits = new(commandService, commandContextProvider, "Edit Directional Light", this.RefreshValues);
        }

        this.colorBinding.ValueRequested += this.OnLightVectorValueRequested;
        this.Register(this.intensityLuxBinding, nameof(this.IntensityLux));
        this.Register(this.angularSizeRadiansBinding, nameof(this.AngularSizeRadians));
        this.Register(this.exposureCompensationBinding, nameof(this.ExposureCompensation));
        this.Register(this.shadowBiasBinding, nameof(this.ShadowBias));
        this.Register(this.shadowNormalBiasBinding, nameof(this.ShadowNormalBias));
        this.Register(this.maxShadowDistanceBinding, nameof(this.MaxShadowDistance));
        this.Register(this.cascadeDistance0Binding, nameof(this.CascadeDistance1));
        this.Register(this.cascadeDistance1Binding, nameof(this.CascadeDistance2));
        this.Register(this.cascadeDistance2Binding, nameof(this.CascadeDistance3));
        this.Register(this.cascadeDistance3Binding, nameof(this.CascadeDistance4));
        this.Register(this.distributionExponentBinding, nameof(this.DistributionExponent));
        this.Register(this.transitionFractionBinding, nameof(this.TransitionFraction));
        this.Register(this.distanceFadeoutFractionBinding, nameof(this.DistanceFadeoutFraction));
        this.Register(this.atmosphereSlotBinding, nameof(this.AtmosphereSlot));
        this.Register(this.usePerPixelAtmosphereTransmittanceBinding, nameof(this.UsePerPixelAtmosphereTransmittance));
        this.Register(this.castsShadowsBinding, nameof(this.CastsShadows));
        this.Register(this.affectsWorldBinding, nameof(this.AffectsWorld));
        this.Register(this.contactShadowsBinding, nameof(this.ContactShadows));
        this.Register(this.cascadeCountBinding, nameof(this.CascadeCount));
        this.atmosphereDiskLuminanceScaleRgbBinding.ValueRequested += this.OnVector3ValueRequested;
        this.Register(this.shadowResolutionHintBinding, nameof(this.ShadowResolutionHint));
        this.Register(this.splitModeBinding, nameof(this.SplitMode));
    }

    [ObservableProperty]
    public partial float ColorR { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorG { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorB { get; set; } = 1f;

    /// <summary>Gets or sets the authored illuminance through its sole binding state.</summary>
    public float IntensityLux { get => this.intensityLuxBinding.Value; set => this.RequestLightValue(this.intensityLuxBinding, value); }

    /// <summary>Gets or sets the source role through its sole binding state.</summary>
    public AtmosphereLightSlot AtmosphereSlot { get => this.atmosphereSlotBinding.Value; set => this.RequestLightValue(this.atmosphereSlotBinding, value); }

    /// <summary>Gets or sets a value indicating whether per-pixel transmittance is authored.</summary>
    public bool UsePerPixelAtmosphereTransmittance { get => this.usePerPixelAtmosphereTransmittanceBinding.Value; set => this.RequestLightValue(this.usePerPixelAtmosphereTransmittanceBinding, value); }

    /// <summary>Gets or sets a value indicating whether shadows are authored.</summary>
    public bool CastsShadows { get => this.castsShadowsBinding.Value; set => this.RequestLightValue(this.castsShadowsBinding, value); }

    /// <summary>Gets or sets a value indicating whether the light affects the scene.</summary>
    public bool AffectsWorld { get => this.affectsWorldBinding.Value; set => this.RequestLightValue(this.affectsWorldBinding, value); }

    /// <summary>Gets or sets the authored diameter in radians; the display adapter converts once.</summary>
    public float AngularSizeRadians { get => this.angularSizeRadiansBinding.Value; set => this.RequestLightValue(this.angularSizeRadiansBinding, value); }

    /// <summary>Gets or sets the source angular diameter in display degrees.</summary>
    public float AngularDiameterDegrees
    {
        get => this.AngularSizeRadians * RadToDeg;
        set => this.AngularSizeRadians = value * DegToRad;
    }

    /// <summary>Gets or sets authored exposure compensation.</summary>
    public float ExposureCompensation { get => this.exposureCompensationBinding.Value; set => this.RequestLightValue(this.exposureCompensationBinding, value); }

    [ObservableProperty]
    public partial Vector3 AtmosphereDiskLuminanceScaleRgb { get; set; } = Vector3.One;

    [ObservableProperty]
    public partial float DiskScaleR { get; set; } = 1f;

    [ObservableProperty]
    public partial float DiskScaleG { get; set; } = 1f;

    [ObservableProperty]
    public partial float DiskScaleB { get; set; } = 1f;

    /// <summary>Gets or sets depth bias.</summary>
    public float ShadowBias { get => this.shadowBiasBinding.Value; set => this.RequestLightValue(this.shadowBiasBinding, value); }

    /// <summary>Gets or sets normal bias.</summary>
    public float ShadowNormalBias { get => this.shadowNormalBiasBinding.Value; set => this.RequestLightValue(this.shadowNormalBiasBinding, value); }

    /// <summary>Gets or sets a value indicating whether contact shadows are authored.</summary>
    public bool ContactShadows { get => this.contactShadowsBinding.Value; set => this.RequestLightValue(this.contactShadowsBinding, value); }

    /// <summary>Gets or sets the resolution hint.</summary>
    public ShadowResolutionHint ShadowResolutionHint { get => this.shadowResolutionHintBinding.Value; set => this.RequestLightValue(this.shadowResolutionHintBinding, value); }

    /// <summary>Gets or sets the numeric display adapter for the authored integer count.</summary>
    public float CascadeCount { get => this.cascadeCountBinding.Value; set => this.RequestLightValue(this.cascadeCountBinding, Math.Clamp((int)MathF.Round(value), 1, 4)); }

    /// <summary>Gets or sets the cascade split mode.</summary>
    public DirectionalCsmSplitMode SplitMode { get => this.splitModeBinding.Value; set => this.RequestLightValue(this.splitModeBinding, value); }

    /// <summary>Gets or sets maximum shadow distance.</summary>
    public float MaxShadowDistance { get => this.maxShadowDistanceBinding.Value; set => this.RequestLightValue(this.maxShadowDistanceBinding, value); }

    /// <summary>Gets or sets the first cascade distance.</summary>
    public float CascadeDistance1 { get => this.cascadeDistance0Binding.Value; set => this.RequestLightValue(this.cascadeDistance0Binding, value); }

    /// <summary>Gets or sets the second cascade distance.</summary>
    public float CascadeDistance2 { get => this.cascadeDistance1Binding.Value; set => this.RequestLightValue(this.cascadeDistance1Binding, value); }

    /// <summary>Gets or sets the third cascade distance.</summary>
    public float CascadeDistance3 { get => this.cascadeDistance2Binding.Value; set => this.RequestLightValue(this.cascadeDistance2Binding, value); }

    /// <summary>Gets or sets the fourth cascade distance.</summary>
    public float CascadeDistance4 { get => this.cascadeDistance3Binding.Value; set => this.RequestLightValue(this.cascadeDistance3Binding, value); }

    /// <summary>Gets or sets distribution exponent.</summary>
    public float DistributionExponent { get => this.distributionExponentBinding.Value; set => this.RequestLightValue(this.distributionExponentBinding, value); }

    /// <summary>Gets or sets transition fraction.</summary>
    public float TransitionFraction { get => this.transitionFractionBinding.Value; set => this.RequestLightValue(this.transitionFractionBinding, value); }

    /// <summary>Gets or sets fade-out fraction.</summary>
    public float DistanceFadeoutFraction { get => this.distanceFadeoutFractionBinding.Value; set => this.RequestLightValue(this.distanceFadeoutFractionBinding, value); }

    [ObservableProperty]
    public partial float SunAzimuth { get; set; }

    [ObservableProperty]
    public partial float SunElevation { get; set; } = 45f;

    /// <summary>
    /// Gets explicit atmosphere assignment options.
    /// </summary>
    public IReadOnlyList<AtmosphereLightSlot> AtmosphereSlotOptions { get; } = Enum.GetValues<AtmosphereLightSlot>();

    /// <summary>
    /// Gets shadow resolution options for the editor.
    /// </summary>
    public IReadOnlyList<ShadowResolutionHint> ShadowResolutionOptions { get; } = Enum.GetValues<ShadowResolutionHint>();

    /// <summary>
    /// Gets CSM split mode options for the editor.
    /// </summary>
    public IReadOnlyList<DirectionalCsmSplitMode> SplitModeOptions { get; } = Enum.GetValues<DirectionalCsmSplitMode>();

    /// <summary>
    /// Gets the authored linear RGB light color.
    /// </summary>
    public Vector3 ColorValue => new(this.ColorR, this.ColorG, this.ColorB);

    /// <summary>Gets current diagnostics for ColorR.</summary>
    public InspectorFieldDiagnostic ColorRDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ColorG.</summary>
    public InspectorFieldDiagnostic ColorGDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ColorB.</summary>
    public InspectorFieldDiagnostic ColorBDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SunAzimuth.</summary>
    public InspectorFieldDiagnostic SunAzimuthDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.Transform.RotationX.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SunElevation.</summary>
    public InspectorFieldDiagnostic SunElevationDiagnostic => this.edits?.Diagnostics.Get(SceneDocumentCommandService.Transform.RotationX.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for IntensityLux.</summary>
    public InspectorFieldDiagnostic IntensityLuxDiagnostic => this.edits?.Diagnostics.Get(this.intensityLuxBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AtmosphereSlot.</summary>
    public InspectorFieldDiagnostic AtmosphereSlotDiagnostic => this.edits?.Diagnostics.Get(this.atmosphereSlotBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for UsePerPixelAtmosphereTransmittance.</summary>
    public InspectorFieldDiagnostic UsePerPixelAtmosphereTransmittanceDiagnostic => this.edits?.Diagnostics.Get(this.usePerPixelAtmosphereTransmittanceBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CastsShadows.</summary>
    public InspectorFieldDiagnostic CastsShadowsDiagnostic => this.edits?.Diagnostics.Get(this.castsShadowsBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AffectsWorld.</summary>
    public InspectorFieldDiagnostic AffectsWorldDiagnostic => this.edits?.Diagnostics.Get(this.affectsWorldBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AngularSizeRadians.</summary>
    public InspectorFieldDiagnostic AngularSizeRadiansDiagnostic => this.edits?.Diagnostics.Get(this.angularSizeRadiansBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ExposureCompensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.edits?.Diagnostics.Get(this.exposureCompensationBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AtmosphereDiskLuminanceScaleRgb.</summary>
    public InspectorFieldDiagnostic AtmosphereDiskLuminanceScaleRgbDiagnostic => this.edits?.Diagnostics.Get(this.atmosphereDiskLuminanceScaleRgbBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ShadowBias.</summary>
    public InspectorFieldDiagnostic ShadowBiasDiagnostic => this.edits?.Diagnostics.Get(this.shadowBiasBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ShadowNormalBias.</summary>
    public InspectorFieldDiagnostic ShadowNormalBiasDiagnostic => this.edits?.Diagnostics.Get(this.shadowNormalBiasBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ContactShadows.</summary>
    public InspectorFieldDiagnostic ContactShadowsDiagnostic => this.edits?.Diagnostics.Get(this.contactShadowsBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ShadowResolutionHint.</summary>
    public InspectorFieldDiagnostic ShadowResolutionHintDiagnostic => this.edits?.Diagnostics.Get(this.shadowResolutionHintBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CascadeCount.</summary>
    public InspectorFieldDiagnostic CascadeCountDiagnostic => this.edits?.Diagnostics.Get(this.cascadeCountBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SplitMode.</summary>
    public InspectorFieldDiagnostic SplitModeDiagnostic => this.edits?.Diagnostics.Get(this.splitModeBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for MaxShadowDistance.</summary>
    public InspectorFieldDiagnostic MaxShadowDistanceDiagnostic => this.edits?.Diagnostics.Get(this.maxShadowDistanceBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CascadeDistance1.</summary>
    public InspectorFieldDiagnostic CascadeDistance1Diagnostic => this.edits?.Diagnostics.Get(this.cascadeDistance0Binding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CascadeDistance2.</summary>
    public InspectorFieldDiagnostic CascadeDistance2Diagnostic => this.edits?.Diagnostics.Get(this.cascadeDistance1Binding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CascadeDistance3.</summary>
    public InspectorFieldDiagnostic CascadeDistance3Diagnostic => this.edits?.Diagnostics.Get(this.cascadeDistance2Binding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CascadeDistance4.</summary>
    public InspectorFieldDiagnostic CascadeDistance4Diagnostic => this.edits?.Diagnostics.Get(this.cascadeDistance3Binding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for DistributionExponent.</summary>
    public InspectorFieldDiagnostic DistributionExponentDiagnostic => this.edits?.Diagnostics.Get(this.distributionExponentBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for TransitionFraction.</summary>
    public InspectorFieldDiagnostic TransitionFractionDiagnostic => this.edits?.Diagnostics.Get(this.transitionFractionBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for DistanceFadeoutFraction.</summary>
    public InspectorFieldDiagnostic DistanceFadeoutFractionDiagnostic => this.edits?.Diagnostics.Get(this.distanceFadeoutFractionBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <inheritdoc/>
    public Guid EditScopeId => this.edits?.ScopeId ?? Guid.Empty;

    /// <inheritdoc />
    public override string Header => "Directional Light";

    /// <inheritdoc />
    public override string Description => "Authored sun, shadow, and environment light data.";

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

    /// <inheritdoc />
    public void Dispose()
    {
        if (this.disposed)
        {
            return;
        }

        this.colorBinding.ValueRequested -= this.OnLightVectorValueRequested;
        this.atmosphereDiskLuminanceScaleRgbBinding.ValueRequested -= this.OnVector3ValueRequested;
        foreach (var registration in this.registrations)
        {
            registration.Dispose();
        }

        this.edits?.Dispose();
        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.edits?.Bind(items.Where(node => node.Components.Any(component => component is DirectionalLightComponent)).Select(node => node.Id).ToArray());
        this.selectedItems = items;
        var targets = items
            .Select(static node => new { Node = node, Light = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() })
            .Where(static target => target.Light is not null)
            .ToList();
        var nodeIds = targets.ConvertAll(static target => target.Node.Id);
        var targetsByNode = targets.ToDictionary(static target => target.Node.Id, static target => (object?)target.Light);

        this.isApplyingEditorValues = true;
        try
        {
            this.colorBinding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
            var color = this.colorBinding.HasValue ? this.colorBinding.Value : Vector3.One;
            this.SetDisplayColor(color);
            this.UpdateBinding(this.intensityLuxBinding, nodeIds, targetsByNode, value => this.IntensityLux = value);
            this.UpdateBinding(this.atmosphereSlotBinding, nodeIds, targetsByNode, value => this.AtmosphereSlot = value);
            this.UpdateBinding(this.usePerPixelAtmosphereTransmittanceBinding, nodeIds, targetsByNode, value => this.UsePerPixelAtmosphereTransmittance = value);
            this.UpdateBinding(this.castsShadowsBinding, nodeIds, targetsByNode, value => this.CastsShadows = value);
            this.UpdateBinding(this.affectsWorldBinding, nodeIds, targetsByNode, value => this.AffectsWorld = value);
            this.UpdateBinding(this.angularSizeRadiansBinding, nodeIds, targetsByNode, value => this.AngularSizeRadians = value);
            this.UpdateBinding(this.exposureCompensationBinding, nodeIds, targetsByNode, value => this.ExposureCompensation = value);
            this.UpdateBinding(this.atmosphereDiskLuminanceScaleRgbBinding, nodeIds, targetsByNode, value =>
            {
                this.AtmosphereDiskLuminanceScaleRgb = value;
                this.DiskScaleR = value.X;
                this.DiskScaleG = value.Y;
                this.DiskScaleB = value.Z;
            });
            this.UpdateBinding(this.shadowBiasBinding, nodeIds, targetsByNode, value => this.ShadowBias = value);
            this.UpdateBinding(this.shadowNormalBiasBinding, nodeIds, targetsByNode, value => this.ShadowNormalBias = value);
            this.UpdateBinding(this.contactShadowsBinding, nodeIds, targetsByNode, value => this.ContactShadows = value);
            this.UpdateBinding(this.shadowResolutionHintBinding, nodeIds, targetsByNode, value => this.ShadowResolutionHint = value);
            this.UpdateBinding(this.cascadeCountBinding, nodeIds, targetsByNode, value => this.CascadeCount = value);
            this.UpdateBinding(this.splitModeBinding, nodeIds, targetsByNode, value => this.SplitMode = value);
            this.UpdateBinding(this.maxShadowDistanceBinding, nodeIds, targetsByNode, value => this.MaxShadowDistance = value);
            this.UpdateBinding(this.cascadeDistance0Binding, nodeIds, targetsByNode, value => this.CascadeDistance1 = value);
            this.UpdateBinding(this.cascadeDistance1Binding, nodeIds, targetsByNode, value => this.CascadeDistance2 = value);
            this.UpdateBinding(this.cascadeDistance2Binding, nodeIds, targetsByNode, value => this.CascadeDistance3 = value);
            this.UpdateBinding(this.cascadeDistance3Binding, nodeIds, targetsByNode, value => this.CascadeDistance4 = value);
            this.UpdateBinding(this.distributionExponentBinding, nodeIds, targetsByNode, value => this.DistributionExponent = value);
            this.UpdateBinding(this.transitionFractionBinding, nodeIds, targetsByNode, value => this.TransitionFraction = value);
            this.UpdateBinding(this.distanceFadeoutFractionBinding, nodeIds, targetsByNode, value => this.DistanceFadeoutFraction = value);
            this.UpdateSunDirectionValues(targets.ConvertAll(static target => target.Node));
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyColorChanged();
    }

    /// <summary>
    /// Authors the complete linear RGB light color in one edit.
    /// </summary>
    /// <param name="color">The linear RGB light color.</param>
    public void SetColor(Vector3 color)
    {
        var r = color.X;
        var g = color.Y;
        var b = color.Z;
        if (NearlyEqual(this.ColorR, r) && NearlyEqual(this.ColorG, g) && NearlyEqual(this.ColorB, b))
        {
            return;
        }

        this.isApplyingEditorValues = true;
        try
        {
            this.SetDisplayColor(color);
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyColorChanged();
        this.colorBinding.Value = new Vector3(r, g, b);
    }

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }

    private void UpdateBinding<T>(
        PropertyBinding<T> binding,
        IReadOnlyList<Guid> nodeIds,
        Dictionary<Guid, object?> targetsByNode,
        Action<T> setValue)
    {
        var previous = binding.HasValue ? (object?)binding.Value : null;
        var wasMixed = binding.IsMixed;
        binding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
        if (!Equals(previous, binding.HasValue ? binding.Value : null) || wasMixed != binding.IsMixed)
        {
            this.edits?.ModelChanged(binding.Id.Id);
        }

        setValue(binding.HasValue ? binding.Value : default!);
    }

    partial void OnColorRChanged(float value) => this.ApplyColorAxisEdit(0, value);

    partial void OnColorGChanged(float value) => this.ApplyColorAxisEdit(1, value);

    partial void OnColorBChanged(float value) => this.ApplyColorAxisEdit(2, value);

    partial void OnAtmosphereDiskLuminanceScaleRgbChanged(Vector3 value) => this.RequestLightValue(this.atmosphereDiskLuminanceScaleRgbBinding, value);

    partial void OnSunAzimuthChanged(float value) => this.ApplySunDirectionEdit(value, this.SunElevation);

    partial void OnSunElevationChanged(float value) => this.ApplySunDirectionEdit(this.SunAzimuth, value);

    private void ApplyColorAxisEdit(int axis, float value)
    {
        this.NotifyColorChanged();
        if (this.isApplyingEditorValues || this.selectedItems is null)
        {
            return;
        }

        var perTarget = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            var color = light.Color;
            color[axis] = value;
            perTarget[node.Id] = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.Color, color);
        }

        this.edits?.Submit(perTarget);
    }

    private void RequestLightValue<T>(PropertyBinding<T> binding, T value)
    {
        if (this.isApplyingEditorValues)
        {
            return;
        }

        if (!this.IsInputEnabled)
        {
            this.RefreshValues();
            return;
        }

        if (!binding.IsMixed && Equals(binding.Value, value))
        {
            return;
        }

        binding.Value = value;
    }

    private void OnLightVectorValueRequested(object? sender, PropertyBindingChangedEventArgs<Vector3> args)
    {
        if (sender is PropertyBinding<Vector3> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    partial void OnDiskScaleRChanged(float value) => this.ApplyDiskScaleAxisEdit(0, value);

    partial void OnDiskScaleGChanged(float value) => this.ApplyDiskScaleAxisEdit(1, value);

    partial void OnDiskScaleBChanged(float value) => this.ApplyDiskScaleAxisEdit(2, value);

    private void ApplyDiskScaleAxisEdit(int axis, float value)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null)
        {
            return;
        }

        var perTarget = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            var scale = light.AtmosphereDiskLuminanceScaleRgb;
            scale[axis] = value;
            perTarget[node.Id] = PropertyEdit.Single(SceneDocumentCommandService.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, scale);
        }

        this.edits?.Submit(perTarget);
    }

    private void OnVector3ValueRequested(object? sender, PropertyBindingChangedEventArgs<Vector3> args)
    {
        if (sender is PropertyBinding<Vector3> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void Register<T>(PropertyBinding<T> binding, string property)
        => this.registrations.Add(new InspectorBindingRegistration<T>(
            binding, this.edits, () => this.IsInputEnabled && !this.disposed, this.RefreshValues, () =>
            {
                this.OnPropertyChanged(property);
                if (string.Equals(property, nameof(this.AngularSizeRadians), StringComparison.Ordinal))
                {
                    this.OnPropertyChanged(nameof(this.AngularDiameterDegrees));
                }
            }));

    private void ApplyLightEdit(PropertyEdit edit)
    {
        if (!this.isApplyingEditorValues)
        {
            this.edits?.Submit(edit);
        }
    }

    private void ApplySunDirectionEdit(float azimuth, float elevation)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null || !float.IsFinite(azimuth) || !float.IsFinite(elevation))
        {
            return;
        }

        var perTarget = new Dictionary<Guid, PropertyEdit>();
        foreach (var node in this.selectedItems.Where(node => node.Components.Any(component => component is DirectionalLightComponent)))
        {
            var angles = TransformConverter.QuaternionToEulerDegrees(DirectionalLightOrientation.LocalRotation(node, azimuth, elevation));
            var edit = PropertyEdit.Single(SceneDocumentCommandService.Transform.RotationX, angles.X);
            edit.Set(SceneDocumentCommandService.Transform.RotationY, angles.Y);
            edit.Set(SceneDocumentCommandService.Transform.RotationZ, angles.Z);
            perTarget[node.Id] = edit;
        }

        this.edits?.Submit(perTarget);
    }

    private void UpdateSunDirectionValues(IReadOnlyList<SceneNode> nodes)
    {
        if (nodes.FirstOrDefault(static node => node.Components.OfType<TransformComponent>().Any()) is not { } node)
        {
            this.SunAzimuth = 0f;
            this.SunElevation = 0f;
            return;
        }

        var (azimuth, elevation) = DirectionalLightOrientation.DisplayAngles(node);
        this.SunAzimuth = azimuth;
        this.SunElevation = elevation;
    }

    private void SetDisplayColor(Vector3 color)
    {
        this.ColorR = color.X;
        this.ColorG = color.Y;
        this.ColorB = color.Z;
    }

    private void NotifyColorChanged()
    {
        this.OnPropertyChanged(nameof(this.ColorValue));
    }
}
