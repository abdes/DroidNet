// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;

namespace Oxygen.Editor.World.Tests;

[TestClass]
public class TransformConverterTests
{
    private const float Tolerance = 1e-3f;

    [TestMethod]
    public void QuaternionToEulerDegrees_Identity_ReturnsZeros()
    {
        var q = Quaternion.Identity;
        var e = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        Assert.AreEqual(0f, e.X, Tolerance);
        Assert.AreEqual(0f, e.Y, Tolerance);
        Assert.AreEqual(0f, e.Z, Tolerance);
    }

    [TestMethod]
    public void EulerRoundTrip_RoundTripsWithinTolerance()
    {
        var original = new Vector3(45f, 90f, 180f);

        var q = Utils.TransformConverter.EulerDegreesToQuaternion(original);
        var round = Utils.TransformConverter.QuaternionToEulerDegrees(q);

        Assert.AreEqual(original.X, round.X, Tolerance);
        Assert.AreEqual(original.Y, round.Y, Tolerance);
        Assert.AreEqual(original.Z, round.Z, Tolerance);
    }
}
