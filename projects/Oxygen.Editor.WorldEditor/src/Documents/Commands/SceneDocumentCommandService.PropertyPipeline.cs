// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using DroidNet.TimeMachine;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Services;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>
/// Property pipeline integration: descriptors for <see cref="TransformComponent"/>
/// and the schema-driven <c>EditPropertiesAsync</c> entry point.
/// </summary>
/// <remarks>
/// <para>
/// This file is the editor-side half of the property pipeline described
/// in <c>design/editor/lld/property-pipeline-redesign.md</c>. It wires
/// the schema-layer abstractions (<see cref="PropertyDescriptor{T}"/>,
/// <see cref="PropertyEdit"/>, <see cref="PropertyApply"/>,
/// <see cref="PropertyOp"/>) to the concrete C# transform model
/// (<see cref="TransformComponent"/>) and to the generic engine property sync
/// (<see cref="ISceneEngineSync.UpdatePropertiesAsync(Scene, SceneNode, IReadOnlyList{EnginePropertyValueEntry}, SceneSyncRevision, CancellationToken)"/>).
/// </para>
/// <para>
/// <b>EditTransformAsync</b> in the sibling partial keeps its public
/// signature; one-shot transform edits flow through this new path so the
/// design is exercised end-to-end without disturbing the inspector
/// view-model or its XAML bindings.
/// </para>
/// </remarks>
public sealed partial class SceneDocumentCommandService
{
    /// <summary>
    /// Component kind id used by transform property identities.
    /// </summary>
    public const string TransformKind = "transform";

    /// <summary>
    /// Component kind id used by geometry property identities.
    /// </summary>
    public const string GeometryKind = "geometry";

    /// <summary>
    /// Component kind id used by perspective camera property identities.
    /// </summary>
    public const string PerspectiveCameraKind = "perspective-camera";

    /// <summary>
    /// Component kind id used by directional light property identities.
    /// </summary>
    public const string DirectionalLightKind = "directional-light";

    /// <summary>
    /// Property kind id used by scene environment property identities.
    /// </summary>
    public const string SceneEnvironmentKind = "scene-environment";

    private const int DirectionalLightPropertyEntryCapacity = 25;

    /// <summary>
    /// Gets the canonical descriptor catalog for transform.
    /// </summary>
    public static TransformDescriptors Transform { get; } = TransformDescriptors.Build();

    /// <summary>
    /// Gets the canonical descriptor catalog for geometry.
    /// </summary>
    internal static GeometryDescriptors Geometry { get; } = GeometryDescriptors.Build();

    /// <summary>
    /// Gets the canonical descriptor catalog for perspective cameras.
    /// </summary>
    internal static PerspectiveCameraDescriptors PerspectiveCamera { get; } = PerspectiveCameraDescriptors.Build();

    /// <summary>
    /// Gets the canonical descriptor catalog for directional lights.
    /// </summary>
    internal static DirectionalLightDescriptors DirectionalLight { get; } = DirectionalLightDescriptors.Build();

    /// <summary>
    /// Gets the canonical descriptor catalog for scene environment authoring data.
    /// </summary>
    internal static SceneEnvironmentDescriptors SceneEnvironment { get; } = SceneEnvironmentDescriptors.Build();

    /// <summary>
    /// Schema-driven property edit entry point.
    /// </summary>
    /// <param name="context">The command context.</param>
    /// <param name="nodeIds">The node ids to edit.</param>
    /// <param name="edit">The property edit map.</param>
    /// <param name="label">A short, human-readable history label.</param>
    /// <returns>The command result.</returns>
    /// <remarks>
    /// <para>
    /// This is the single, generic edit path. It does the following:
    /// </para>
    /// <list type="number">
    /// <item>Resolves nodes from <paramref name="nodeIds"/>.</item>
    /// <item>Captures a <see cref="PropertySnapshot"/> of the touched
    ///   property ids on each node (the <c>before</c> operand).</item>
    /// <item>Validates each entry via the descriptor's validator. On
    ///   failure, returns the validation failure unchanged.</item>
    /// <item>Calls <see cref="PropertyApply.ApplyAsync"/> to write the
    ///   model and push the engine command.</item>
    /// <item>Captures the <c>after</c> snapshot, registers a
    ///   <see cref="PropertyOp"/> with TimeMachine, and marks the
    ///   document dirty.</item>
    /// </list>
    /// <para>
    /// Undo restores <c>before</c> by calling the same
    /// <see cref="PropertyApply.ApplyAsync"/> with
    /// <see cref="ApplySide.Before"/>; redo applies <c>after</c>. The
    /// structural identity <c>redo(OP) == undo(UOP) == OP</c> holds by
    /// construction.
    /// </para>
    /// </remarks>
    public async Task<SceneCommandResult> EditPropertiesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        string label)
        => await this.EditPropertiesAsync(context, nodeIds, edit, label, EditSessionToken.OneShot).ConfigureAwait(true);

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditPropertiesAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        string label,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        ArgumentNullException.ThrowIfNull(session);

        if (!session.IsOneShot)
        {
            return await this.EditPropertiesForTargetsAsync(context, nodeIds.ToDictionary(id => id, _ => edit), label, session).ConfigureAwait(true);
        }

        if (edit.Count == 0)
        {
            return SceneCommandResult.Success;
        }

        var kind = GetSingleComponentKind(edit);
        return kind is null
            ? this.ValidationFailure(
                SceneOperationKinds.EditTransform,
                "PROPERTY_MIXED_COMPONENTS",
                "Property edit rejected",
                "A single property edit can target only one component kind.",
                context)
            : !string.Equals(kind, TransformKind, StringComparison.Ordinal)
            ? await this.EditComponentPropertiesThroughExistingCommandAsync(
                context,
                nodeIds,
                edit,
                kind,
                session).ConfigureAwait(true)
            : this.ValidateComponentPropertyEdit(context, edit, kind) is { } validationResult
            ? validationResult
            : await this.EditTransformPropertiesOneShotAsync(context, nodeIds, edit, label).ConfigureAwait(true);
    }

    /// <inheritdoc />
    public async Task<SceneCommandResult> EditSceneEnvironmentPropertiesAsync(
        SceneDocumentCommandContext context,
        PropertyEdit edit,
        string label,
        EditSessionToken session)
    {
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return new SceneCommandResult(Succeeded: false);
        }

        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);
        ArgumentNullException.ThrowIfNull(session);

        if (!session.IsOneShot)
        {
            return await this.EditPropertiesForTargetsAsync(context, new Dictionary<Guid, PropertyEdit> { [context.Scene.Id] = edit }, label, session).ConfigureAwait(true);
        }

        if (edit.Count == 0)
        {
            return SceneCommandResult.Success;
        }

        var kind = GetSingleComponentKind(edit);
        if (!string.Equals(kind, SceneEnvironmentKind, StringComparison.Ordinal))
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditEnvironment,
                "PROPERTY_UNKNOWN",
                "Environment edit rejected",
                "The supplied property edit does not target the scene environment.",
                context);
        }

        var (environmentEdit, result) = this.TryBuildSceneEnvironmentEdit(context, edit);
        return result ?? await this.EditSceneEnvironmentAsync(context, environmentEdit, session).ConfigureAwait(true);
    }

    private static (Dictionary<Guid, object> nodeTargets, Dictionary<Guid, SceneNode> sceneNodes) ResolveTransformPropertyTargets(
        IReadOnlyList<SceneNode> nodes)
    {
        var nodeTargets = new Dictionary<Guid, object>();
        var sceneNodes = new Dictionary<Guid, SceneNode>();
        foreach (var node in nodes)
        {
            var target = node.Components.OfType<TransformComponent>().FirstOrDefault();
            if (target is null)
            {
                continue;
            }

            nodeTargets[node.Id] = target;
            sceneNodes[node.Id] = node;
        }

        return (nodeTargets, sceneNodes);
    }

    private static List<PropertyDescriptor> GetTouchedDescriptors(
        PropertyEdit edit,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
    {
        var touchedDescriptors = new List<PropertyDescriptor>(edit.Count);
        foreach (var id in edit.Ids)
        {
            touchedDescriptors.Add(descriptors[id]);
        }

        return touchedDescriptors;
    }

    private static PropertyOp BuildPropertyOp(
        PropertyEdit edit,
        string label,
        IReadOnlyDictionary<Guid, object> nodeTargets,
        IReadOnlyList<PropertyDescriptor> touchedDescriptors)
    {
        var before = PropertySnapshot.Capture(nodeTargets, touchedDescriptors);
        var afterPerNode = new Dictionary<Guid, PropertyEdit>();
        foreach (var nodeId in nodeTargets.Keys)
        {
            afterPerNode[nodeId] = edit.Clone();
        }

        return new PropertyOp(
            Nodes: [.. nodeTargets.Keys],
            Before: before,
            After: new PropertySnapshot(afterPerNode),
            Label: label);
    }

    /// <summary>
    /// Builds a <see cref="PropertyEdit"/> for transform from the
    /// existing <see cref="TransformEdit"/> record. Used by the
    /// <c>EditTransformAsync</c> adapter path.
    /// </summary>
    /// <param name="edit">The legacy edit record.</param>
    /// <returns>The translated property edit. Empty fields are omitted.</returns>
    private static PropertyEdit BuildPropertyEditFromTransformEdit(TransformEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        var result = new PropertyEdit();

        SetOptional(result, Transform.PositionX, edit.PositionX);
        SetOptional(result, Transform.PositionY, edit.PositionY);
        SetOptional(result, Transform.PositionZ, edit.PositionZ);
        SetOptional(result, Transform.RotationX, edit.RotationXDegrees);
        SetOptional(result, Transform.RotationY, edit.RotationYDegrees);
        SetOptional(result, Transform.RotationZ, edit.RotationZDegrees);
        SetOptional(result, Transform.ScaleX, edit.ScaleX);
        SetOptional(result, Transform.ScaleY, edit.ScaleY);
        SetOptional(result, Transform.ScaleZ, edit.ScaleZ);
        SetVector(result, edit.Position, Transform.PositionX, Transform.PositionY, Transform.PositionZ);
        SetVector(result, edit.RotationEulerDegrees, Transform.RotationX, Transform.RotationY, Transform.RotationZ);
        SetVector(result, edit.Scale, Transform.ScaleX, Transform.ScaleY, Transform.ScaleZ);

        return result;
    }

    private static void SetOptional(PropertyEdit result, PropertyId<float> id, OptionalEditValue<float> value)
    {
        if (value.HasValue)
        {
            result.Set(id, value.Value);
        }
    }

    private static void SetVector(
        PropertyEdit result,
        OptionalEditValue<Vector3> value,
        PropertyId<float> xId,
        PropertyId<float> yId,
        PropertyId<float> zId)
    {
        if (!value.HasValue)
        {
            return;
        }

        var vector = value.Value;
        result.Set(xId, vector.X);
        result.Set(yId, vector.Y);
        result.Set(zId, vector.Z);
    }

    /// <summary>
    /// Converts descriptor-addressed transform edits into the compact engine property wire format.
    /// </summary>
    /// <param name="edit">The descriptor-addressed property edit map.</param>
    /// <returns>The engine property entries that can be sent to the runtime.</returns>
    private static List<EnginePropertyValueEntry> BuildTransformPropertyEntries(PropertyEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);

        var entries = new List<EnginePropertyValueEntry>(edit.Count);
        foreach (var (id, value) in edit)
        {
            if (!Transform.ById.TryGetValue(id, out var descriptor)
                || value is not float floatValue
                || !TryMapTransformField(descriptor.EngineCommandKey, out var field))
            {
                continue;
            }

            entries.Add(new EnginePropertyValueEntry(
                EngineComponentId.Transform,
                (ushort)field,
                floatValue));
        }

        return entries;
    }

    private static bool TryMapTransformField(string engineCommandKey, out TransformField field)
    {
        switch (engineCommandKey)
        {
            case "transform.position.x": field = TransformField.PositionX; return true;
            case "transform.position.y": field = TransformField.PositionY; return true;
            case "transform.position.z": field = TransformField.PositionZ; return true;
            case "transform.rotation.x": field = TransformField.RotationXDegrees; return true;
            case "transform.rotation.y": field = TransformField.RotationYDegrees; return true;
            case "transform.rotation.z": field = TransformField.RotationZDegrees; return true;
            case "transform.scale.x": field = TransformField.ScaleX; return true;
            case "transform.scale.y": field = TransformField.ScaleY; return true;
            case "transform.scale.z": field = TransformField.ScaleZ; return true;
            default: field = default; return false;
        }
    }

    private static List<EnginePropertyValueEntry> BuildPerspectiveCameraPropertyEntries(PerspectiveCameraEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);

        var entries = new List<EnginePropertyValueEntry>(capacity: 4);
        AddOptional(entries, edit.FieldOfViewDegrees, PerspectiveCameraField.FieldOfViewYRadians, DegreesToRadians);
        AddOptional(entries, edit.AspectRatio, PerspectiveCameraField.AspectRatio);
        AddOptional(entries, edit.NearPlane, PerspectiveCameraField.NearPlane);
        AddOptional(entries, edit.FarPlane, PerspectiveCameraField.FarPlane);
        return entries;
    }

    private static List<EnginePropertyValueEntry> BuildPerspectiveCameraPropertyEntries(PerspectiveCamera camera)
    {
        ArgumentNullException.ThrowIfNull(camera);

        return
        [
            new(EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.FieldOfViewYRadians, DegreesToRadians(camera.FieldOfView)),
            new(EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.AspectRatio, camera.AspectRatio),
            new(EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.NearPlane, camera.NearPlane),
            new(EngineComponentId.PerspectiveCamera, (ushort)PerspectiveCameraField.FarPlane, camera.FarPlane),
        ];
    }

    private static List<EnginePropertyValueEntry> BuildDirectionalLightPropertyEntries(DirectionalLightEdit edit, DirectionalLightComponent light)
    {
        ArgumentNullException.ThrowIfNull(light);
        return BuildDirectionalLightPropertyEntries(edit, light.Color, light.IsSunLight);
    }

    private static List<EnginePropertyValueEntry> BuildDirectionalLightPropertyEntries(DirectionalLightEdit edit)
        => BuildDirectionalLightPropertyEntries(edit, edit.Color.HasValue ? edit.Color.Value : Vector3.Zero, edit.IsSunLight.HasValue && edit.IsSunLight.Value);

    private static List<EnginePropertyValueEntry> BuildDirectionalLightPropertyEntries(DirectionalLightEdit edit, Vector3 color, bool isSunLight)
    {
        ArgumentNullException.ThrowIfNull(edit);

        var entries = new List<EnginePropertyValueEntry>(capacity: DirectionalLightPropertyEntryCapacity);
        if (edit.Color.HasValue)
        {
            AddDirectionalLightColor(entries, color);
        }

        AddOptional(entries, edit.AffectsWorld, DirectionalLightField.AffectsWorld);
        AddOptional(entries, edit.Mobility, DirectionalLightField.Mobility);
        AddOptional(entries, edit.CastsShadows, DirectionalLightField.CastsShadows);
        AddOptional(entries, edit.ShadowBias, DirectionalLightField.ShadowBias);
        AddOptional(entries, edit.ShadowNormalBias, DirectionalLightField.ShadowNormalBias);
        AddOptional(entries, edit.ContactShadows, DirectionalLightField.ContactShadows);
        AddOptional(entries, edit.ShadowResolutionHint, DirectionalLightField.ShadowResolutionHint);
        AddOptional(entries, edit.ExposureCompensation, DirectionalLightField.ExposureCompensation);
        AddOptional(entries, edit.IntensityLux, DirectionalLightField.IntensityLux);
        AddOptional(entries, edit.AngularSizeRadians, DirectionalLightField.AngularSizeRadians);
        AddOptional(entries, edit.EnvironmentContribution, DirectionalLightField.EnvironmentContribution);
        if (edit.IsSunLight.HasValue)
        {
            entries.Add(BoolEntry(DirectionalLightField.IsSunLight, isSunLight));
        }

        AddOptional(entries, edit.CascadeCount, DirectionalLightField.CascadeCount);
        AddOptional(entries, edit.SplitMode, DirectionalLightField.SplitMode);
        AddOptional(entries, edit.MaxShadowDistance, DirectionalLightField.MaxShadowDistance);
        AddOptional(entries, edit.CascadeDistance0, DirectionalLightField.CascadeDistance0);
        AddOptional(entries, edit.CascadeDistance1, DirectionalLightField.CascadeDistance1);
        AddOptional(entries, edit.CascadeDistance2, DirectionalLightField.CascadeDistance2);
        AddOptional(entries, edit.CascadeDistance3, DirectionalLightField.CascadeDistance3);
        AddOptional(entries, edit.DistributionExponent, DirectionalLightField.DistributionExponent);
        AddOptional(entries, edit.TransitionFraction, DirectionalLightField.TransitionFraction);
        AddOptional(entries, edit.DistanceFadeoutFraction, DirectionalLightField.DistanceFadeoutFraction);
        return entries;
    }

    private static List<EnginePropertyValueEntry> BuildDirectionalLightPropertyEntries(DirectionalLightComponent light)
    {
        ArgumentNullException.ThrowIfNull(light);

        var entries = new List<EnginePropertyValueEntry>(capacity: DirectionalLightPropertyEntryCapacity);
        AddDirectionalLightColor(entries, light.Color);
        entries.Add(BoolEntry(DirectionalLightField.AffectsWorld, light.AffectsWorld));
        entries.Add(EnumEntry(DirectionalLightField.Mobility, light.Mobility));
        entries.Add(BoolEntry(DirectionalLightField.CastsShadows, light.CastsShadows));
        entries.Add(FloatEntry(DirectionalLightField.ShadowBias, light.ShadowBias));
        entries.Add(FloatEntry(DirectionalLightField.ShadowNormalBias, light.ShadowNormalBias));
        entries.Add(BoolEntry(DirectionalLightField.ContactShadows, light.ContactShadows));
        entries.Add(EnumEntry(DirectionalLightField.ShadowResolutionHint, light.ShadowResolutionHint));
        entries.Add(FloatEntry(DirectionalLightField.ExposureCompensation, light.ExposureCompensation));
        entries.Add(FloatEntry(DirectionalLightField.IntensityLux, light.IntensityLux));
        entries.Add(FloatEntry(DirectionalLightField.AngularSizeRadians, light.AngularSizeRadians));
        entries.Add(BoolEntry(DirectionalLightField.EnvironmentContribution, light.EnvironmentContribution));
        entries.Add(BoolEntry(DirectionalLightField.IsSunLight, light.IsSunLight));
        entries.Add(FloatEntry(DirectionalLightField.CascadeCount, light.CascadeCount));
        entries.Add(EnumEntry(DirectionalLightField.SplitMode, light.SplitMode));
        entries.Add(FloatEntry(DirectionalLightField.MaxShadowDistance, light.MaxShadowDistance));
        entries.Add(FloatEntry(DirectionalLightField.CascadeDistance0, light.CascadeDistances.X));
        entries.Add(FloatEntry(DirectionalLightField.CascadeDistance1, light.CascadeDistances.Y));
        entries.Add(FloatEntry(DirectionalLightField.CascadeDistance2, light.CascadeDistances.Z));
        entries.Add(FloatEntry(DirectionalLightField.CascadeDistance3, light.CascadeDistances.W));
        entries.Add(FloatEntry(DirectionalLightField.DistributionExponent, light.DistributionExponent));
        entries.Add(FloatEntry(DirectionalLightField.TransitionFraction, light.TransitionFraction));
        entries.Add(FloatEntry(DirectionalLightField.DistanceFadeoutFraction, light.DistanceFadeoutFraction));
        return entries;
    }

    private static void AddDirectionalLightColor(List<EnginePropertyValueEntry> entries, Vector3 color)
    {
        entries.Add(FloatEntry(DirectionalLightField.ColorR, color.X));
        entries.Add(FloatEntry(DirectionalLightField.ColorG, color.Y));
        entries.Add(FloatEntry(DirectionalLightField.ColorB, color.Z));
    }

    private static void AddOptional(
        List<EnginePropertyValueEntry> entries,
        OptionalEditValue<float> value,
        PerspectiveCameraField field,
        Func<float, float>? convert = null)
    {
        if (value.HasValue)
        {
            entries.Add(new EnginePropertyValueEntry(
                EngineComponentId.PerspectiveCamera,
                (ushort)field,
                convert?.Invoke(value.Value) ?? value.Value));
        }
    }

    private static void AddOptional(List<EnginePropertyValueEntry> entries, OptionalEditValue<float> value, DirectionalLightField field)
    {
        if (value.HasValue)
        {
            entries.Add(FloatEntry(field, value.Value));
        }
    }

    private static void AddOptional(List<EnginePropertyValueEntry> entries, OptionalEditValue<int> value, DirectionalLightField field)
    {
        if (value.HasValue)
        {
            entries.Add(FloatEntry(field, value.Value));
        }
    }

    private static void AddOptional(List<EnginePropertyValueEntry> entries, OptionalEditValue<bool> value, DirectionalLightField field)
    {
        if (value.HasValue)
        {
            entries.Add(BoolEntry(field, value.Value));
        }
    }

    private static void AddOptional<T>(List<EnginePropertyValueEntry> entries, OptionalEditValue<T> value, DirectionalLightField field)
        where T : struct, Enum
    {
        if (value.HasValue)
        {
            entries.Add(EnumEntry(field, value.Value));
        }
    }

    private static EnginePropertyValueEntry FloatEntry(DirectionalLightField field, float value)
        => new(EngineComponentId.DirectionalLight, (ushort)field, value);

    private static EnginePropertyValueEntry BoolEntry(DirectionalLightField field, bool value)
        => FloatEntry(field, value ? 1f : 0f);

    private static EnginePropertyValueEntry EnumEntry<T>(DirectionalLightField field, T value)
        where T : struct, Enum
        => FloatEntry(field, Convert.ToInt32(value, System.Globalization.CultureInfo.InvariantCulture));

    private static float DegreesToRadians(float degrees)
        => degrees * (MathF.PI / 180f);

    private async Task<SceneCommandResult> EditTransformPropertiesOneShotAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        PropertyEdit edit,
        string label)
    {
        var nodes = ResolveNodes(context.Scene, nodeIds);
        var (nodeTargets, sceneNodes) = ResolveTransformPropertyTargets(nodes);
        if (nodeTargets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditTransform,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Property edit ignored",
                "No selected node has a component matching the requested property ids.",
                context);
        }

        var descriptors = Transform.ById;
        var touchedDescriptors = GetTouchedDescriptors(edit, descriptors);
        var op = BuildPropertyOp(edit, label, nodeTargets, touchedDescriptors);
        if (op.EffectiveEdit().Count == 0)
        {
            return SceneCommandResult.Success;
        }

        var resolver = new TransformPropertyTarget(this, context, sceneNodes);
        PropertyApply.ApplyToTargets(op, ApplySide.After, resolver, descriptors);
        this.RegisterPropertyOpHistory(context, op, resolver, descriptors);
        var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
        await CompletePublicationAsync(metadataUpdate, PropertyApply.PushToEngineAsync(op, ApplySide.After, resolver.WithRevision(revision))).ConfigureAwait(true);
        return SceneCommandResult.Success;
    }

    private void RegisterPropertyOpHistory(
        SceneDocumentCommandContext context,
        PropertyOp op,
        TransformPropertyTarget resolver,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors)
        => this.RegisterPropertyOpHistory(
            context,
            op,
            resolver,
            descriptors,
            ApplySide.Before,
            "Restore Transform",
            ApplySide.After,
            "Reapply Transform");

    private void RegisterPropertyOpHistory(
        SceneDocumentCommandContext context,
        PropertyOp op,
        TransformPropertyTarget resolver,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors,
        ApplySide applySide,
        string label,
        ApplySide inverseSide,
        string inverseLabel)
        => context.History.AddChange(
            label,
            async () =>
            {
                using var authoring = EnterAuthoring(context);
                if (authoring is null)
                {
                    return;
                }

                PropertyApply.ApplyToTargets(op, applySide, resolver, descriptors);
                this.RegisterPropertyOpHistory(
                    context,
                    op,
                    resolver,
                    descriptors,
                    inverseSide,
                    inverseLabel,
                    applySide,
                    label);
                var metadataUpdate = this.MarkDirtyAsync(context, out var revision);
                await CompletePublicationAsync(metadataUpdate, PropertyApply.PushToEngineAsync(op, applySide, resolver.WithRevision(revision))).ConfigureAwait(true);
            });

    /// <summary>
    /// Resolves transform-component property ids to model targets and
    /// pushes engine commands via <see cref="ISceneEngineSync"/>.
    /// </summary>
    /// <param name="owner">The owning command service.</param>
    /// <param name="context">The authoring context.</param>
    /// <param name="nodes">The transform targets.</param>
    /// <param name="revision">The revision captured for this immutable transport instance.</param>
    private sealed class TransformPropertyTarget(
        SceneDocumentCommandService owner,
        SceneDocumentCommandContext context,
        IReadOnlyDictionary<Guid, SceneNode> nodes,
        SceneSyncRevision revision = default) : IPropertyTarget
    {
        public TransformPropertyTarget WithRevision(SceneSyncRevision captured)
            => new(owner, context, nodes, captured);

        public bool TryGetTarget(Guid nodeId, out object? target)
        {
            if (nodes.TryGetValue(nodeId, out var node))
            {
                target = node.Components.OfType<TransformComponent>().FirstOrDefault();
                return target is not null;
            }

            target = null;
            return false;
        }

        public Task PushToEngineAsync(Guid nodeId, PropertyEdit edit)
        {
            // Property pipeline §5.3 — translate the descriptor's
            // engineCommandKey strings into stable engine property wire ids
            // and dispatch through the new generic SetProperties
            // transport. This replaces the wide UpdateNodeTransformAsync
            // path for property-pipeline edits.
            if (!nodes.TryGetValue(nodeId, out var node))
            {
                return Task.CompletedTask;
            }

            var entries = BuildTransformPropertyEntries(edit);
            return entries.Count == 0
                ? Task.CompletedTask
                : PushAndPublishAsync(owner, context, node, entries, revision);
        }

        private static async Task PushAndPublishAsync(
            SceneDocumentCommandService owner,
            SceneDocumentCommandContext context,
            SceneNode node,
            IReadOnlyList<EnginePropertyValueEntry> entries,
            SceneSyncRevision revision)
        {
            var outcome = await owner.sceneEngineSync.UpdatePropertiesAsync(
                context.Scene,
                node,
                entries,
                revision).ConfigureAwait(true);
            _ = await owner.PublishSyncOutcomeAsync(
                context,
                SceneOperationKinds.EditTransform,
                outcome).ConfigureAwait(true);
        }
    }
}
