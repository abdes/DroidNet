// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Utils;

/// <summary>
///     Provides conversion utilities for <see cref="Transform"/> components to native engine types.
/// </summary>
public static class TransformConverter
{
    /// <summary>
    ///     Converts a managed <see cref="Transform"/> to native Vector3/Quaternion/Vector3 tuple.
    /// </summary>
    /// <param name="transform">The transform to convert.</param>
    /// <returns>A tuple containing position, rotation, and scale as native types.</returns>
    public static (Vector3 Position, Quaternion Rotation, Vector3 Scale) ToNative(Transform transform)
    {
        ArgumentNullException.ThrowIfNull(transform);

        return (
            new Vector3(transform.LocalPosition.X, transform.LocalPosition.Y, transform.LocalPosition.Z),
            new Quaternion(
                transform.LocalRotation.X,
                transform.LocalRotation.Y,
                transform.LocalRotation.Z,
                transform.LocalRotation.W),
            new Vector3(transform.LocalScale.X, transform.LocalScale.Y, transform.LocalScale.Z));
    }

    /// <summary>
    ///     Extracts position as a native Vector3.
    /// </summary>
    /// <param name="transform">The transform to extract from.</param>
    /// <returns>Position as Vector3.</returns>
    public static Vector3 GetPosition(Transform transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        return new Vector3(transform.LocalPosition.X, transform.LocalPosition.Y, transform.LocalPosition.Z);
    }

    /// <summary>
    ///     Extracts rotation as a native Quaternion.
    /// </summary>
    /// <param name="transform">The transform to extract from.</param>
    /// <returns>Rotation as Quaternion.</returns>
    public static Quaternion GetRotation(Transform transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        return new Quaternion(
            transform.LocalRotation.X,
            transform.LocalRotation.Y,
            transform.LocalRotation.Z,
            transform.LocalRotation.W);
    }

    /// <summary>
    ///     Extracts scale as a native Vector3.
    /// </summary>
    /// <param name="transform">The transform to extract from.</param>
    /// <returns>Scale as Vector3.</returns>
    public static Vector3 GetScale(Transform transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        return new Vector3(transform.LocalScale.X, transform.LocalScale.Y, transform.LocalScale.Z);
    }

    /// <summary>
    /// Convert a quaternion to Euler angles (degrees) using the yaw-pitch-roll convention
    /// compatible with <see cref="Quaternion.CreateFromYawPitchRoll"/>.
    /// Euler ordering: X = pitch, Y = yaw, Z = roll.
    /// </summary>
    public static Vector3 QuaternionToEulerDegrees(Quaternion q)
    {
        // Normalize first
        if (q.LengthSquared() <= 0f) q = Quaternion.Identity;
        q = Quaternion.Normalize(q);

        var sinp = 2f * (q.W * q.X - q.Y * q.Z);
        float pitch;
        if (MathF.Abs(sinp) >= 1f)
        {
            pitch = MathF.CopySign(MathF.PI / 2f, sinp);
        }
        else
        {
            pitch = MathF.Asin(sinp);
        }

        var yaw = MathF.Atan2(2f * (q.W * q.Y + q.Z * q.X), 1f - 2f * (q.X * q.X + q.Y * q.Y));

        var roll = MathF.Atan2(2f * (q.W * q.Z + q.X * q.Y), 1f - 2f * (q.Y * q.Y + q.Z * q.Z));

        const float RadToDeg = 180f / MathF.PI;
        return new Vector3(pitch * RadToDeg, yaw * RadToDeg, roll * RadToDeg);
    }

    /// <summary>
    /// Convert Euler angles (degrees) to a quaternion using the yaw-pitch-roll convention.
    /// Euler ordering: X = pitch, Y = yaw, Z = roll.
    /// </summary>
    public static Quaternion EulerDegreesToQuaternion(Vector3 eulerDegrees)
    {
        const float DegToRad = MathF.PI / 180f;
        var pitch = eulerDegrees.X * DegToRad;
        var yaw = eulerDegrees.Y * DegToRad;
        var roll = eulerDegrees.Z * DegToRad;

        return Quaternion.CreateFromYawPitchRoll(yaw, pitch, roll);
    }
}
