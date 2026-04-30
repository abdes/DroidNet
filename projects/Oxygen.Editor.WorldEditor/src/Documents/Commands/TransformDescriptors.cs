// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Managed.Core.Diagnostics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

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
        this.ById = new Dictionary<PropertyId, PropertyDescriptor>
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
    /// infinite values; scale axes additionally reject zero.
    /// </summary>
    /// <returns>The catalog.</returns>
    public static TransformDescriptors Build()
        => new(
            positionX: BuildPositionAxis("/local_position/0", static t => t.LocalPosition.X, static (t, v) => t.LocalPosition = new Vector3(v, t.LocalPosition.Y, t.LocalPosition.Z), "transform.position.x"),
            positionY: BuildPositionAxis("/local_position/1", static t => t.LocalPosition.Y, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, v, t.LocalPosition.Z), "transform.position.y"),
            positionZ: BuildPositionAxis("/local_position/2", static t => t.LocalPosition.Z, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, t.LocalPosition.Y, v), "transform.position.z"),
            rotationX: BuildRotationAxis("/local_rotation_euler_degrees/0", 0, "transform.rotation.x"),
            rotationY: BuildRotationAxis("/local_rotation_euler_degrees/1", 1, "transform.rotation.y"),
            rotationZ: BuildRotationAxis("/local_rotation_euler_degrees/2", 2, "transform.rotation.z"),
            scaleX: BuildScaleAxis("/local_scale/0", static t => t.LocalScale.X, static (t, v) => t.LocalScale = new Vector3(v, t.LocalScale.Y, t.LocalScale.Z), "transform.scale.x"),
            scaleY: BuildScaleAxis("/local_scale/1", static t => t.LocalScale.Y, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, v, t.LocalScale.Z), "transform.scale.y"),
            scaleZ: BuildScaleAxis("/local_scale/2", static t => t.LocalScale.Z, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, t.LocalScale.Y, v), "transform.scale.z"));

    private static PropertyDescriptor<float> BuildPositionAxis(
        string pointer,
        Func<TransformComponent, float> read,
        Action<TransformComponent, float> write,
        string key)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
            reader: target => read((TransformComponent)target),
            writer: (target, value) => write((TransformComponent)target, value),
            validator: static value => float.IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail("PROPERTY_NONFINITE", "Position must be finite."),
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 },
            engineCommandKey: key);

    private static PropertyDescriptor<float> BuildRotationAxis(string pointer, int axisIndex, string key)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
            reader: target => ReadRotationAxis((TransformComponent)target, axisIndex),
            writer: (target, value) => WriteRotationAxis((TransformComponent)target, axisIndex, value),
            validator: static value => float.IsFinite(value)
                ? ValidationResult.Ok
                : ValidationResult.Fail("PROPERTY_NONFINITE", "Rotation must be finite."),
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.5 },
            engineCommandKey: key);

    private static PropertyDescriptor<float> BuildScaleAxis(
        string pointer,
        Func<TransformComponent, float> read,
        Action<TransformComponent, float> write,
        string key)
        => new(
            id: new PropertyId<float>(SceneDocumentCommandService.TransformKind, pointer),
            reader: target => read((TransformComponent)target),
            writer: (target, value) => write((TransformComponent)target, value),
            validator: static value => !float.IsFinite(value)
                ? ValidationResult.Fail("PROPERTY_NONFINITE", "Scale must be finite.")
                : value == 0f
                    ? ValidationResult.Fail("PROPERTY_DEGENERATE_SCALE", "Scale axis must be non-zero.")
                    : ValidationResult.Ok,
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 },
            engineCommandKey: key);

    private static float ReadRotationAxis(TransformComponent component, int axisIndex)
    {
        var euler = TransformConverter.QuaternionToEulerDegrees(component.LocalRotation);
        return axisIndex switch
        {
            0 => euler.X,
            1 => euler.Y,
            _ => euler.Z,
        };
    }

    private static void WriteRotationAxis(TransformComponent component, int axisIndex, float value)
    {
        var euler = TransformConverter.QuaternionToEulerDegrees(component.LocalRotation);
        var updated = axisIndex switch
        {
            0 => new Vector3(value, euler.Y, euler.Z),
            1 => new Vector3(euler.X, value, euler.Z),
            _ => new Vector3(euler.X, euler.Y, value),
        };
        component.LocalRotation = TransformConverter.EulerDegreesToQuaternion(updated);
    }
}
