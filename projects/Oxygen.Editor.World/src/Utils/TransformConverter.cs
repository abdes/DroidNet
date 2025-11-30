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
}
