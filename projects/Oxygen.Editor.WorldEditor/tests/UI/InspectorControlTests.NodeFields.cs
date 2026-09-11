// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Tests;

/// <summary>Declares component field edits and their engine-unit expectations.</summary>
public sealed partial class InspectorControlTests
{
    private static readonly NodeFieldCase[] NativeNodeFields =
    [
        new("Transform", "PositionX", 1, 0, 12f, 12f),
        new("Transform", "PositionY", 1, 1, 12f, 12f),
        new("Transform", "PositionZ", 1, 2, 12f, 12f),
        new("Transform", "RotationX", 1, 3, 30f, 30f),
        new("Transform", "RotationY", 1, 4, 30f, 30f),
        new("Transform", "RotationZ", 1, 5, 30f, 30f),
        new("Transform", "ScaleX", 1, 6, 2f, 2f),
        new("Transform", "ScaleY", 1, 7, 2f, 2f),
        new("Transform", "ScaleZ", 1, 8, 2f, 2f),
        new("Camera", "FieldOfView", 2, 0, 75f, 75f * MathF.PI / 180f),
        new("Camera", "AspectRatio", 2, 1, 1.5f, 1.5f),
        new("Camera", "NearPlane", 2, 2, 0.5f, 0.5f),
        new("Camera", "FarPlane", 2, 3, 2000f, 2000f),
        new("Light", "ColorR", 3, 0, 0.2f, 0.2f),
        new("Light", "ColorG", 3, 1, 0.3f, 0.3f),
        new("Light", "ColorB", 3, 2, 0.4f, 0.4f),
        new("Light", "AffectsWorld", 3, 3, ControlValue: false, 0),
        new("Light", "Mobility", 3, 4, LightMobility.Mixed, 1),
        new("Light", "CastsShadows", 3, 5, ControlValue: false, 0),
        new("Light", "ShadowBias", 3, 6, 0.01f, 0.01f),
        new("Light", "ShadowNormalBias", 3, 7, 0.04f, 0.04f),
        new("Light", "ContactShadows", 3, 8, ControlValue: true, 1),
        new("Light", "ShadowResolutionHint", 3, 9, ShadowResolutionHint.High, 2),
        new("Light", "ExposureCompensation", 3, 10, 1.5f, 1.5f),
        new("Light", "IntensityLux", 3, 11, 80000f, 80000f),
        new("Light", "AngularSizeRadians", 3, 12, 0.02f, 0.02f),
        new("Light", "EnvironmentContribution", 3, 13, ControlValue: false, 0),
        new("Light", "IsSunLight", 3, 14, ControlValue: false, 0),
        new("Light", "CascadeCount", 3, 15, 3f, 3),
        new("Light", "SplitMode", 3, 16, DirectionalCsmSplitMode.ManualDistances, 1),
        new("Light", "MaxShadowDistance", 3, 17, 200f, 200),
        new("Light", "CascadeDistance1", 3, 18, 12f, 12),
        new("Light", "CascadeDistance2", 3, 19, 32f, 32),
        new("Light", "CascadeDistance3", 3, 20, 90f, 90),
        new("Light", "CascadeDistance4", 3, 21, 220f, 220),
        new("Light", "DistributionExponent", 3, 22, 2f, 2),
        new("Light", "TransitionFraction", 3, 23, 0.2f, 0.2f),
        new("Light", "DistanceFadeoutFraction", 3, 24, 0.3f, 0.3f),
    ];

    private static Dictionary<(ushort component, ushort field), float> SourceNodeProperties(SceneNode node)
    {
        var result = new Dictionary<(ushort component, ushort field), float>();
        var transform = node.Components.OfType<TransformComponent>().Single();
        var position = transform.LocalPosition;
        var rotation = TransformConverter.QuaternionToEulerDegrees(transform.LocalRotation);
        var scale = transform.LocalScale;
        Add(1, [position.X, position.Y, position.Z, rotation.X, rotation.Y, rotation.Z, scale.X, scale.Y, scale.Z]);
        if (node.Components.OfType<PerspectiveCamera>().FirstOrDefault() is { } camera)
        {
            Add(2, [camera.FieldOfView * MathF.PI / 180f, camera.AspectRatio, camera.NearPlane, camera.FarPlane]);
        }

        if (node.Components.OfType<DirectionalLightComponent>().FirstOrDefault() is { } light)
        {
            Add(
                3,
                [
                light.Color.X, light.Color.Y, light.Color.Z,
                light.AffectsWorld ? 1f : 0f, (int)light.Mobility, light.CastsShadows ? 1f : 0f,
                light.ShadowBias, light.ShadowNormalBias, light.ContactShadows ? 1f : 0f, (int)light.ShadowResolutionHint,
                light.ExposureCompensation, light.IntensityLux, light.AngularSizeRadians,
                light.EnvironmentContribution ? 1f : 0f, light.IsSunLight ? 1f : 0f,
                light.CascadeCount, (int)light.SplitMode, light.MaxShadowDistance,
                light.CascadeDistances.X, light.CascadeDistances.Y, light.CascadeDistances.Z, light.CascadeDistances.W,
                light.DistributionExponent, light.TransitionFraction, light.DistanceFadeoutFraction,
            ]);
        }

        return result;

        void Add(ushort component, float[] values)
        {
            for (ushort index = 0; index < values.Length; index++)
            {
                result[(component, index)] = values[index];
            }
        }
    }

    private sealed record NodeFieldCase(string Kind, string Field, ushort Component, ushort NativeField, object ControlValue, float ExpectedValue);
}
