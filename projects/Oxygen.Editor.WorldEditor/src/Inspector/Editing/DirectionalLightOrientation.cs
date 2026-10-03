// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Utils;

namespace Oxygen.Editor.World.Inspector.Editing;

/// <summary>Converts Z-up, emitted-forward -Y source direction without owning authoring commands.</summary>
internal static class DirectionalLightOrientation
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;
    private static readonly Vector3 EngineForward = new(0f, -1f, 0f);
    private static readonly Vector3 EngineUp = new(0f, 0f, 1f);

    /// <summary>Projects world direction into the target's authored parent-relative rotation.</summary>
    /// <param name="node">The captured target node.</param>
    /// <param name="azimuthDegrees">The world azimuth.</param>
    /// <param name="elevationDegrees">The world elevation.</param>
    /// <returns>The authored local rotation.</returns>
    internal static Quaternion LocalRotation(SceneNode node, float azimuthDegrees, float elevationDegrees)
    {
        var azimuth = azimuthDegrees * DegToRad;
        var elevation = Math.Clamp(elevationDegrees, -89.9f, 89.9f) * DegToRad;
        var cosElevation = MathF.Cos(elevation);
        var directionToLight = NormalizeOrFallback(new(MathF.Sin(azimuth) * cosElevation, MathF.Cos(azimuth) * cosElevation, MathF.Sin(elevation)), EngineUp);
        var desired = RotationFromForward(-directionToLight);
        return node.Parent is null || node.IgnoreParentTransform
            ? desired : NormalizeOrIdentity(Quaternion.Inverse(WorldRotation(node.Parent)) * desired);
    }

    /// <summary>Reads world direction using the engine's emitted-forward convention.</summary>
    /// <param name="node">The source node.</param>
    /// <returns>The world azimuth and elevation in degrees.</returns>
    internal static (float azimuth, float elevation) DisplayAngles(SceneNode node)
    {
        var emitted = NormalizeOrFallback(Vector3.Transform(EngineForward, WorldRotation(node)), EngineForward);
        var direction = -emitted;
        var horizontalLength = MathF.Sqrt((direction.X * direction.X) + (direction.Y * direction.Y));
        return (TransformConverter.NormalizeAngle(MathF.Atan2(direction.X, direction.Y) * RadToDeg),
            MathF.Atan2(direction.Z, horizontalLength) * RadToDeg);
    }

    private static Quaternion WorldRotation(SceneNode node)
    {
        var local = node.Components.OfType<TransformComponent>().FirstOrDefault()?.LocalRotation ?? Quaternion.Identity;
        return node.Parent is null || node.IgnoreParentTransform ? NormalizeOrIdentity(local)
            : NormalizeOrIdentity(WorldRotation(node.Parent) * local);
    }

    private static Quaternion RotationFromForward(Vector3 targetDirection)
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
        => float.IsFinite(value.LengthSquared()) && value.LengthSquared() > 0.000001f ? Vector3.Normalize(value) : fallback;

    private static Quaternion NormalizeOrIdentity(Quaternion value)
        => float.IsFinite(value.LengthSquared()) && value.LengthSquared() > 0.000001f ? Quaternion.Normalize(value) : Quaternion.Identity;
}
