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
    public static (Vector3 position, Quaternion rotation, Vector3 scale) ToNative(Transform transform)
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
    ///     Converts a quaternion to Euler angles (degrees) using the YXZ (Yaw-Pitch-Roll) rotation
    ///     order.
    ///     <para>
    ///     <b>Rotation Order:</b> Yaw (Y) -> Pitch (X) -> Roll (Z).</para>
    /// </summary>
    /// <remarks>
    ///     <b>Compatibility:</b> This method is the inverse of <see
    ///     cref="Quaternion.CreateFromYawPitchRoll"/> and is compatible with GLM's YXZ Euler angle
    ///     conventions (often used in FPS cameras and game engines).
    ///     <para>
    ///     <b>Human-Friendly Optimization:</b> While the standard mathematical extraction restricts
    ///     Pitch (X) to [-90, 90], this method applies a heuristic to "unwrap" the angles if it
    ///     results in a cleaner representation. For example, a pitch of 100 degrees is returned as
    ///     (100, 0, 0) rather than the mathematically equivalent but confusing (80, 180,
    ///     180).</para>
    /// </remarks>
    /// <param name="q">The quaternion to convert.</param>
    /// <returns>A <see cref="Vector3"/> containing Euler angles in degrees (X = pitch, Y = yaw, Z =
    /// roll).</returns>
    public static Vector3 QuaternionToEulerDegrees(Quaternion q)
    {
        // Normalize first to ensure valid range for asin
        if (q.LengthSquared() < 1e-6f)
        {
            return Vector3.Zero;
        }

        q = Quaternion.Normalize(q);

        var test = 2f * ((q.W * q.X) - (q.Y * q.Z));
        test = Math.Clamp(test, -1f, 1f); // Handle float precision errors

        // Compute the canonical Euler angles (pitch restricted to [-90, 90])
        var (pitchDeg, yawDeg, rollDeg) = MathF.Abs(test) >= 0.99999f
            ? HandleGimbalLock(q, test)
            : ComputeNormalAngles(q, test);

        // Compute alternative representation (pitch beyond [-90, 90])
        var (pitchAlt, yawAlt, rollAlt) = ComputeAlternativeRepresentation(pitchDeg, yawDeg, rollDeg);

        // Select the representation with cleaner yaw/roll values
        return SelectBestRepresentation(pitchDeg, yawDeg, rollDeg, pitchAlt, yawAlt, rollAlt);

        // Local helper: Handle gimbal lock case (pitch = +/- 90 degrees)
        static (float pitchDeg, float yawDeg, float rollDeg) HandleGimbalLock(Quaternion q, float test)
        {
            const float RadToDeg = 180f / MathF.PI;
            var pitch = MathF.CopySign(MathF.PI / 2f, test);

            // In YXZ order at gimbal lock:
            // If Pitch = 90, R00 = cos(y-z), R01 = sin(y-z) => Yaw - Roll = Atan2(R01, R00)
            // If Pitch = -90, R00 = cos(y+z), R01 = -sin(y+z) => Yaw + Roll = Atan2(-R01, R00)
            var r01 = 2f * ((q.X * q.Y) - (q.W * q.Z));
            var r00 = 1f - (2f * ((q.Y * q.Y) + (q.Z * q.Z)));

            // Set Roll = 0 to resolve the ambiguity
            var yaw = test > 0 ? MathF.Atan2(r01, r00) : MathF.Atan2(-r01, r00);
            const float roll = 0f;

            return (pitch * RadToDeg, yaw * RadToDeg, roll * RadToDeg);
        }

        // Local helper: Compute angles for normal case (no gimbal lock)
        static (float pitchDeg, float yawDeg, float rollDeg) ComputeNormalAngles(Quaternion q, float test)
        {
            const float RadToDeg = 180f / MathF.PI;

            var pitch = MathF.Asin(test);
            var yaw = MathF.Atan2(2f * ((q.W * q.Y) + (q.X * q.Z)), 1f - (2f * ((q.X * q.X) + (q.Y * q.Y))));
            var roll = MathF.Atan2(2f * ((q.W * q.Z) + (q.X * q.Y)), 1f - (2f * ((q.X * q.X) + (q.Z * q.Z))));

            return (pitch * RadToDeg, yaw * RadToDeg, roll * RadToDeg);
        }

        // Local helper: Compute alternative representation with pitch beyond [-90, 90]
        static (float pitchAlt, float yawAlt, float rollAlt) ComputeAlternativeRepresentation(
            float pitchDeg,
            float yawDeg,
            float rollDeg)
        {
            /*
             ## Optimization for human readability:

             The standard YXZ extraction restricts Pitch (X) to [-90, 90]. However, humans often prefer to see Pitch go
             beyond 90 (e.g. 100) instead of seeing Yaw/Roll flip 180 degrees. We check if the "alternative" representation
             (Pitch > 90 or < -90) results in simpler Yaw/Roll values.
            */

            var pitchAlt = pitchDeg >= 0 ? 180f - pitchDeg : -180f - pitchDeg;
            var yawAlt = NormalizeAngle(yawDeg + 180f);
            var rollAlt = NormalizeAngle(rollDeg + 180f);

            return (pitchAlt, yawAlt, rollAlt);
        }

        // Local helper: Select the representation with the lowest cost (cleaner yaw/roll)
        static Vector3 SelectBestRepresentation(
            float pitchDeg,
            float yawDeg,
            float rollDeg,
            float pitchAlt,
            float yawAlt,
            float rollAlt)
        {
            // Cost function: sum of absolute Yaw and Roll. Smaller is "cleaner".
            var costCanonical = MathF.Abs(yawDeg) + MathF.Abs(rollDeg);
            var costAlt = MathF.Abs(yawAlt) + MathF.Abs(rollAlt);

            // To avoid jitter when rotating near the ±90° boundary, we need stronger preference.
            // If the alternative representation has significantly cleaner yaw/roll (threshold: 10°),
            // prefer it. Additionally, if the alternative pitch is smaller in absolute value,
            // that's a strong indicator we should use it.
            const float costThreshold = 10f; // Increased from 0.1f to provide stronger hysteresis

            // If alternative pitch magnitude is smaller AND cost is better (or within threshold),
            // prefer alternative (this handles cases like (100,0,0) vs (80,180,180))
            if (MathF.Abs(pitchAlt) < MathF.Abs(pitchDeg) && costAlt <= costCanonical + costThreshold)
            {
                return new Vector3(pitchAlt, yawAlt, rollAlt);
            }

            // Otherwise, use cost-based selection with hysteresis threshold
            return costAlt < costCanonical - costThreshold
                ? new Vector3(pitchAlt, yawAlt, rollAlt)
                : new Vector3(pitchDeg, yawDeg, rollDeg);
        }
    }

    /// <summary>
    ///     Converts Euler angles (degrees) to a quaternion using the YXZ (Yaw-Pitch-Roll) rotation order.
    ///     <para>
    ///     <b>Rotation Order:</b> Yaw (Y) -> Pitch (X) -> Roll (Z).</para>
    /// </summary>
    /// <remarks>
    ///     <b>Compatibility:</b> This wraps <see cref="Quaternion.CreateFromYawPitchRoll"/> and ensures that the
    ///     resulting quaternion matches the rotation produced by GLM when using the same YXZ convention.
    /// </remarks>
    /// <param name="eulerDegrees">Euler angles in degrees (X = pitch, Y = yaw, Z = roll).</param>
    /// <returns>A <see cref="Quaternion"/> representing the rotation.</returns>
    public static Quaternion EulerDegreesToQuaternion(Vector3 eulerDegrees)
    {
        const float DegToRad = MathF.PI / 180f;

        // Editor convention: X=pitch, Y=yaw, Z=roll
        // Quaternion.CreateFromYawPitchRoll expects (yaw, pitch, roll)
        return Quaternion.CreateFromYawPitchRoll(
            eulerDegrees.Y * DegToRad, // yaw
            eulerDegrees.X * DegToRad, // pitch
            eulerDegrees.Z * DegToRad); // roll
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
