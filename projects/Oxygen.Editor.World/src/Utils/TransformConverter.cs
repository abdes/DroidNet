// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Utils;

/// <summary>
///     Provides conversion utilities for <see cref="TransformComponent"/> components to native engine types.
/// </summary>
public static class TransformConverter
{
    /// <summary>
    ///     Converts a managed <see cref="TransformComponent"/> to native Vector3/Quaternion/Vector3 tuple.
    /// </summary>
    /// <param name="transform">The transform to convert.</param>
    /// <returns>A tuple containing position, rotation, and scale as native types.</returns>
    public static (Vector3 Position, Quaternion Rotation, Vector3 Scale) ToNative(TransformComponent transform)
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
    public static Vector3 GetPosition(TransformComponent transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        return new Vector3(transform.LocalPosition.X, transform.LocalPosition.Y, transform.LocalPosition.Z);
    }

    /// <summary>
    ///     Extracts rotation as a native Quaternion.
    /// </summary>
    /// <param name="transform">The transform to extract from.</param>
    /// <returns>Rotation as Quaternion.</returns>
    public static Quaternion GetRotation(TransformComponent transform)
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
    public static Vector3 GetScale(TransformComponent transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        return new Vector3(transform.LocalScale.X, transform.LocalScale.Y, transform.LocalScale.Z);
    }

    /// <summary>
    ///     Converts a quaternion to X-Y-Z Euler angles in degrees: about X, then about Y, then about Z, each about a
    ///     parent axis, so R = Rz * Ry * Rx (the engine convention, see <c>Oxygen/Core/Constants.h</c>).
    /// </summary>
    /// <remarks>
    ///     <b>Compatibility:</b> This method is the inverse of <see cref="EulerDegreesToQuaternion"/> and matches the
    ///     engine's <c>QuaternionToEulerXyzDegrees</c> on the canonical range.
    ///     <para>
    ///     <b>Human-Friendly Optimization:</b> While the standard mathematical extraction restricts the Y angle to
    ///     [-90, 90], this method applies a heuristic to "unwrap" the angles if it results in a cleaner
    ///     representation. For example, a Y angle of 100 degrees is returned as (0, 100, 0) rather than the
    ///     mathematically equivalent but confusing (180, 80, 180).</para>
    /// </remarks>
    /// <param name="q">The quaternion to convert.</param>
    /// <returns>A <see cref="Vector3"/> containing the X, Y and Z Euler angles in degrees.</returns>
    public static Vector3 QuaternionToEulerDegrees(Quaternion q)
    {
        // Normalize first to ensure valid range for asin
        if (q.LengthSquared() < 1e-6f)
        {
            return Vector3.Zero;
        }

        q = Quaternion.Normalize(q);

        // sin(Y) = -R20 of R = Rz * Ry * Rx.
        var test = 2f * ((q.W * q.Y) - (q.X * q.Z));
        test = Math.Clamp(test, -1f, 1f); // Handle float precision errors

        // Compute the canonical Euler angles (Y restricted to [-90, 90])
        var (xDeg, yDeg, zDeg) = MathF.Abs(test) >= 0.99999f
            ? HandleGimbalLock(q, test)
            : ComputeNormalAngles(q, test);

        // Compute alternative representation (Y beyond [-90, 90])
        var (xAlt, yAlt, zAlt) = ComputeAlternativeRepresentation(xDeg, yDeg, zDeg);

        // Select the representation with cleaner X and Z values
        return SelectBestRepresentation(new Vector3(xDeg, yDeg, zDeg), new Vector3(xAlt, yAlt, zAlt));

        // Local helper: Handle gimbal lock case (Y = +/- 90 degrees)
        static (float XDeg, float YDeg, float ZDeg) HandleGimbalLock(Quaternion q, float test)
        {
            const float RadToDeg = 180f / MathF.PI;
            var y = MathF.CopySign(MathF.PI / 2f, test);

            // At Y = +/-90 the X and Z turns share one axis; X = 0 resolves the ambiguity, and then
            // R01 = -sin(Z), R11 = cos(Z).
            var r01 = 2f * ((q.X * q.Y) - (q.W * q.Z));
            var r11 = 1f - (2f * ((q.X * q.X) + (q.Z * q.Z)));
            const float x = 0f;
            var z = MathF.Atan2(-r01, r11);

            return (x * RadToDeg, y * RadToDeg, z * RadToDeg);
        }

        // Local helper: Compute angles for normal case (no gimbal lock)
        static (float XDeg, float YDeg, float ZDeg) ComputeNormalAngles(Quaternion q, float test)
        {
            const float RadToDeg = 180f / MathF.PI;

            var y = MathF.Asin(test);
            var x = MathF.Atan2(2f * ((q.Y * q.Z) + (q.W * q.X)), 1f - (2f * ((q.X * q.X) + (q.Y * q.Y))));
            var z = MathF.Atan2(2f * ((q.X * q.Y) + (q.W * q.Z)), 1f - (2f * ((q.Y * q.Y) + (q.Z * q.Z))));

            return (x * RadToDeg, y * RadToDeg, z * RadToDeg);
        }

        // Local helper: Compute alternative representation with Y beyond [-90, 90]
        static (float XAlt, float YAlt, float ZAlt) ComputeAlternativeRepresentation(
            float xDeg,
            float yDeg,
            float zDeg)
        {
            /*
             ## Optimization for human readability:

             The standard X-Y-Z extraction restricts Y to [-90, 90]. However, humans often prefer to see Y go beyond 90
             (e.g. 100) instead of seeing X and Z flip 180 degrees. We check if the "alternative" representation
             (Y > 90 or < -90) results in simpler X and Z values.
            */

            var yAlt = yDeg >= 0 ? 180f - yDeg : -180f - yDeg;
            var xAlt = NormalizeAngle(xDeg + 180f);
            var zAlt = NormalizeAngle(zDeg + 180f);

            return (xAlt, yAlt, zAlt);
        }

        // Local helper: Select the representation with the lowest cost (cleaner X and Z)
        static Vector3 SelectBestRepresentation(Vector3 canonical, Vector3 alternative)
        {
            // Cost function: sum of absolute X and Z. Smaller is "cleaner".
            var costCanonical = MathF.Abs(canonical.X) + MathF.Abs(canonical.Z);
            var costAlt = MathF.Abs(alternative.X) + MathF.Abs(alternative.Z);

            // To avoid jitter when rotating near the ±90° boundary, we need stronger preference.
            // If the alternative representation has significantly cleaner X and Z (threshold: 10°),
            // prefer it. Additionally, if the alternative Y is smaller in absolute value,
            // that's a strong indicator we should use it.
            const float costThreshold = 10f;

            // If alternative Y magnitude is smaller AND cost is better (or within threshold),
            // prefer alternative (this handles cases like (0,100,0) vs (180,80,180))
            if (MathF.Abs(alternative.Y) < MathF.Abs(canonical.Y) && costAlt <= costCanonical + costThreshold)
            {
                return alternative;
            }

            // Otherwise, use cost-based selection with hysteresis threshold
            return costAlt < costCanonical - costThreshold ? alternative : canonical;
        }
    }

    /// <summary>
    ///     Converts X-Y-Z Euler angles in degrees to a quaternion: about X, then about Y, then about Z, each about a
    ///     parent axis, so R = Rz * Ry * Rx (the engine convention, see <c>Oxygen/Core/Constants.h</c>).
    /// </summary>
    /// <remarks>
    ///     <b>Compatibility:</b> Matches the engine's <c>EulerXyzDegreesToQuaternion</c>.
    /// </remarks>
    /// <param name="eulerDegrees">The X, Y and Z Euler angles in degrees.</param>
    /// <returns>A <see cref="Quaternion"/> representing the rotation.</returns>
    public static Quaternion EulerDegreesToQuaternion(Vector3 eulerDegrees)
    {
        const float DegToRad = MathF.PI / 180f;

        var x = Quaternion.CreateFromAxisAngle(Vector3.UnitX, eulerDegrees.X * DegToRad);
        var y = Quaternion.CreateFromAxisAngle(Vector3.UnitY, eulerDegrees.Y * DegToRad);
        var z = Quaternion.CreateFromAxisAngle(Vector3.UnitZ, eulerDegrees.Z * DegToRad);
        return z * y * x;
    }

    /// <summary>
    ///     Normalizes an angle in degrees into the canonical [-180, 180) range.
    /// </summary>
    /// <param name="deg">The angle in degrees to normalize.</param>
    /// <returns>The normalized angle in degrees within [-180, 180).</returns>
    public static float NormalizeAngle(float deg)
    {
        // bring into [-360,360)
        var r = deg % 360f;

        // map to [-180,180)
        if (r >= 180f)
        {
            r -= 360f;
        }

        if (r < -180f)
        {
            r += 360f;
        }

        return r;
    }

    /// <summary>
    ///     Normalizes each component of an Euler degrees vector into [-180, 180).
    /// </summary>
    /// <param name="e">The Euler angles vector to normalize.</param>
    /// <returns>A <see cref="Vector3"/> with each component normalized to [-180, 180).</returns>
    public static Vector3 NormalizeEulerDegrees(Vector3 e)
        => new(NormalizeAngle(e.X), NormalizeAngle(e.Y), NormalizeAngle(e.Z));

    /// <summary>
    ///     Normalizes a scale value so exact zero becomes the supplied epsilon. Preserves sign for
    ///     non-zero values.
    /// </summary>
    /// <param name="value">The scale value to normalize.</param>
    /// <param name="epsilon">The minimum allowed magnitude for scale (default: 1e-3).</param>
    /// <returns>The normalized scale value.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "clearer like this")]
    public static float NormalizeScaleValue(float value, float epsilon = 1e-3f)
    {
        if (float.IsNaN(value) || float.IsInfinity(value))
        {
            return value; // caller decides how to handle invalid numbers
        }

        // Replace exact zero with epsilon (preserving sign for very small values isn't necessary for zero)
        if (value == 0f)
        {
            return epsilon;
        }

        // If value is non-zero but too small in magnitude, bring it up to epsilon with sign preserved
        if (MathF.Abs(value) < epsilon)
        {
            return MathF.CopySign(epsilon, value);
        }

        return value;
    }

    /// <summary>
    ///     Normalizes a <see cref="Vector3"/> scale by applying <see cref="NormalizeScaleValue"/>
    ///     to each component.
    /// </summary>
    /// <param name="v">The scale vector to normalize.</param>
    /// <param name="epsilon">The minimum allowed magnitude for each component (default:
    /// 1e-3).</param>
    /// <returns>The normalized <see cref="Vector3"/> scale.</returns>
    public static Vector3 NormalizeScaleVector(Vector3 v, float epsilon = 1e-3f)
        => new(NormalizeScaleValue(v.X, epsilon), NormalizeScaleValue(v.Y, epsilon), NormalizeScaleValue(v.Z, epsilon));
}
