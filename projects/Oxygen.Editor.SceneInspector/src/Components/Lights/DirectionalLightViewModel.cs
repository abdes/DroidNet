// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using Oxygen.Editor.SceneInspector.Editing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.SceneInspector.Components.Lights;

/// <summary>
/// ViewModel for directional light inspector editing.
/// </summary>
public sealed partial class DirectionalLightViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;

    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();
    private readonly InspectorFieldOverride unboundOverride = new(static () => { });

    private readonly InspectorEditSessionCoordinator? edits;
    private readonly PropertyBinding<Vector3> colorBinding = new(SceneProperties.DirectionalLight.ColorDescriptor);
    private readonly NumberBinding<float> intensityLuxBinding = new(SceneProperties.DirectionalLight.IntensityLuxDescriptor);
    private readonly PropertyBinding<AtmosphereLightSlot> atmosphereSlotBinding = new(SceneProperties.DirectionalLight.AtmosphereSlotDescriptor);
    private readonly PropertyBinding<bool> usePerPixelAtmosphereTransmittanceBinding = new(SceneProperties.DirectionalLight.UsePerPixelAtmosphereTransmittanceDescriptor);
    private readonly PropertyBinding<bool> castsShadowsBinding = new(SceneProperties.DirectionalLight.CastsShadowsDescriptor);
    private readonly PropertyBinding<bool> affectsWorldBinding = new(SceneProperties.DirectionalLight.AffectsWorldDescriptor);
    private readonly NumberBinding<float> angularSizeRadiansBinding = new(SceneProperties.DirectionalLight.AngularSizeRadiansDescriptor);
    private readonly NumberBinding<float> exposureCompensationBinding = new(SceneProperties.DirectionalLight.ExposureCompensationDescriptor);
    private readonly PropertyBinding<Vector3> atmosphereDiskLuminanceScaleRgbBinding = new(SceneProperties.DirectionalLight.AtmosphereDiskLuminanceScaleRgbDescriptor);
    private readonly NumberBinding<float> shadowBiasBinding = new(SceneProperties.DirectionalLight.ShadowBiasDescriptor);
    private readonly NumberBinding<float> shadowNormalBiasBinding = new(SceneProperties.DirectionalLight.ShadowNormalBiasDescriptor);
    private readonly PropertyBinding<bool> contactShadowsBinding = new(SceneProperties.DirectionalLight.ContactShadowsDescriptor);
    private readonly PropertyBinding<ShadowResolutionHint> shadowResolutionHintBinding = new(SceneProperties.DirectionalLight.ShadowResolutionHintDescriptor);
    private readonly PropertyBinding<int> cascadeCountBinding = new(SceneProperties.DirectionalLight.CascadeCountDescriptor);
    private readonly PropertyBinding<DirectionalCsmSplitMode> splitModeBinding = new(SceneProperties.DirectionalLight.SplitModeDescriptor);
    private readonly NumberBinding<float> maxShadowDistanceBinding = new(SceneProperties.DirectionalLight.MaxShadowDistanceDescriptor);
    private readonly NumberBinding<float> cascadeDistance0Binding = new(SceneProperties.DirectionalLight.CascadeDistance0Descriptor);
    private readonly NumberBinding<float> cascadeDistance1Binding = new(SceneProperties.DirectionalLight.CascadeDistance1Descriptor);
    private readonly NumberBinding<float> cascadeDistance2Binding = new(SceneProperties.DirectionalLight.CascadeDistance2Descriptor);
    private readonly NumberBinding<float> cascadeDistance3Binding = new(SceneProperties.DirectionalLight.CascadeDistance3Descriptor);
    private readonly NumberBinding<float> distributionExponentBinding = new(SceneProperties.DirectionalLight.DistributionExponentDescriptor);
    private readonly NumberBinding<float> transitionFractionBinding = new(SceneProperties.DirectionalLight.TransitionFractionDescriptor);
    private readonly NumberBinding<float> distanceFadeoutFractionBinding = new(SceneProperties.DirectionalLight.DistanceFadeoutFractionDescriptor);
    private readonly List<IDisposable> registrations = [];
    private ICollection<SceneNode>? selectedItems;
    private bool isApplyingEditorValues;
    private bool disposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="DirectionalLightViewModel"/> class.
    /// </summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public DirectionalLightViewModel(
        Func<SceneAuthoring?>? authoringProvider = null)
    {
        if (authoringProvider is not null)
        {
            this.edits = new(authoringProvider, "Edit Directional Light", this.RefreshValues, sourceApply: () => this.SourceApply);
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
    public partial double ColorR { get; set; } = 1f;

    [ObservableProperty]
    public partial double ColorG { get; set; } = 1f;

    [ObservableProperty]
    public partial double ColorB { get; set; } = 1f;

    /// <summary>Gets or sets the authored illuminance through its sole binding state.</summary>
    public double IntensityLux { get => this.intensityLuxBinding.Value; set => this.RequestLightNumber(this.intensityLuxBinding, value); }

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
    public double AngularDiameterDegrees
    {
        get => this.AngularSizeRadians * RadToDeg;
        set => this.AngularSizeRadians = (float)(value * DegToRad);
    }

    /// <summary>Gets or sets authored exposure compensation.</summary>
    public double ExposureCompensation { get => this.exposureCompensationBinding.Value; set => this.RequestLightNumber(this.exposureCompensationBinding, value); }

    [ObservableProperty]
    public partial Vector3 AtmosphereDiskLuminanceScaleRgb { get; set; } = Vector3.One;

    [ObservableProperty]
    public partial double DiskScaleR { get; set; } = 1f;

    [ObservableProperty]
    public partial double DiskScaleG { get; set; } = 1f;

    [ObservableProperty]
    public partial double DiskScaleB { get; set; } = 1f;

    /// <summary>Gets or sets depth bias.</summary>
    public double ShadowBias { get => this.shadowBiasBinding.Value; set => this.RequestLightNumber(this.shadowBiasBinding, value); }

    /// <summary>Gets or sets normal bias.</summary>
    public double ShadowNormalBias { get => this.shadowNormalBiasBinding.Value; set => this.RequestLightNumber(this.shadowNormalBiasBinding, value); }

    /// <summary>Gets or sets a value indicating whether contact shadows are authored.</summary>
    public bool ContactShadows { get => this.contactShadowsBinding.Value; set => this.RequestLightValue(this.contactShadowsBinding, value); }

    /// <summary>Gets or sets the resolution hint.</summary>
    public ShadowResolutionHint ShadowResolutionHint { get => this.shadowResolutionHintBinding.Value; set => this.RequestLightValue(this.shadowResolutionHintBinding, value); }

    /// <summary>
    /// Gets or sets the numeric display adapter for the authored integer count; a count outside the schema range is
    /// rejected, not clamped, and a number with no integer value shows the authored count again.
    /// </summary>
    public double CascadeCount
    {
        get => this.cascadeCountBinding.Value;
        set
        {
            if (InspectorValues.TryToInt32(value, out var count))
            {
                this.RequestLightValue(this.cascadeCountBinding, count);
            }
            else
            {
                this.RefreshValues();
            }
        }
    }

    /// <summary>Gets or sets the cascade split mode.</summary>
    public DirectionalCsmSplitMode SplitMode { get => this.splitModeBinding.Value; set => this.RequestLightValue(this.splitModeBinding, value); }

    /// <summary>Gets or sets maximum shadow distance.</summary>
    public double MaxShadowDistance { get => this.maxShadowDistanceBinding.Value; set => this.RequestLightNumber(this.maxShadowDistanceBinding, value); }

    /// <summary>Gets or sets the first cascade distance.</summary>
    public double CascadeDistance1 { get => this.cascadeDistance0Binding.Value; set => this.RequestLightNumber(this.cascadeDistance0Binding, value); }

    /// <summary>Gets or sets the second cascade distance.</summary>
    public double CascadeDistance2 { get => this.cascadeDistance1Binding.Value; set => this.RequestLightNumber(this.cascadeDistance1Binding, value); }

    /// <summary>Gets or sets the third cascade distance.</summary>
    public double CascadeDistance3 { get => this.cascadeDistance2Binding.Value; set => this.RequestLightNumber(this.cascadeDistance2Binding, value); }

    /// <summary>Gets or sets the fourth cascade distance.</summary>
    public double CascadeDistance4 { get => this.cascadeDistance3Binding.Value; set => this.RequestLightNumber(this.cascadeDistance3Binding, value); }

    /// <summary>Gets or sets distribution exponent.</summary>
    public double DistributionExponent { get => this.distributionExponentBinding.Value; set => this.RequestLightNumber(this.distributionExponentBinding, value); }

    /// <summary>Gets or sets transition fraction.</summary>
    public double TransitionFraction { get => this.transitionFractionBinding.Value; set => this.RequestLightNumber(this.transitionFractionBinding, value); }

    /// <summary>Gets or sets fade-out fraction.</summary>
    public double DistanceFadeoutFraction { get => this.distanceFadeoutFractionBinding.Value; set => this.RequestLightNumber(this.distanceFadeoutFractionBinding, value); }

    [ObservableProperty]
    public partial double SunAzimuth { get; set; }

    [ObservableProperty]
    public partial double SunElevation { get; set; } = 45f;

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
    public Vector3 ColorValue => InspectorValues.ToVector3(this.ColorR, this.ColorG, this.ColorB);

    /// <summary>Gets current diagnostics for ColorR.</summary>
    public InspectorFieldDiagnostic ColorRDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ColorG.</summary>
    public InspectorFieldDiagnostic ColorGDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ColorB.</summary>
    public InspectorFieldDiagnostic ColorBDiagnostic => this.edits?.Diagnostics.Get(this.colorBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SunAzimuth.</summary>
    public InspectorFieldDiagnostic SunAzimuthDiagnostic => this.edits?.Diagnostics.Get(SceneSchema.Transform.Rotation.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for SunElevation.</summary>
    public InspectorFieldDiagnostic SunElevationDiagnostic => this.edits?.Diagnostics.Get(SceneSchema.Transform.Rotation.Id) ?? this.unboundDiagnostic;

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

    /// <summary>Gets the color override bar.</summary>
    public InspectorFieldOverride ColorOverride => this.Override(this.colorBinding.Id.Id);

    /// <summary>Gets the illuminance override bar.</summary>
    public InspectorFieldOverride IntensityLuxOverride => this.Override(this.intensityLuxBinding.Id.Id);

    /// <summary>Gets the angular diameter override bar.</summary>
    public InspectorFieldOverride AngularSizeRadiansOverride => this.Override(this.angularSizeRadiansBinding.Id.Id);

    /// <summary>Gets the sun direction override bar.</summary>
    public InspectorFieldOverride SunDirectionOverride => this.Override(SceneSchema.Transform.Rotation.Id);

    /// <summary>Gets the atmosphere slot override bar.</summary>
    public InspectorFieldOverride AtmosphereSlotOverride => this.Override(this.atmosphereSlotBinding.Id.Id);

    /// <summary>Gets the per-pixel transmittance override bar.</summary>
    public InspectorFieldOverride UsePerPixelAtmosphereTransmittanceOverride => this.Override(this.usePerPixelAtmosphereTransmittanceBinding.Id.Id);

    /// <summary>Gets the disk luminance scale override bar.</summary>
    public InspectorFieldOverride AtmosphereDiskLuminanceScaleRgbOverride => this.Override(this.atmosphereDiskLuminanceScaleRgbBinding.Id.Id);

    /// <summary>Gets the affects world override bar.</summary>
    public InspectorFieldOverride AffectsWorldOverride => this.Override(this.affectsWorldBinding.Id.Id);

    /// <summary>Gets the exposure compensation override bar.</summary>
    public InspectorFieldOverride ExposureCompensationOverride => this.Override(this.exposureCompensationBinding.Id.Id);

    /// <summary>Gets the cast shadows override bar.</summary>
    public InspectorFieldOverride CastsShadowsOverride => this.Override(this.castsShadowsBinding.Id.Id);

    /// <summary>Gets the contact shadows override bar.</summary>
    public InspectorFieldOverride ContactShadowsOverride => this.Override(this.contactShadowsBinding.Id.Id);

    /// <summary>Gets the shadow resolution override bar.</summary>
    public InspectorFieldOverride ShadowResolutionHintOverride => this.Override(this.shadowResolutionHintBinding.Id.Id);

    /// <summary>Gets the shadow depth bias override bar.</summary>
    public InspectorFieldOverride ShadowBiasOverride => this.Override(this.shadowBiasBinding.Id.Id);

    /// <summary>Gets the shadow normal bias override bar.</summary>
    public InspectorFieldOverride ShadowNormalBiasOverride => this.Override(this.shadowNormalBiasBinding.Id.Id);

    /// <summary>Gets the split mode override bar.</summary>
    public InspectorFieldOverride SplitModeOverride => this.Override(this.splitModeBinding.Id.Id);

    /// <summary>Gets the cascade count override bar.</summary>
    public InspectorFieldOverride CascadeCountOverride => this.Override(this.cascadeCountBinding.Id.Id);

    /// <summary>Gets the maximum shadow distance override bar.</summary>
    public InspectorFieldOverride MaxShadowDistanceOverride => this.Override(this.maxShadowDistanceBinding.Id.Id);

    /// <summary>Gets the distribution exponent override bar.</summary>
    public InspectorFieldOverride DistributionExponentOverride => this.Override(this.distributionExponentBinding.Id.Id);

    /// <summary>Gets the cascade 1 distance override bar.</summary>
    public InspectorFieldOverride CascadeDistance1Override => this.Override(this.cascadeDistance0Binding.Id.Id);

    /// <summary>Gets the cascade 2 distance override bar.</summary>
    public InspectorFieldOverride CascadeDistance2Override => this.Override(this.cascadeDistance1Binding.Id.Id);

    /// <summary>Gets the cascade 3 distance override bar.</summary>
    public InspectorFieldOverride CascadeDistance3Override => this.Override(this.cascadeDistance2Binding.Id.Id);

    /// <summary>Gets the cascade 4 distance override bar.</summary>
    public InspectorFieldOverride CascadeDistance4Override => this.Override(this.cascadeDistance3Binding.Id.Id);

    /// <summary>Gets the transition fraction override bar.</summary>
    public InspectorFieldOverride TransitionFractionOverride => this.Override(this.transitionFractionBinding.Id.Id);

    /// <summary>Gets the fade-out fraction override bar.</summary>
    public InspectorFieldOverride DistanceFadeoutFractionOverride => this.Override(this.distanceFadeoutFractionBinding.Id.Id);

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
        this.edits?.Bind([.. items.Where(node => node.Components.Any(component => component is DirectionalLightComponent)).Select(this.ReferenceOf)]);
        this.selectedItems = items;
        var targets = items
            .Select(static node => new { Node = node, Light = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() })
            .Where(static target => target.Light is not null)
            .ToList();
        var nodeIds = targets.ConvertAll(target => this.ReferenceOf(target.Node));
        var targetsByNode = targets.ToDictionary(target => this.ReferenceOf(target.Node), static target => (object?)target.Light);

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
        this.NotifyCascadeApplicabilityChanged();
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

    private static bool NearlyEqual(double left, double right)
        => Math.Abs(left - right) <= 0.0001;

    private void RefreshValues()
    {
        if (this.selectedItems is { } items)
        {
            this.UpdateValues(items);
        }
    }

    /// <summary>Gets the override bar of a field.</summary>
    /// <param name="ids">The properties the field shows.</param>
    /// <returns>The field's bar.</returns>
    private InspectorFieldOverride Override(params PropertyId[] ids)
        => this.edits?.Overrides.Get(ids) ?? this.unboundOverride;

    private void UpdateBinding<T>(
        PropertyBinding<T> binding,
        IReadOnlyList<NodeReference> nodeIds,
        Dictionary<NodeReference, object?> targetsByNode,
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

    partial void OnColorRChanged(double value) => this.ApplyColorAxisEdit(0, value);

    partial void OnColorGChanged(double value) => this.ApplyColorAxisEdit(1, value);

    partial void OnColorBChanged(double value) => this.ApplyColorAxisEdit(2, value);

    partial void OnAtmosphereDiskLuminanceScaleRgbChanged(Vector3 value) => this.RequestLightValue(this.atmosphereDiskLuminanceScaleRgbBinding, value);

    partial void OnSunAzimuthChanged(double value) => this.ApplySunDirectionEdit(value, this.SunElevation);

    partial void OnSunElevationChanged(double value) => this.ApplySunDirectionEdit(this.SunAzimuth, value);

    private void ApplyColorAxisEdit(int axis, double value)
    {
        this.NotifyColorChanged();
        if (this.isApplyingEditorValues || this.selectedItems is null)
        {
            return;
        }

        var perTarget = new Dictionary<NodeReference, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            var color = light.Color;
            color[axis] = (float)value;
            perTarget[this.ReferenceOf(node)] = PropertyEdit.SingleEdit(SceneSchema.DirectionalLight.Color, color);
        }

        this.edits?.Submit(perTarget);
    }

    // The view edits doubles; the light stores single precision.
    private void RequestLightNumber(NumberBinding<float> binding, double value) => this.RequestLightValue(binding, (float)value);

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
            this.ApplyLightEdit(PropertyEdit.SingleEdit(binding.Id, args.NewValue));
        }
    }

    partial void OnDiskScaleRChanged(double value) => this.ApplyDiskScaleAxisEdit(0, value);

    partial void OnDiskScaleGChanged(double value) => this.ApplyDiskScaleAxisEdit(1, value);

    partial void OnDiskScaleBChanged(double value) => this.ApplyDiskScaleAxisEdit(2, value);

    private void ApplyDiskScaleAxisEdit(int axis, double value)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null)
        {
            return;
        }

        var perTarget = new Dictionary<NodeReference, PropertyEdit>();
        foreach (var node in this.selectedItems)
        {
            if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is not { } light)
            {
                continue;
            }

            var scale = light.AtmosphereDiskLuminanceScaleRgb;
            scale[axis] = (float)value;
            perTarget[this.ReferenceOf(node)] = PropertyEdit.SingleEdit(SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, scale);
        }

        this.edits?.Submit(perTarget);
    }

    private void OnVector3ValueRequested(object? sender, PropertyBindingChangedEventArgs<Vector3> args)
    {
        if (sender is PropertyBinding<Vector3> binding)
        {
            this.ApplyLightEdit(PropertyEdit.SingleEdit(binding.Id, args.NewValue));
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

                if (property is nameof(this.SplitMode) or nameof(this.CascadeCount))
                {
                    this.NotifyCascadeApplicabilityChanged();
                }
            }));

    private void ApplyLightEdit(PropertyEdit edit)
    {
        if (!this.isApplyingEditorValues)
        {
            this.edits?.Submit(edit);
        }
    }

    private void ApplySunDirectionEdit(double azimuth, double elevation)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null || !double.IsFinite(azimuth) || !double.IsFinite(elevation))
        {
            return;
        }

        var perTarget = new Dictionary<NodeReference, PropertyEdit>();
        foreach (var node in this.selectedItems.Where(node => node.Components.Any(component => component is DirectionalLightComponent)))
        {
            var angles = TransformConverter.QuaternionToEulerDegrees(DirectionalLightOrientation.LocalRotation(node, (float)azimuth, (float)elevation));
            perTarget[this.ReferenceOf(node)] = new PropertyEdit.Builder()
                .Set(SceneSchema.Transform.Rotation, angles)
                .Build();
        }

        this.edits?.Submit(perTarget);
    }

    private void UpdateSunDirectionValues(List<SceneNode> nodes)
    {
        if (nodes.Count == 0)
        {
            this.SunAzimuth = 0f;
            this.SunElevation = 0f;
            return;
        }

        var (azimuth, elevation) = DirectionalLightOrientation.DisplayAngles(nodes[0]);
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
        => this.OnPropertyChanged(nameof(this.ColorValue));
}
