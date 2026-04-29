// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using System.Numerics;
using DroidNet.TimeMachine;
using Oxygen.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Utils;

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
/// (<see cref="ISceneEngineSync.UpdatePropertiesAsync"/>).
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
    /// Gets the canonical descriptor catalog for transform.
    /// </summary>
    public static TransformDescriptors Transform { get; } = TransformDescriptors.Build();

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
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        ArgumentNullException.ThrowIfNull(edit);
        ArgumentException.ThrowIfNullOrWhiteSpace(label);

        if (edit.Count == 0)
        {
            return SceneCommandResult.Success;
        }

        // 1. Resolve nodes & their relevant component model targets. The
        //    schema layer never sees the scene types directly; it only
        //    sees IPropertyTarget.
        var nodes = ResolveNodes(context.Scene, nodeIds);
        var nodeTargets = new Dictionary<Guid, object>();
        var sceneNodes = new Dictionary<Guid, SceneNode>();
        foreach (var node in nodes)
        {
            // For transform we know the target is the transform
            // component. Future kinds dispatch on edit.Ids[0].ComponentKind.
            var target = node.Components.OfType<TransformComponent>().FirstOrDefault();
            if (target is null)
            {
                continue;
            }

            nodeTargets[node.Id] = target;
            sceneNodes[node.Id] = node;
        }

        if (nodeTargets.Count == 0)
        {
            return this.ValidationFailure(
                SceneOperationKinds.EditTransform,
                SceneDiagnosticCodes.ComponentRemoveDenied,
                "Property edit ignored",
                "No selected node has a component matching the requested property ids.",
                context);
        }

        // 2. Validate each entry.
        var descriptors = Transform.ById;
        foreach (var (id, value) in edit)
        {
            if (!descriptors.TryGetValue(id, out var descriptor))
            {
                return this.ValidationFailure(
                    SceneOperationKinds.EditTransform,
                    "PROPERTY_UNKNOWN",
                    "Property edit rejected",
                    $"Unknown property id: {id.Qualified()}.",
                    context);
            }

            var result = descriptor.ValidateBoxed(value);
            if (!result.IsValid)
            {
                return this.ValidationFailure(
                    SceneOperationKinds.EditTransform,
                    result.Code,
                    "Property edit rejected",
                    result.Message,
                    context);
            }
        }

        // 3. Capture Before snapshot (only for the touched property ids).
        var touchedDescriptors = new List<PropertyDescriptor>(edit.Count);
        foreach (var id in edit.Ids)
        {
            touchedDescriptors.Add(descriptors[id]);
        }

        var before = PropertySnapshot.Capture(nodeTargets, touchedDescriptors);

        // 4. Build the per-node After edit map (every node receives the
        //    same edit in this initial slice; future iterations may
        //    project per-node).
        var afterPerNode = new Dictionary<Guid, PropertyEdit>();
        foreach (var nodeId in nodeTargets.Keys)
        {
            afterPerNode[nodeId] = edit.Clone();
        }

        var after = new PropertySnapshot(afterPerNode);

        var op = new PropertyOp(
            Nodes: [.. nodeTargets.Keys],
            Before: before,
            After: after,
            Label: label);

        if (op.EffectiveEdit().Count == 0)
        {
            return SceneCommandResult.Success;
        }

        // 5. Apply After (model + engine) via the schema-layer apply
        //    function. Same function will be called for undo with
        //    ApplySide.Before.
        var resolver = new TransformPropertyTarget(this, context, sceneNodes);
        await PropertyApply.ApplyAsync(op, ApplySide.After, resolver, descriptors).ConfigureAwait(true);

        // 6. Register undo / redo as a single coalesced TimeMachine entry.
        RegisterPropertyOpHistory(context, op, resolver, descriptors);

        await this.MarkDirtyAsync(context).ConfigureAwait(true);
        return SceneCommandResult.Success;
    }

    /// <summary>
    /// Builds a <see cref="PropertyEdit"/> for transform from the
    /// existing <see cref="TransformEdit"/> record. Used by the
    /// <c>EditTransformAsync</c> adapter path.
    /// </summary>
    /// <param name="edit">The legacy edit record.</param>
    /// <returns>The translated property edit. Empty fields are omitted.</returns>
    internal static PropertyEdit BuildPropertyEditFromTransformEdit(TransformEdit edit)
    {
        ArgumentNullException.ThrowIfNull(edit);
        var result = new PropertyEdit();

        // Per-axis entries take precedence over the vector composites
        // because that's the contract callers already rely on.
        if (edit.PositionX.HasValue)
        {
            result.Set(Transform.PositionX, edit.PositionX.Value);
        }

        if (edit.PositionY.HasValue)
        {
            result.Set(Transform.PositionY, edit.PositionY.Value);
        }

        if (edit.PositionZ.HasValue)
        {
            result.Set(Transform.PositionZ, edit.PositionZ.Value);
        }

        if (edit.RotationXDegrees.HasValue)
        {
            result.Set(Transform.RotationX, edit.RotationXDegrees.Value);
        }

        if (edit.RotationYDegrees.HasValue)
        {
            result.Set(Transform.RotationY, edit.RotationYDegrees.Value);
        }

        if (edit.RotationZDegrees.HasValue)
        {
            result.Set(Transform.RotationZ, edit.RotationZDegrees.Value);
        }

        if (edit.ScaleX.HasValue)
        {
            result.Set(Transform.ScaleX, edit.ScaleX.Value);
        }

        if (edit.ScaleY.HasValue)
        {
            result.Set(Transform.ScaleY, edit.ScaleY.Value);
        }

        if (edit.ScaleZ.HasValue)
        {
            result.Set(Transform.ScaleZ, edit.ScaleZ.Value);
        }

        if (edit.Position.HasValue)
        {
            var v = edit.Position.Value;
            result.Set(Transform.PositionX, v.X);
            result.Set(Transform.PositionY, v.Y);
            result.Set(Transform.PositionZ, v.Z);
        }

        if (edit.RotationEulerDegrees.HasValue)
        {
            var v = edit.RotationEulerDegrees.Value;
            result.Set(Transform.RotationX, v.X);
            result.Set(Transform.RotationY, v.Y);
            result.Set(Transform.RotationZ, v.Z);
        }

        if (edit.Scale.HasValue)
        {
            var v = edit.Scale.Value;
            result.Set(Transform.ScaleX, v.X);
            result.Set(Transform.ScaleY, v.Y);
            result.Set(Transform.ScaleZ, v.Z);
        }

        return result;
    }

    /// <summary>
    /// Converts descriptor-addressed transform edits into the compact engine property wire format.
    /// </summary>
    /// <param name="edit">The descriptor-addressed property edit map.</param>
    /// <returns>The engine property entries that can be sent to the runtime.</returns>
    internal static List<EnginePropertyValueEntry> BuildTransformPropertyEntries(PropertyEdit edit)
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

    private void RegisterPropertyOpHistory(
        SceneDocumentCommandContext context,
        PropertyOp op,
        IPropertyTarget resolver,
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
        IPropertyTarget resolver,
        IReadOnlyDictionary<PropertyId, PropertyDescriptor> descriptors,
        ApplySide applySide,
        string label,
        ApplySide inverseSide,
        string inverseLabel)
    {
        // The label "Restore Transform" is reused so the existing tests
        // that observe TimeMachine labels keep passing.
        context.History.AddChange(
            label,
            async () =>
            {
                await PropertyApply.ApplyAsync(op, applySide, resolver, descriptors).ConfigureAwait(true);
                this.RegisterPropertyOpHistory(
                    context,
                    op,
                    resolver,
                    descriptors,
                    inverseSide,
                    inverseLabel,
                    applySide,
                    label);
                await this.MarkDirtyAsync(context).ConfigureAwait(true);
            });
    }

    /// <summary>
    /// Resolves transform-component property ids to model targets and
    /// pushes engine commands via <see cref="ISceneEngineSync"/>.
    /// </summary>
    private sealed class TransformPropertyTarget : IPropertyTarget
    {
        private readonly SceneDocumentCommandService owner;
        private readonly SceneDocumentCommandContext context;
        private readonly IReadOnlyDictionary<Guid, SceneNode> nodes;

        public TransformPropertyTarget(
            SceneDocumentCommandService owner,
            SceneDocumentCommandContext context,
            IReadOnlyDictionary<Guid, SceneNode> nodes)
        {
            this.owner = owner;
            this.context = context;
            this.nodes = nodes;
        }

        public bool TryGetTarget(Guid nodeId, out object? target)
        {
            if (this.nodes.TryGetValue(nodeId, out var node))
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
            // engineCommandKey strings into stable EnginePropertyKey wire
            // ids and dispatch through the new generic SetProperties
            // transport. This replaces the wide UpdateNodeTransformAsync
            // path for property-pipeline edits.
            if (!this.nodes.TryGetValue(nodeId, out var node))
            {
                return Task.CompletedTask;
            }

            var entries = BuildTransformPropertyEntries(edit);
            if (entries.Count == 0)
            {
                return Task.CompletedTask;
            }

            return PushAndPublishAsync(this.owner, this.context, node, entries);
        }

        private static async Task PushAndPublishAsync(
            SceneDocumentCommandService owner,
            SceneDocumentCommandContext context,
            SceneNode node,
            IReadOnlyList<EnginePropertyValueEntry> entries)
        {
            var outcome = await owner.sceneEngineSync.UpdatePropertiesAsync(
                context.Scene,
                node,
                entries).ConfigureAwait(true);
            _ = await owner.PublishSyncOutcomeAsync(
                context,
                SceneOperationKinds.EditTransform,
                outcome).ConfigureAwait(true);
        }
    }
}

/// <summary>
/// Catalog of property descriptors for <see cref="TransformComponent"/>.
/// </summary>
public sealed class TransformDescriptors
{
    private TransformDescriptors(
        PropertyDescriptor<float> positionX,
        PropertyDescriptor<float> positionY,
        PropertyDescriptor<float> positionZ,
        PropertyDescriptor<float> rotationX,
        PropertyDescriptor<float> rotationY,
        PropertyDescriptor<float> rotationZ,
        PropertyDescriptor<float> scaleX,
        PropertyDescriptor<float> scaleY,
        PropertyDescriptor<float> scaleZ)
    {
        this.PositionX = new PropertyId<float>(positionX.Id);
        this.PositionY = new PropertyId<float>(positionY.Id);
        this.PositionZ = new PropertyId<float>(positionZ.Id);
        this.RotationX = new PropertyId<float>(rotationX.Id);
        this.RotationY = new PropertyId<float>(rotationY.Id);
        this.RotationZ = new PropertyId<float>(rotationZ.Id);
        this.ScaleX = new PropertyId<float>(scaleX.Id);
        this.ScaleY = new PropertyId<float>(scaleY.Id);
        this.ScaleZ = new PropertyId<float>(scaleZ.Id);
        this.PositionXDescriptor = positionX;
        this.PositionYDescriptor = positionY;
        this.PositionZDescriptor = positionZ;
        this.RotationXDescriptor = rotationX;
        this.RotationYDescriptor = rotationY;
        this.RotationZDescriptor = rotationZ;
        this.ScaleXDescriptor = scaleX;
        this.ScaleYDescriptor = scaleY;
        this.ScaleZDescriptor = scaleZ;

        var byId = new Dictionary<PropertyId, PropertyDescriptor>
        {
            [positionX.Id] = positionX,
            [positionY.Id] = positionY,
            [positionZ.Id] = positionZ,
            [rotationX.Id] = rotationX,
            [rotationY.Id] = rotationY,
            [rotationZ.Id] = rotationZ,
            [scaleX.Id] = scaleX,
            [scaleY.Id] = scaleY,
            [scaleZ.Id] = scaleZ,
        };
        this.ById = byId;
    }

    /// <summary>Gets the typed id for /local_position/0.</summary>
    public PropertyId<float> PositionX { get; }

    /// <summary>Gets the typed id for /local_position/1.</summary>
    public PropertyId<float> PositionY { get; }

    /// <summary>Gets the typed id for /local_position/2.</summary>
    public PropertyId<float> PositionZ { get; }

    /// <summary>Gets the typed id for /local_rotation_euler_degrees/0.</summary>
    public PropertyId<float> RotationX { get; }

    /// <summary>Gets the typed id for /local_rotation_euler_degrees/1.</summary>
    public PropertyId<float> RotationY { get; }

    /// <summary>Gets the typed id for /local_rotation_euler_degrees/2.</summary>
    public PropertyId<float> RotationZ { get; }

    /// <summary>Gets the typed id for /local_scale/0.</summary>
    public PropertyId<float> ScaleX { get; }

    /// <summary>Gets the typed id for /local_scale/1.</summary>
    public PropertyId<float> ScaleY { get; }

    /// <summary>Gets the typed id for /local_scale/2.</summary>
    public PropertyId<float> ScaleZ { get; }

    /// <summary>Gets the position X descriptor.</summary>
    public PropertyDescriptor<float> PositionXDescriptor { get; }

    /// <summary>Gets the position Y descriptor.</summary>
    public PropertyDescriptor<float> PositionYDescriptor { get; }

    /// <summary>Gets the position Z descriptor.</summary>
    public PropertyDescriptor<float> PositionZDescriptor { get; }

    /// <summary>Gets the rotation X descriptor (Euler degrees).</summary>
    public PropertyDescriptor<float> RotationXDescriptor { get; }

    /// <summary>Gets the rotation Y descriptor (Euler degrees).</summary>
    public PropertyDescriptor<float> RotationYDescriptor { get; }

    /// <summary>Gets the rotation Z descriptor (Euler degrees).</summary>
    public PropertyDescriptor<float> RotationZDescriptor { get; }

    /// <summary>Gets the scale X descriptor.</summary>
    public PropertyDescriptor<float> ScaleXDescriptor { get; }

    /// <summary>Gets the scale Y descriptor.</summary>
    public PropertyDescriptor<float> ScaleYDescriptor { get; }

    /// <summary>Gets the scale Z descriptor.</summary>
    public PropertyDescriptor<float> ScaleZDescriptor { get; }

    /// <summary>Gets the descriptor table indexed by <see cref="PropertyId"/>.</summary>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> ById { get; }

    /// <summary>
    /// Builds the canonical descriptor catalog. Validators reject NaN /
    /// infinite values; scale axes additionally reject zero (degenerate
    /// transforms).
    /// </summary>
    /// <returns>The catalog.</returns>
    public static TransformDescriptors Build()
    {
        var annotation = new EditorAnnotation { Group = "Transform" };

        return new TransformDescriptors(
            positionX: BuildPositionAxis("/local_position/0", static t => t.LocalPosition.X, static (t, v) => t.LocalPosition = new Vector3(v, t.LocalPosition.Y, t.LocalPosition.Z), "transform.position.x"),
            positionY: BuildPositionAxis("/local_position/1", static t => t.LocalPosition.Y, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, v, t.LocalPosition.Z), "transform.position.y"),
            positionZ: BuildPositionAxis("/local_position/2", static t => t.LocalPosition.Z, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, t.LocalPosition.Y, v), "transform.position.z"),
            rotationX: BuildRotationAxis("/local_rotation_euler_degrees/0", 0, "transform.rotation.x"),
            rotationY: BuildRotationAxis("/local_rotation_euler_degrees/1", 1, "transform.rotation.y"),
            rotationZ: BuildRotationAxis("/local_rotation_euler_degrees/2", 2, "transform.rotation.z"),
            scaleX: BuildScaleAxis("/local_scale/0", static t => t.LocalScale.X, static (t, v) => t.LocalScale = new Vector3(v, t.LocalScale.Y, t.LocalScale.Z), "transform.scale.x"),
            scaleY: BuildScaleAxis("/local_scale/1", static t => t.LocalScale.Y, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, v, t.LocalScale.Z), "transform.scale.y"),
            scaleZ: BuildScaleAxis("/local_scale/2", static t => t.LocalScale.Z, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, t.LocalScale.Y, v), "transform.scale.z"));

        static PropertyDescriptor<float> BuildPositionAxis(string pointer, Func<TransformComponent, float> read, Action<TransformComponent, float> write, string key)
        {
            return new PropertyDescriptor<float>(
                id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
                reader: t => read((TransformComponent)t),
                writer: (t, v) => write((TransformComponent)t, v),
                validator: static v => float.IsFinite(v) ? ValidationResult.Ok : ValidationResult.Fail("PROPERTY_NONFINITE", "Position must be finite."),
                annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 },
                engineCommandKey: key);
        }

        static PropertyDescriptor<float> BuildRotationAxis(string pointer, int axisIndex, string key)
        {
            return new PropertyDescriptor<float>(
                id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
                reader: t =>
                {
                    var euler = TransformConverter.QuaternionToEulerDegrees(((TransformComponent)t).LocalRotation);
                    return axisIndex switch
                    {
                        0 => euler.X,
                        1 => euler.Y,
                        _ => euler.Z,
                    };
                },
                writer: (t, v) =>
                {
                    var component = (TransformComponent)t;
                    var euler = TransformConverter.QuaternionToEulerDegrees(component.LocalRotation);
                    var updated = axisIndex switch
                    {
                        0 => new Vector3(v, euler.Y, euler.Z),
                        1 => new Vector3(euler.X, v, euler.Z),
                        _ => new Vector3(euler.X, euler.Y, v),
                    };
                    component.LocalRotation = TransformConverter.EulerDegreesToQuaternion(updated);
                },
                validator: static v => float.IsFinite(v) ? ValidationResult.Ok : ValidationResult.Fail("PROPERTY_NONFINITE", "Rotation must be finite."),
                annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.5 },
                engineCommandKey: key);
        }

        static PropertyDescriptor<float> BuildScaleAxis(string pointer, Func<TransformComponent, float> read, Action<TransformComponent, float> write, string key)
        {
            return new PropertyDescriptor<float>(
                id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
                reader: t => read((TransformComponent)t),
                writer: (t, v) => write((TransformComponent)t, v),
                validator: static v => !float.IsFinite(v)
                    ? ValidationResult.Fail("PROPERTY_NONFINITE", "Scale must be finite.")
                    : v == 0f
                        ? ValidationResult.Fail("PROPERTY_DEGENERATE_SCALE", "Scale axis must be non-zero.")
                        : ValidationResult.Ok,
                annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 },
                engineCommandKey: key);
        }
    }
}
