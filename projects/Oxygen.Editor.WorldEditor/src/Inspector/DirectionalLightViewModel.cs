// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using CommunityToolkit.Mvvm.ComponentModel;
using DroidNet.Controls;
using Microsoft.UI;
using Microsoft.UI.Xaml.Media;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Windows.UI;

namespace Oxygen.Editor.World.Inspector;

/// <summary>
/// ViewModel for directional light inspector editing.
/// </summary>
public sealed partial class DirectionalLightViewModel : ComponentPropertyEditor, IDisposable, IInspectorEditSessionOwner
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;
    private static readonly Vector3 EngineForward = new(0f, -1f, 0f);
    private static readonly Vector3 EngineUp = new(0f, 0f, 1f);

    private readonly InspectorFieldDiagnostic unboundDiagnostic = new();

    private readonly InspectorEditSessionCoordinator? edits;
    private readonly PropertyBinding<Vector3> colorBinding = new(SceneDocumentCommandService.DirectionalLight.ColorDescriptor);
    private readonly PropertyBinding<float> intensityLuxBinding = new(SceneDocumentCommandService.DirectionalLight.IntensityLuxDescriptor);
    private readonly PropertyBinding<bool> isSunLightBinding = new(SceneDocumentCommandService.DirectionalLight.IsSunLightDescriptor);
    private readonly PropertyBinding<bool> environmentContributionBinding = new(SceneDocumentCommandService.DirectionalLight.EnvironmentContributionDescriptor);
    private readonly PropertyBinding<bool> castsShadowsBinding = new(SceneDocumentCommandService.DirectionalLight.CastsShadowsDescriptor);
    private readonly PropertyBinding<bool> affectsWorldBinding = new(SceneDocumentCommandService.DirectionalLight.AffectsWorldDescriptor);
    private readonly PropertyBinding<float> angularSizeRadiansBinding = new(SceneDocumentCommandService.DirectionalLight.AngularSizeRadiansDescriptor);
    private readonly PropertyBinding<float> exposureCompensationBinding = new(SceneDocumentCommandService.DirectionalLight.ExposureCompensationDescriptor);
    private readonly PropertyBinding<LightMobility> mobilityBinding = new(SceneDocumentCommandService.DirectionalLight.MobilityDescriptor);
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
        this.intensityLuxBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.angularSizeRadiansBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.exposureCompensationBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.shadowBiasBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.shadowNormalBiasBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.maxShadowDistanceBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.cascadeDistance0Binding.ValueRequested += this.OnLightFloatValueRequested;
        this.cascadeDistance1Binding.ValueRequested += this.OnLightFloatValueRequested;
        this.cascadeDistance2Binding.ValueRequested += this.OnLightFloatValueRequested;
        this.cascadeDistance3Binding.ValueRequested += this.OnLightFloatValueRequested;
        this.distributionExponentBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.transitionFractionBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.distanceFadeoutFractionBinding.ValueRequested += this.OnLightFloatValueRequested;
        this.isSunLightBinding.ValueRequested += this.OnLightBoolValueRequested;
        this.environmentContributionBinding.ValueRequested += this.OnLightBoolValueRequested;
        this.castsShadowsBinding.ValueRequested += this.OnLightBoolValueRequested;
        this.affectsWorldBinding.ValueRequested += this.OnLightBoolValueRequested;
        this.contactShadowsBinding.ValueRequested += this.OnLightBoolValueRequested;
        this.cascadeCountBinding.ValueRequested += this.OnLightIntValueRequested;
        this.mobilityBinding.ValueRequested += this.OnLightMobilityValueRequested;
        this.shadowResolutionHintBinding.ValueRequested += this.OnLightShadowResolutionHintValueRequested;
        this.splitModeBinding.ValueRequested += this.OnLightSplitModeValueRequested;
    }

    [ObservableProperty]
    public partial float ColorR { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorG { get; set; } = 1f;

    [ObservableProperty]
    public partial float ColorB { get; set; } = 1f;

    [ObservableProperty]
    public partial float IntensityLux { get; set; }

    [ObservableProperty]
    public partial bool IsSunLight { get; set; }

    [ObservableProperty]
    public partial bool EnvironmentContribution { get; set; }

    [ObservableProperty]
    public partial bool CastsShadows { get; set; }

    [ObservableProperty]
    public partial bool AffectsWorld { get; set; }

    [ObservableProperty]
    public partial float AngularSizeRadians { get; set; }

    [ObservableProperty]
    public partial float ExposureCompensation { get; set; }

    [ObservableProperty]
    public partial LightMobility Mobility { get; set; }

    [ObservableProperty]
    public partial float ShadowBias { get; set; }

    [ObservableProperty]
    public partial float ShadowNormalBias { get; set; }

    [ObservableProperty]
    public partial bool ContactShadows { get; set; }

    [ObservableProperty]
    public partial ShadowResolutionHint ShadowResolutionHint { get; set; }

    [ObservableProperty]
    public partial float CascadeCount { get; set; } = DirectionalLightComponent.DefaultCascadeCount;

    [ObservableProperty]
    public partial DirectionalCsmSplitMode SplitMode { get; set; }

    [ObservableProperty]
    public partial float MaxShadowDistance { get; set; } = DirectionalLightComponent.DefaultMaxShadowDistance;

    [ObservableProperty]
    public partial float CascadeDistance1 { get; set; } = DirectionalLightComponent.DefaultCascadeDistances.X;

    [ObservableProperty]
    public partial float CascadeDistance2 { get; set; } = DirectionalLightComponent.DefaultCascadeDistances.Y;

    [ObservableProperty]
    public partial float CascadeDistance3 { get; set; } = DirectionalLightComponent.DefaultCascadeDistances.Z;

    [ObservableProperty]
    public partial float CascadeDistance4 { get; set; } = DirectionalLightComponent.DefaultCascadeDistances.W;

    [ObservableProperty]
    public partial float DistributionExponent { get; set; } = DirectionalLightComponent.DefaultDistributionExponent;

    [ObservableProperty]
    public partial float TransitionFraction { get; set; } = DirectionalLightComponent.DefaultTransitionFraction;

    [ObservableProperty]
    public partial float DistanceFadeoutFraction { get; set; } = DirectionalLightComponent.DefaultDistanceFadeoutFraction;

    [ObservableProperty]
    public partial float SunAzimuth { get; set; }

    [ObservableProperty]
    public partial float SunElevation { get; set; } = 45f;

    /// <summary>
    /// Gets light mobility options for the editor.
    /// </summary>
    public IReadOnlyList<LightMobility> MobilityOptions { get; } = Enum.GetValues<LightMobility>();

    /// <summary>
    /// Gets shadow resolution options for the editor.
    /// </summary>
    public IReadOnlyList<ShadowResolutionHint> ShadowResolutionOptions { get; } = Enum.GetValues<ShadowResolutionHint>();

    /// <summary>
    /// Gets CSM split mode options for the editor.
    /// </summary>
    public IReadOnlyList<DirectionalCsmSplitMode> SplitModeOptions { get; } = Enum.GetValues<DirectionalCsmSplitMode>();

    /// <summary>
    /// Gets a brush for the light color swatch.
    /// </summary>
    public SolidColorBrush ColorBrush => new(this.ColorValue);

    /// <summary>
    /// Gets the current light color as a WinUI color.
    /// </summary>
    public Color ColorValue => Color.FromArgb(255, ToByte(this.ColorR), ToByte(this.ColorG), ToByte(this.ColorB));

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

    /// <summary>Gets current diagnostics for IsSunLight.</summary>
    public InspectorFieldDiagnostic IsSunLightDiagnostic => this.edits?.Diagnostics.Get(this.isSunLightBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for EnvironmentContribution.</summary>
    public InspectorFieldDiagnostic EnvironmentContributionDiagnostic => this.edits?.Diagnostics.Get(this.environmentContributionBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for CastsShadows.</summary>
    public InspectorFieldDiagnostic CastsShadowsDiagnostic => this.edits?.Diagnostics.Get(this.castsShadowsBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AffectsWorld.</summary>
    public InspectorFieldDiagnostic AffectsWorldDiagnostic => this.edits?.Diagnostics.Get(this.affectsWorldBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for AngularSizeRadians.</summary>
    public InspectorFieldDiagnostic AngularSizeRadiansDiagnostic => this.edits?.Diagnostics.Get(this.angularSizeRadiansBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for ExposureCompensation.</summary>
    public InspectorFieldDiagnostic ExposureCompensationDiagnostic => this.edits?.Diagnostics.Get(this.exposureCompensationBinding.Id.Id) ?? this.unboundDiagnostic;

    /// <summary>Gets current diagnostics for Mobility.</summary>
    public InspectorFieldDiagnostic MobilityDiagnostic => this.edits?.Diagnostics.Get(this.mobilityBinding.Id.Id) ?? this.unboundDiagnostic;

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
        this.intensityLuxBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.angularSizeRadiansBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.exposureCompensationBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.shadowBiasBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.shadowNormalBiasBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.maxShadowDistanceBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.cascadeDistance0Binding.ValueRequested -= this.OnLightFloatValueRequested;
        this.cascadeDistance1Binding.ValueRequested -= this.OnLightFloatValueRequested;
        this.cascadeDistance2Binding.ValueRequested -= this.OnLightFloatValueRequested;
        this.cascadeDistance3Binding.ValueRequested -= this.OnLightFloatValueRequested;
        this.distributionExponentBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.transitionFractionBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.distanceFadeoutFractionBinding.ValueRequested -= this.OnLightFloatValueRequested;
        this.isSunLightBinding.ValueRequested -= this.OnLightBoolValueRequested;
        this.environmentContributionBinding.ValueRequested -= this.OnLightBoolValueRequested;
        this.castsShadowsBinding.ValueRequested -= this.OnLightBoolValueRequested;
        this.affectsWorldBinding.ValueRequested -= this.OnLightBoolValueRequested;
        this.contactShadowsBinding.ValueRequested -= this.OnLightBoolValueRequested;
        this.cascadeCountBinding.ValueRequested -= this.OnLightIntValueRequested;
        this.mobilityBinding.ValueRequested -= this.OnLightMobilityValueRequested;
        this.shadowResolutionHintBinding.ValueRequested -= this.OnLightShadowResolutionHintValueRequested;
        this.splitModeBinding.ValueRequested -= this.OnLightSplitModeValueRequested;
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
            this.ColorR = color.X;
            this.ColorG = color.Y;
            this.ColorB = color.Z;
            this.UpdateBinding(this.intensityLuxBinding, nodeIds, targetsByNode, value => this.IntensityLux = value);
            this.UpdateBinding(this.isSunLightBinding, nodeIds, targetsByNode, value => this.IsSunLight = value);
            this.UpdateBinding(this.environmentContributionBinding, nodeIds, targetsByNode, value => this.EnvironmentContribution = value);
            this.UpdateBinding(this.castsShadowsBinding, nodeIds, targetsByNode, value => this.CastsShadows = value);
            this.UpdateBinding(this.affectsWorldBinding, nodeIds, targetsByNode, value => this.AffectsWorld = value);
            this.UpdateBinding(this.angularSizeRadiansBinding, nodeIds, targetsByNode, value => this.AngularSizeRadians = value);
            this.UpdateBinding(this.exposureCompensationBinding, nodeIds, targetsByNode, value => this.ExposureCompensation = value);
            this.UpdateBinding(this.mobilityBinding, nodeIds, targetsByNode, value => this.Mobility = value);
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
    /// Applies the selected color from the color picker.
    /// </summary>
    /// <param name="color">The selected light color.</param>
    public void SetColor(Color color)
    {
        var r = color.R / 255f;
        var g = color.G / 255f;
        var b = color.B / 255f;
        if (NearlyEqual(this.ColorR, r) && NearlyEqual(this.ColorG, g) && NearlyEqual(this.ColorB, b))
        {
            return;
        }

        this.isApplyingEditorValues = true;
        try
        {
            this.ColorR = r;
            this.ColorG = g;
            this.ColorB = b;
        }
        finally
        {
            this.isApplyingEditorValues = false;
        }

        this.NotifyColorChanged();
        this.colorBinding.Value = new Vector3(r, g, b);
    }

    private static Quaternion BuildLocalRotationForSunDirection(SceneNode node, float azimuthDegrees, float elevationDegrees)
    {
        var desiredWorldRotation = BuildWorldRotationForSunDirection(azimuthDegrees, elevationDegrees);
        if (node.Parent is null || node.IgnoreParentTransform)
        {
            return desiredWorldRotation;
        }

        var parentWorldRotation = ResolveWorldRotation(node.Parent);
        return NormalizeOrIdentity(Quaternion.Inverse(parentWorldRotation) * desiredWorldRotation);
    }

    private static Quaternion BuildWorldRotationForSunDirection(float azimuthDegrees, float elevationDegrees)
    {
        var azimuth = azimuthDegrees * DegToRad;
        var elevation = Math.Clamp(elevationDegrees, -89.9f, 89.9f) * DegToRad;
        var cosElevation = MathF.Cos(elevation);
        var directionToLight = NormalizeOrFallback(
            new Vector3(
                MathF.Sin(azimuth) * cosElevation,
                MathF.Cos(azimuth) * cosElevation,
                MathF.Sin(elevation)),
            EngineUp);
        var emittedRayDirection = -directionToLight;
        return CreateRotationFromForward(emittedRayDirection);
    }

    private static Quaternion ResolveWorldRotation(SceneNode node)
    {
        var local = node.Components.OfType<TransformComponent>().FirstOrDefault()?.LocalRotation ?? Quaternion.Identity;
        return node.Parent is null || node.IgnoreParentTransform
            ? NormalizeOrIdentity(local)
            : NormalizeOrIdentity(ResolveWorldRotation(node.Parent) * local);
    }

    private static Quaternion CreateRotationFromForward(Vector3 targetDirection)
    {
        var to = NormalizeOrFallback(targetDirection, EngineForward);
        var dot = Math.Clamp(Vector3.Dot(EngineForward, to), -1f, 1f);
        if (dot > 0.9999f)
        {
            return Quaternion.Identity;
        }

        if (dot < -0.9999f)
        {
            return Quaternion.CreateFromAxisAngle(EngineUp, MathF.PI);
        }

        var axis = NormalizeOrFallback(Vector3.Cross(EngineForward, to), EngineUp);
        return NormalizeOrIdentity(Quaternion.CreateFromAxisAngle(axis, MathF.Acos(dot)));
    }

    private static Vector3 NormalizeOrFallback(Vector3 value, Vector3 fallback)
    {
        var lengthSquared = value.LengthSquared();
        return float.IsFinite(lengthSquared) && lengthSquared > 0.000001f
            ? Vector3.Normalize(value)
            : fallback;
    }

    private static Quaternion NormalizeOrIdentity(Quaternion value)
    {
        var lengthSquared = value.LengthSquared();
        return float.IsFinite(lengthSquared) && lengthSquared > 0.000001f
            ? Quaternion.Normalize(value)
            : Quaternion.Identity;
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0f, 255f);

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

    partial void OnIntensityLuxChanged(float value) => this.RequestLightValue(this.intensityLuxBinding, value);

    partial void OnIsSunLightChanged(bool value) => this.RequestLightValue(this.isSunLightBinding, value);

    partial void OnEnvironmentContributionChanged(bool value) => this.RequestLightValue(this.environmentContributionBinding, value);

    partial void OnCastsShadowsChanged(bool value) => this.RequestLightValue(this.castsShadowsBinding, value);

    partial void OnAffectsWorldChanged(bool value) => this.RequestLightValue(this.affectsWorldBinding, value);

    partial void OnAngularSizeRadiansChanged(float value) => this.RequestLightValue(this.angularSizeRadiansBinding, value);

    partial void OnExposureCompensationChanged(float value) => this.RequestLightValue(this.exposureCompensationBinding, value);

    partial void OnMobilityChanged(LightMobility value) => this.RequestLightValue(this.mobilityBinding, value);

    partial void OnShadowBiasChanged(float value) => this.RequestLightValue(this.shadowBiasBinding, value);

    partial void OnShadowNormalBiasChanged(float value) => this.RequestLightValue(this.shadowNormalBiasBinding, value);

    partial void OnContactShadowsChanged(bool value) => this.RequestLightValue(this.contactShadowsBinding, value);

    partial void OnShadowResolutionHintChanged(ShadowResolutionHint value) => this.RequestLightValue(this.shadowResolutionHintBinding, value);

    partial void OnCascadeCountChanged(float value) => this.RequestLightValue(this.cascadeCountBinding, Math.Clamp((int)MathF.Round(value), 1, 4));

    partial void OnSplitModeChanged(DirectionalCsmSplitMode value) => this.RequestLightValue(this.splitModeBinding, value);

    partial void OnMaxShadowDistanceChanged(float value) => this.RequestLightValue(this.maxShadowDistanceBinding, value);

    partial void OnCascadeDistance1Changed(float value) => this.RequestLightValue(this.cascadeDistance0Binding, value);

    partial void OnCascadeDistance2Changed(float value) => this.RequestLightValue(this.cascadeDistance1Binding, value);

    partial void OnCascadeDistance3Changed(float value) => this.RequestLightValue(this.cascadeDistance2Binding, value);

    partial void OnCascadeDistance4Changed(float value) => this.RequestLightValue(this.cascadeDistance3Binding, value);

    partial void OnDistributionExponentChanged(float value) => this.RequestLightValue(this.distributionExponentBinding, value);

    partial void OnTransitionFractionChanged(float value) => this.RequestLightValue(this.transitionFractionBinding, value);

    partial void OnDistanceFadeoutFractionChanged(float value) => this.RequestLightValue(this.distanceFadeoutFractionBinding, value);

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

        binding.Value = value;
    }

    private void OnLightVectorValueRequested(object? sender, PropertyBindingChangedEventArgs<Vector3> args)
    {
        if (sender is PropertyBinding<Vector3> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightFloatValueRequested(object? sender, PropertyBindingChangedEventArgs<float> args)
    {
        if (sender is PropertyBinding<float> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightIntValueRequested(object? sender, PropertyBindingChangedEventArgs<int> args)
    {
        if (sender is PropertyBinding<int> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightBoolValueRequested(object? sender, PropertyBindingChangedEventArgs<bool> args)
    {
        if (sender is PropertyBinding<bool> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightMobilityValueRequested(object? sender, PropertyBindingChangedEventArgs<LightMobility> args)
    {
        if (sender is PropertyBinding<LightMobility> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightShadowResolutionHintValueRequested(object? sender, PropertyBindingChangedEventArgs<ShadowResolutionHint> args)
    {
        if (sender is PropertyBinding<ShadowResolutionHint> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

    private void OnLightSplitModeValueRequested(object? sender, PropertyBindingChangedEventArgs<DirectionalCsmSplitMode> args)
    {
        if (sender is PropertyBinding<DirectionalCsmSplitMode> binding)
        {
            this.ApplyLightEdit(PropertyEdit.Single(binding.Id, args.NewValue));
        }
    }

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
            var angles = TransformConverter.QuaternionToEulerDegrees(BuildLocalRotationForSunDirection(node, azimuth, elevation));
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

        var worldRotation = ResolveWorldRotation(node);
        var emittedRayDirection = NormalizeOrFallback(Vector3.Transform(EngineForward, worldRotation), EngineForward);
        var directionToLight = -emittedRayDirection;
        var horizontalLength = MathF.Sqrt((directionToLight.X * directionToLight.X) + (directionToLight.Y * directionToLight.Y));
        this.SunAzimuth = TransformConverter.NormalizeAngle(MathF.Atan2(directionToLight.X, directionToLight.Y) * RadToDeg);
        this.SunElevation = MathF.Atan2(directionToLight.Z, horizontalLength) * RadToDeg;
    }

    private void NotifyColorChanged()
    {
        this.OnPropertyChanged(nameof(this.ColorValue));
        this.OnPropertyChanged(nameof(this.ColorBrush));
    }
}
