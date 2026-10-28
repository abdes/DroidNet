// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.World.Tests.Utils;

/// <summary>
/// Verifies conversion between transforms, quaternions, euler angles, and native types in <see cref="Oxygen.Editor.World.Utils.TransformConverter"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(Oxygen.Editor.World.Utils.TransformConverter))]
public sealed class TransformConverterTests
{
    private const float Tolerance = 1e-3f;

    [TestMethod]
    public void QuaternionToEulerDegrees_IdentityQuaternion_ReturnsZeroAngles()
    {
        var q = Quaternion.Identity;
        var e = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);
        _ = e.X.Should().BeApproximately(0f, Tolerance);
        _ = e.Y.Should().BeApproximately(0f, Tolerance);
        _ = e.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void NormalizeAngle_OutOfRangeValue_NormalizesToCanonical()
    {
        _ = World.Utils.TransformConverter.NormalizeAngle(370f).Should().BeApproximately(10f, Tolerance);
        _ = World.Utils.TransformConverter.NormalizeAngle(190f).Should().BeApproximately(-170f, Tolerance);
        _ = World.Utils.TransformConverter.NormalizeAngle(360f).Should().BeApproximately(0f, Tolerance);
        _ = World.Utils.TransformConverter.NormalizeAngle(-370f).Should().BeApproximately(-10f, Tolerance);
    }

    [TestMethod]
    public void NormalizeEuler_OutOfRangeVector_NormalizesEachComponent()
    {
        var e = new Vector3(370f, 190f, -370f);
        var n = World.Utils.TransformConverter.NormalizeEulerDegrees(e);

        _ = n.X.Should().BeApproximately(10f, Tolerance);
        _ = n.Y.Should().BeApproximately(-170f, Tolerance);
        _ = n.Z.Should().BeApproximately(-10f, Tolerance);
    }

    [TestMethod]
    public void NormalizeScaleValue_ZeroOrSmallMagnitude_ReplacesWithEpsilon()
    {
        const float eps = 1e-3f;
        _ = World.Utils.TransformConverter.NormalizeScaleValue(0f, eps).Should().BeApproximately(eps, 1e-6f);
        _ = World.Utils.TransformConverter.NormalizeScaleValue(1e-6f, eps).Should().BeApproximately(eps, 1e-6f);
        _ = World.Utils.TransformConverter.NormalizeScaleValue(-1e-6f, eps).Should().BeApproximately(-eps, 1e-6f);
        _ = World.Utils.TransformConverter.NormalizeScaleValue(2.0f, eps).Should().BeApproximately(2.0f, 1e-6f);
    }

    [TestMethod]
    public void NormalizeScaleVector_ZeroOrSmallMagnitude_ReplacesWithEpsilonPerComponent()
    {
        const float eps = 1e-3f;
        var v = new Vector3(0f, -1e-6f, 5.0f);
        var n = World.Utils.TransformConverter.NormalizeScaleVector(v, eps);

        _ = n.X.Should().BeApproximately(eps, 1e-6f);
        _ = n.Y.Should().BeApproximately(-eps, 1e-6f);
        _ = n.Z.Should().BeApproximately(5.0f, 1e-6f);
    }

    [TestMethod]
    [DataRow(0f, 0f, 0f)]
    [DataRow(10f, 20f, 30f)]
    [DataRow(-10f, -20f, -30f)]
    [DataRow(45f, 45f, 45f)]
    [DataRow(90f, 0f, 0f)] // X +90
    [DataRow(0f, 90f, 0f)] // Y +90
    [DataRow(0f, -90f, 0f)] // Y -90
    [DataRow(0f, 0f, 90f)] // Z +90
    public void QuaternionToEulerDegrees_RoundTrip_PreservesAngles(float x, float y, float z)
    {
        var original = new Vector3(x, y, z);

        // 1. Convert Euler -> Quaternion
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);

        // 2. Convert Quaternion -> Euler
        var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

        // 3. Verify that we get the same angles back (within tolerance)
        // Note: This relies on the "Human Eye" optimization to return the same values for inputs like (100, 0, 0).
        // For standard canonical angles, this should always match.
        _ = result.X.Should().BeApproximately(original.X, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on X for input {original}"));
        _ = result.Y.Should().BeApproximately(original.Y, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on Y for input {original}"));
        _ = result.Z.Should().BeApproximately(original.Z, Tolerance, string.Create(CultureInfo.InvariantCulture, $"Mismatch on Z for input {original}"));
    }

    [TestMethod]
    public void EulerDegreesToQuaternion_AppliesXThenYThenZ()
    {
        // R = Rz * Ry * Rx: Z(90) * X(90) = (0.5, 0.5, 0.5, 0.5); X(90) * Z(90) would negate y.
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(new Vector3(90f, 0f, 90f));

        _ = q.X.Should().BeApproximately(0.5f, Tolerance);
        _ = q.Y.Should().BeApproximately(0.5f, Tolerance);
        _ = q.Z.Should().BeApproximately(0.5f, Tolerance);
        _ = q.W.Should().BeApproximately(0.5f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_HumanEyeOptimization_PreservesYBeyond90()
    {
        // Case: Y = 100 degrees.
        // Canonical X-Y-Z would represent this as X=180, Y=80, Z=180.
        // We want the converter to prefer (0, 100, 0) because it's "cleaner" (smaller X and Z).
        var original = new Vector3(0f, 100f, 0f);
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(0f, Tolerance);
        _ = result.Y.Should().BeApproximately(100f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_HumanEyeOptimization_PreservesYBelowMinus90()
    {
        // Case: Y = -100 degrees.
        // Canonical X-Y-Z would represent this as X=180, Y=-80, Z=180.
        // We want the converter to prefer (0, -100, 0).
        var original = new Vector3(0f, -100f, 0f);
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(0f, Tolerance);
        _ = result.Y.Should().BeApproximately(-100f, Tolerance);
        _ = result.Z.Should().BeApproximately(0f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_GimbalLock_PositiveY_HandlesCorrectly()
    {
        // Y = 90 degrees causes gimbal lock in X-Y-Z: X and Z turn about one axis.
        // Our implementation sets X=0 and puts everything in Z.
        // So (0, 90, 10) -> (0, 90, 10).
        var original = new Vector3(0f, 90f, 10f);
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(0f, Tolerance);
        _ = result.Y.Should().BeApproximately(90f, Tolerance);
        _ = result.Z.Should().BeApproximately(10f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_GimbalLock_NegativeY_HandlesCorrectly()
    {
        // Y = -90 degrees.
        var original = new Vector3(0f, -90f, 25f);
        var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

        _ = result.X.Should().BeApproximately(0f, Tolerance);
        _ = result.Y.Should().BeApproximately(-90f, Tolerance);
        _ = result.Z.Should().BeApproximately(25f, Tolerance);
    }

    [TestMethod]
    public void QuaternionToEulerDegrees_YStability_NearBoundaries_RemainsConsistent()
    {
        // Test that values near 90-degree boundaries don't cause flipping between representations
        // This tests for the jitter issue when Y crosses ±90 degrees

        // Test positive boundary (85-95 degrees)
        for (var y = 85f; y <= 95f; ++y)
        {
            var original = new Vector3(0f, y, 0f);
            var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
            var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

            // The result should prefer the representation that's closest to the input
            // For Y > 90, it should return Y > 90 (not flip with 180° X and Z)
            _ = result.X.Should().BeApproximately(0f, Tolerance);
            _ = result.Y.Should().BeApproximately(y, Tolerance, "Y should remain close to input, not flip to alternative");
            _ = result.Z.Should().BeApproximately(0f, Tolerance);
        }

        // Test negative boundary (-95 to -85 degrees)
        for (var y = -95f; y <= -85f; ++y)
        {
            var original = new Vector3(0f, y, 0f);
            var q = World.Utils.TransformConverter.EulerDegreesToQuaternion(original);
            var result = World.Utils.TransformConverter.QuaternionToEulerDegrees(q);

            _ = result.X.Should().BeApproximately(0f, Tolerance);
            _ = result.Y.Should().BeApproximately(y, Tolerance, "Y should remain close to input, not flip to alternative");
            _ = result.Z.Should().BeApproximately(0f, Tolerance);
        }
    }

    [TestMethod]
    public void ToNative_ExtractsPositionRotationAndScale()
    {
        var transform = new TransformComponent
        {
            Name = "Transform",
            LocalPosition = new Vector3(1f, 2f, 3f),
            LocalRotation = new Quaternion(0.1f, 0.2f, 0.3f, 0.9f),
            LocalScale = new Vector3(2f, 3f, 4f),
        };

        var (position, rotation, scale) = World.Utils.TransformConverter.ToNative(transform);

        _ = position.Should().Be(new Vector3(1f, 2f, 3f));
        _ = rotation.Should().Be(new Quaternion(0.1f, 0.2f, 0.3f, 0.9f));
        _ = scale.Should().Be(new Vector3(2f, 3f, 4f));
    }

    [TestMethod]
    public void GetPosition_GetRotation_GetScale_ExtractIndividualComponents()
    {
        var transform = new TransformComponent
        {
            Name = "Transform",
            LocalPosition = new Vector3(10f, 20f, 30f),
            LocalRotation = new Quaternion(0f, 1f, 0f, 0f),
            LocalScale = new Vector3(5f, 5f, 5f),
        };

        _ = World.Utils.TransformConverter.GetPosition(transform).Should().Be(new Vector3(10f, 20f, 30f));
        _ = World.Utils.TransformConverter.GetRotation(transform).Should().Be(new Quaternion(0f, 1f, 0f, 0f));
        _ = World.Utils.TransformConverter.GetScale(transform).Should().Be(new Vector3(5f, 5f, 5f));
    }

    [TestMethod]
    public void NativeAccessors_WhenGivenNullTransform_ThrowArgumentNullException()
    {
        Action act1 = () => World.Utils.TransformConverter.ToNative(null!);
        Action act2 = () => World.Utils.TransformConverter.GetPosition(null!);
        Action act3 = () => World.Utils.TransformConverter.GetRotation(null!);
        Action act4 = () => World.Utils.TransformConverter.GetScale(null!);

        _ = act1.Should().Throw<ArgumentNullException>();
        _ = act2.Should().Throw<ArgumentNullException>();
        _ = act3.Should().Throw<ArgumentNullException>();
        _ = act4.Should().Throw<ArgumentNullException>();
    }
}
