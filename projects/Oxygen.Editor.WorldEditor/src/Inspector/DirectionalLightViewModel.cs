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
public sealed partial class DirectionalLightViewModel : ComponentPropertyEditor, IDisposable
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;
    private static readonly Vector3 EngineForward = new(0f, -1f, 0f);
    private static readonly Vector3 EngineUp = new(0f, 0f, 1f);

    private readonly ISceneDocumentCommandService? commandService;
    private readonly Func<SceneDocumentCommandContext?>? commandContextProvider;
    private readonly SemaphoreSlim editGate = new(initialCount: 1, maxCount: 1);
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
        this.commandService = commandService;
        this.commandContextProvider = commandContextProvider;
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

    /// <inheritdoc />
    public override string Header => "Directional Light";

    /// <inheritdoc />
    public override string Description => "Authored sun, shadow, and environment light data.";

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
        this.editGate.Dispose();
        this.disposed = true;
        GC.SuppressFinalize(this);
    }

    /// <inheritdoc />
    public override void UpdateValues(ICollection<SceneNode> items)
    {
        this.selectedItems = items;
        var targets = items
            .Select(static node => new { Node = node, Light = node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() })
            .Where(static target => target.Light is not null)
            .ToList();
        var nodeIds = targets.Select(static target => target.Node.Id).ToList();
        var targetsByNode = targets.ToDictionary(static target => target.Node.Id, static target => (object?)target.Light);

        this.isApplyingEditorValues = true;
        try
        {
            this.colorBinding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
            var color = this.colorBinding.HasValue ? this.colorBinding.Value : Vector3.One;
            this.ColorR = color.X;
            this.ColorG = color.Y;
            this.ColorB = color.Z;
            UpdateBinding(this.intensityLuxBinding, nodeIds, targetsByNode, value => this.IntensityLux = value);
            UpdateBinding(this.isSunLightBinding, nodeIds, targetsByNode, value => this.IsSunLight = value);
            UpdateBinding(this.environmentContributionBinding, nodeIds, targetsByNode, value => this.EnvironmentContribution = value);
            UpdateBinding(this.castsShadowsBinding, nodeIds, targetsByNode, value => this.CastsShadows = value);
            UpdateBinding(this.affectsWorldBinding, nodeIds, targetsByNode, value => this.AffectsWorld = value);
            UpdateBinding(this.angularSizeRadiansBinding, nodeIds, targetsByNode, value => this.AngularSizeRadians = value);
            UpdateBinding(this.exposureCompensationBinding, nodeIds, targetsByNode, value => this.ExposureCompensation = value);
            UpdateBinding(this.mobilityBinding, nodeIds, targetsByNode, value => this.Mobility = value);
            UpdateBinding(this.shadowBiasBinding, nodeIds, targetsByNode, value => this.ShadowBias = value);
            UpdateBinding(this.shadowNormalBiasBinding, nodeIds, targetsByNode, value => this.ShadowNormalBias = value);
            UpdateBinding(this.contactShadowsBinding, nodeIds, targetsByNode, value => this.ContactShadows = value);
            UpdateBinding(this.shadowResolutionHintBinding, nodeIds, targetsByNode, value => this.ShadowResolutionHint = value);
            UpdateBinding(this.cascadeCountBinding, nodeIds, targetsByNode, value => this.CascadeCount = value);
            UpdateBinding(this.splitModeBinding, nodeIds, targetsByNode, value => this.SplitMode = value);
            UpdateBinding(this.maxShadowDistanceBinding, nodeIds, targetsByNode, value => this.MaxShadowDistance = value);
            UpdateBinding(this.cascadeDistance0Binding, nodeIds, targetsByNode, value => this.CascadeDistance1 = value);
            UpdateBinding(this.cascadeDistance1Binding, nodeIds, targetsByNode, value => this.CascadeDistance2 = value);
            UpdateBinding(this.cascadeDistance2Binding, nodeIds, targetsByNode, value => this.CascadeDistance3 = value);
            UpdateBinding(this.cascadeDistance3Binding, nodeIds, targetsByNode, value => this.CascadeDistance4 = value);
            UpdateBinding(this.distributionExponentBinding, nodeIds, targetsByNode, value => this.DistributionExponent = value);
            UpdateBinding(this.transitionFractionBinding, nodeIds, targetsByNode, value => this.TransitionFraction = value);
            UpdateBinding(this.distanceFadeoutFractionBinding, nodeIds, targetsByNode, value => this.DistanceFadeoutFraction = value);
            this.UpdateSunDirectionValues(targets.Select(static target => target.Node).ToList());
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

    private static void UpdateBinding<T>(
        PropertyBinding<T> binding,
        IReadOnlyList<Guid> nodeIds,
        Dictionary<Guid, object?> targetsByNode,
        Action<T> setValue)
    {
        binding.UpdateFromModel(nodeIds, nodeId => targetsByNode.TryGetValue(nodeId, out var target) ? target : null);
        setValue(binding.HasValue ? binding.Value : default!);
    }

    private static byte ToByte(float value)
        => (byte)Math.Clamp(MathF.Round(Math.Clamp(value, 0f, 1f) * 255f), 0f, 255f);

    private static bool NearlyEqual(float left, float right)
        => MathF.Abs(left - right) <= 0.0001f;

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
        if (node.Parent is null || node.IgnoreParentTransform)
        {
            return NormalizeOrIdentity(local);
        }

        return NormalizeOrIdentity(ResolveWorldRotation(node.Parent) * local);
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

    partial void OnColorRChanged(float value) => this.ApplyColorAxisEdit(value, this.ColorG, this.ColorB);

    partial void OnColorGChanged(float value) => this.ApplyColorAxisEdit(this.ColorR, value, this.ColorB);

    partial void OnColorBChanged(float value) => this.ApplyColorAxisEdit(this.ColorR, this.ColorG, value);

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

    private void ApplyColorAxisEdit(float r, float g, float b)
    {
        this.NotifyColorChanged();
        this.RequestLightValue(this.colorBinding, new Vector3(r, g, b));
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
        if (this.isApplyingEditorValues || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        if (this.commandService is null || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var nodes = this.selectedItems.ToList();
        _ = this.ApplyLightEditAsync(context, nodes, edit);
    }

    private void ApplySunDirectionEdit(float azimuth, float elevation)
    {
        if (this.isApplyingEditorValues || this.selectedItems is null || this.selectedItems.Count == 0)
        {
            return;
        }

        if (!float.IsFinite(azimuth) || !float.IsFinite(elevation))
        {
            return;
        }

        if (this.commandService is null || this.commandContextProvider?.Invoke() is not { } context)
        {
            return;
        }

        var nodes = this.selectedItems
            .Where(static node => node.Components.OfType<DirectionalLightComponent>().Any())
            .ToList();
        _ = this.ApplySunDirectionEditAsync(context, nodes, azimuth, elevation);
    }

    private async Task ApplyLightEditAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        PropertyEdit edit)
    {
        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            var result = await this.commandService!.EditPropertiesAsync(
                context,
                nodes.Select(static node => node.Id).ToList(),
                edit,
                "Edit Directional Light",
                EditSessionToken.OneShot).ConfigureAwait(true);
            if (!result.Succeeded || !this.SelectionMatches(nodes))
            {
                return;
            }

            this.isApplyingEditorValues = true;
            try
            {
                this.UpdateValues(nodes.ToList());
            }
            finally
            {
                this.isApplyingEditorValues = false;
            }
        }
        catch (InvalidOperationException)
        {
            // Live-sync failures are published by the command service. Keep the
            // property editor alive if an async command path rejects or throws.
        }
        catch (OperationCanceledException)
        {
            // The edit was canceled by the active document workflow.
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private async Task ApplySunDirectionEditAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<SceneNode> nodes,
        float azimuth,
        float elevation)
    {
        await this.editGate.WaitAsync().ConfigureAwait(true);
        try
        {
            foreach (var node in nodes)
            {
                if (node.Components.OfType<TransformComponent>().FirstOrDefault() is null)
                {
                    continue;
                }

                var localRotation = BuildLocalRotationForSunDirection(node, azimuth, elevation);
                var euler = TransformConverter.QuaternionToEulerDegrees(localRotation);
                var result = await this.commandService!.EditTransformAsync(
                    context,
                    [node.Id],
                    new TransformEdit(
                        OptionalEditValues.Unspecified<Vector3>(),
                        OptionalEditValues.Supplied<Vector3>(euler),
                        OptionalEditValues.Unspecified<Vector3>()),
                    EditSessionToken.OneShot).ConfigureAwait(true);
                if (!result.Succeeded)
                {
                    return;
                }
            }

            if (!this.SelectionMatches(nodes))
            {
                return;
            }

            this.isApplyingEditorValues = true;
            try
            {
                this.UpdateValues(nodes.ToList());
            }
            finally
            {
                this.isApplyingEditorValues = false;
            }
        }
        catch (InvalidOperationException)
        {
            // The command service publishes live-sync failures; keep the editor responsive.
        }
        catch (OperationCanceledException)
        {
            // The edit was canceled by the active document workflow.
        }
        finally
        {
            _ = this.editGate.Release();
        }
    }

    private bool SelectionMatches(IReadOnlyCollection<SceneNode> nodes)
    {
        if (this.selectedItems is null || this.selectedItems.Count != nodes.Count)
        {
            return false;
        }

        var expectedIds = nodes.Select(static node => node.Id).ToHashSet();
        return this.selectedItems.All(node => expectedIds.Contains(node.Id));
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
