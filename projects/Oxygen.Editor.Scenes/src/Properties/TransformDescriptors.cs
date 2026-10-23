// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

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
    /// Builds the canonical descriptor catalog. Validators take the scene schema's ranges: every value is finite and a
    /// scale axis is not zero.
    /// </summary>
    /// <returns>The catalog.</returns>
    public static TransformDescriptors Build()
        => new(
            positionX: BuildPositionAxis(SceneSchema.Transform.PositionX, static t => t.LocalPosition.X, static (t, v) => t.LocalPosition = new Vector3(v, t.LocalPosition.Y, t.LocalPosition.Z)),
            positionY: BuildPositionAxis(SceneSchema.Transform.PositionY, static t => t.LocalPosition.Y, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, v, t.LocalPosition.Z)),
            positionZ: BuildPositionAxis(SceneSchema.Transform.PositionZ, static t => t.LocalPosition.Z, static (t, v) => t.LocalPosition = new Vector3(t.LocalPosition.X, t.LocalPosition.Y, v)),
            rotationX: BuildRotationAxis(SceneSchema.Transform.RotationX, 0),
            rotationY: BuildRotationAxis(SceneSchema.Transform.RotationY, 1),
            rotationZ: BuildRotationAxis(SceneSchema.Transform.RotationZ, 2),
            scaleX: BuildScaleAxis(SceneSchema.Transform.ScaleX, static t => t.LocalScale.X, static (t, v) => t.LocalScale = new Vector3(v, t.LocalScale.Y, t.LocalScale.Z)),
            scaleY: BuildScaleAxis(SceneSchema.Transform.ScaleY, static t => t.LocalScale.Y, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, v, t.LocalScale.Z)),
            scaleZ: BuildScaleAxis(SceneSchema.Transform.ScaleZ, static t => t.LocalScale.Z, static (t, v) => t.LocalScale = new Vector3(t.LocalScale.X, t.LocalScale.Y, v)));

    private static PropertyDescriptor<float> BuildPositionAxis(
        PropertyId<float> id,
        Func<TransformComponent, float> read,
        Action<TransformComponent, float> write)
        => new(
            id: id,
            reader: target => read((TransformComponent)target),
            writer: (target, value) => write((TransformComponent)target, value),
            validator: PropertyValidators.Of(id, "Position"),
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 });

    private static PropertyDescriptor<float> BuildRotationAxis(PropertyId<float> id, int axisIndex)
        => new(
            id: id,
            reader: target => ReadRotationAxis((TransformComponent)target, axisIndex),
            writer: (target, value) => WriteRotationAxis((TransformComponent)target, axisIndex, value),
            validator: PropertyValidators.Of(id, "Rotation"),
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.5 });

    private static PropertyDescriptor<float> BuildScaleAxis(
        PropertyId<float> id,
        Func<TransformComponent, float> read,
        Action<TransformComponent, float> write)
        => new(
            id: id,
            reader: target => read((TransformComponent)target),
            writer: (target, value) => write((TransformComponent)target, value),
            validator: PropertyValidators.Of(id, "Scale"),
            annotation: new EditorAnnotation { Group = "Transform", Renderer = "vector3-box", Step = 0.01 });

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
