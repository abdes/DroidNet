// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Numerics;
using AwesomeAssertions;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class TransformConverterTests
{
    private const float Tolerance = 1e-3f;

    [TestMethod]
    public void QuaternionToEulerDegrees_IdentityQuaternion_ReturnsZeroAngles()
    {
        var q = Quaternion.Identity;
        var e = Utils.TransformConverter.QuaternionToEulerDegrees(q);
        _ = e.X.Should().BeApproximately(0f, Tolerance);
        _ = e.Y.Should().BeApproximately(0f, Tolerance);
        _ = e.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void NormalizeAngle_OutOfRangeValue_NormalizesToCanonical()
    {
        _ = Utils.TransformConverter.NormalizeAngle(370f).Should().BeApproximately(10f, Tolerance);
        _ = Utils.TransformConverter.NormalizeAngle(190f).Should().BeApproximately(-170f, Tolerance);
        _ = Utils.TransformConverter.NormalizeAngle(360f).Should().BeApproximately(0f, Tolerance);
        _ = Utils.TransformConverter.NormalizeAngle(-370f).Should().BeApproximately(-10f, Tolerance);
    }

    [TestMethod]
    public void NormalizeEuler_OutOfRangeVector_NormalizesEachComponent()
    {
        var e = new Vector3(370f, 190f, -370f);
        var n = Utils.TransformConverter.NormalizeEulerDegrees(e);

        _ = n.X.Should().BeApproximately(10f, Tolerance);
        _ = n.Y.Should().BeApproximately(-170f, Tolerance);
        _ = n.Z.Should().BeApproximately(-10f, Tolerance);
    }

    [TestMethod]
    public void NormalizeScaleValue_ZeroOrSmallMagnitude_ReplacesWithEpsilon()
    {
        const float eps = 1e-3f;
        _ = Utils.TransformConverter.NormalizeScaleValue(0f, eps).Should().BeApproximately(eps, 1e-6f);
        _ = Utils.TransformConverter.NormalizeScaleValue(1e-6f, eps).Should().BeApproximately(eps, 1e-6f);
        _ = Utils.TransformConverter.NormalizeScaleValue(-1e-6f, eps).Should().BeApproximately(-eps, 1e-6f);
        _ = Utils.TransformConverter.NormalizeScaleValue(2.0f, eps).Should().BeApproximately(2.0f, 1e-6f);
    }

    [TestMethod]
    public void NormalizeScaleVector_ZeroOrSmallMagnitude_ReplacesWithEpsilonPerComponent()
    {
        const float eps = 1e-3f;
        var v = new Vector3(0f, -1e-6f, 5.0f);
        var n = Utils.TransformConverter.NormalizeScaleVector(v, eps);

        _ = n.X.Should().BeApproximately(eps, 1e-6f);
        _ = n.Y.Should().BeApproximately(-eps, 1e-6f);
        _ = n.Z.Should().BeApproximately(5.0f, 1e-6f);
    }

    [TestMethod]
    [DataRow(0f, 0f, 0f)]
    [DataRow(10f, 20f, 30f)]
    [DataRow(-10f, -20f, -30f)]
    [DataRow(45f, 45f, 45f)]
    [DataRow(90f, 0f, 0f)] // Pitch +90
    [DataRow(-90f, 0f, 0f)] // Pitch -90
    [DataRow(0f, 90f, 0f)] // Yaw +90
    [DataRow(0f, 0f, 90f)] // Roll +90
    public void QuaternionToEulerDegrees_RoundTrip_PreservesAngles(float x, float y, float z)
    {
        var original = new Vector3(x, y, z);

        // 1. Convert Euler -> Quaternion
        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);

        // 2. Convert Quaternion -> Euler
        var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        // 3. Verify that we get the same angles back (within tolerance)
        // Note: This relies on the "Human Eye" optimization to return the same values for inputs like (100, 0, 0).
        // For standard canonical angles, this should always match.
        _ = result.X.Should().BeApproximately(original.X, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on X for input {original}"));
        _ = result.Y.Should().BeApproximately(original.Y, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on Y for input {original}"));
        _ = result.Z.Should().BeApproximately(original.Z, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on Z for input {original}"));
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_HumanEyeOptimization_PreservesPitchBeyond90()
    {
        // Case: Pitch = 100 degrees.
        // Canonical YXZ would represent this as Pitch=80, Yaw=180, Roll=180.
        // We want the converter to prefer (100, 0, 0) because it's "cleaner" (smaller Yaw/Roll).
        var original = new Vector3(100f, 0f, 0f);
        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(100f, Tolerance);
        _ = result.Y.Should().BeApproximately(0f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_HumanEyeOptimization_PreservesPitchBelowMinus90()
    {
        // Case: Pitch = -100 degrees.
        // Canonical YXZ would represent this as Pitch=-80, Yaw=180, Roll=180.
        // We want the converter to prefer (-100, 0, 0).
        var original = new Vector3(-100f, 0f, 0f);
        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(-100f, Tolerance);
        _ = result.Y.Should().BeApproximately(0f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_GimbalLock_PositivePitch_HandlesCorrectly()
    {
        // Pitch = 90 degrees causes Gimbal lock in YXZ.
        // Yaw and Roll become linked.
        // Input: Pitch=90, Yaw=10, Roll=0.
        // Quaternion logic should handle this.
        var original = new Vector3(90f, 10f, 0f);
        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        // At exactly 90, the result should be stable.
        _ = result.X.Should().BeApproximately(90f, Tolerance);

        // Yaw should be recovered.
        // Note: In Gimbal lock, Yaw+Roll (or Yaw-Roll) is constant.
        // Our implementation sets Roll=0 and puts everything in Yaw.
        // So (90, 10, 0) -> (90, 10, 0).
        _ = result.Y.Should().BeApproximately(10f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_GimbalLock_NegativePitch_HandlesCorrectly()
    {
        // Pitch = -90 degrees.
        var original = new Vector3(-90f, 25f, 0f);
        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(-90f, Tolerance);
        _ = result.Y.Should().BeApproximately(25f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_PitchStability_NearBoundaries_RemainsConsistent()
    {
        // Test that values near 90-degree boundaries don't cause flipping between representations
        // This tests for the jitter issue when pitch crosses ±90 degrees

        // Test positive boundary (85-95 degrees)
        for (var pitch = 85f; pitch <= 95f; ++pitch)
        {
            var original = new Vector3(pitch, 0f, 0f);
            var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
            var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

            // The result should prefer the representation that's closest to the input
            // For pitch > 90, it should return pitch > 90 (not flip to negative with 180° yaw/roll)
            _ = result.X.Should().BeApproximately(pitch, Tolerance, "Pitch should remain close to input, not flip to alternative");
            _ = result.Y.Should().BeApproximately(0f, Tolerance);
            _ = result.Z.Should().BeApproximately(0f, Tolerance);
        }

        // Test negative boundary (-95 to -85 degrees)
        for (var pitch = -95f; pitch <= -85f; ++pitch)
        {
            var original = new Vector3(pitch, 0f, 0f);
            var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
            var result = Utils.TransformConverter.QuaternionToEulerDegrees(q);

            _ = result.X.Should().BeApproximately(pitch, Tolerance, "Pitch should remain close to input, not flip to alternative");
            _ = result.Y.Should().BeApproximately(0f, Tolerance);
            _ = result.Z.Should().BeApproximately(0f, Tolerance);
        }
    }
}
