// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests;

/// <summary>
/// <see cref="PropertyChecks"/>: a property's single-value rule is the range the scene schema generates for it; only an
/// inspector commit clamps, and only where the range says so.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(PropertyChecks))]
public sealed class PropertyChecksTests
{
    [TestMethod]
    [DataRow(60f, RangeCheck.InRange, DisplayName = "InRange")]
    [DataRow(1f, RangeCheck.InRange, DisplayName = "LowerBound")]
    [DataRow(179f, RangeCheck.InRange, DisplayName = "UpperBound")]
    [DataRow(0.99f, RangeCheck.OutOfRange, DisplayName = "BelowLowerBound")]
    [DataRow(179.5f, RangeCheck.OutOfRange, DisplayName = "AboveUpperBound")]
    [DataRow(float.NaN, RangeCheck.NotFinite, DisplayName = "NaN")]
    [DataRow(float.NegativeInfinity, RangeCheck.NotFinite, DisplayName = "Infinity")]
    public void Check_FieldOfView_FollowsTheSchemaRange(float value, RangeCheck expected)
        => _ = PropertyChecks.Check(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, value).Should().Be(expected);

    [TestMethod]
    public void Check_ExclusiveLowerBound_RejectsTheBoundItself()
    {
        _ = PropertyChecks.Check(SceneSchema.PerspectiveCamera.NearPlane, 0f).Should().Be(RangeCheck.OutOfRange);
        _ = PropertyChecks.Check(SceneSchema.PerspectiveCamera.NearPlane, 1e-6f).Should().Be(RangeCheck.InRange);
    }

    [TestMethod]
    public void Check_NonZeroProperty_RejectsZeroOnly()
    {
        _ = PropertyChecks.Check(SceneSchema.Transform.ScaleX, 0f).Should().Be(RangeCheck.OutOfRange);
        _ = PropertyChecks.Check(SceneSchema.Transform.ScaleX, -3f).Should().Be(RangeCheck.InRange);
    }

    [TestMethod]
    public void Check_IntegerProperty_FollowsTheSchemaRange()
    {
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.CascadeCount, 1).Should().Be(RangeCheck.InRange);
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.CascadeCount, 4).Should().Be(RangeCheck.InRange);
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.CascadeCount, 0).Should().Be(RangeCheck.OutOfRange);
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.CascadeCount, 5).Should().Be(RangeCheck.OutOfRange);
    }

    [TestMethod]
    public void Check_VectorProperty_BoundsEachComponentAndReportsNotFiniteFirst()
    {
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.Color, new Vector3(0, 0.5f, 1)).Should().Be(RangeCheck.InRange);
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.Color, new Vector3(0, 0.5f, 1.01f)).Should().Be(RangeCheck.OutOfRange);
        _ = PropertyChecks.Check(SceneSchema.DirectionalLight.Color, new Vector3(-1, float.NaN, 0)).Should().Be(RangeCheck.NotFinite);
    }

    [TestMethod]
    [DataRow(1.0e39, RangeCheck.OutOfRange, DisplayName = "BeyondFloat")]
    [DataRow(-1.0e39, RangeCheck.OutOfRange, DisplayName = "BeyondFloatNegative")]
    [DataRow(double.NaN, RangeCheck.NotFinite, DisplayName = "NaN")]
    [DataRow(double.PositiveInfinity, RangeCheck.NotFinite, DisplayName = "Infinity")]
    [DataRow(12.5, RangeCheck.InRange, DisplayName = "InRange")]
    public void Check_DoubleBeforeNarrowing_FiniteBeyondFloatIsOutOfRange(double value, RangeCheck expected)
        => _ = PropertyChecks.Check(SceneSchema.Transform.PositionX, value).Should().Be(expected);

    [TestMethod]
    public void Commit_ClampingProperty_ClampsIntoTheRange()
    {
        _ = PropertyChecks.Commit(SceneSchema.PerspectiveCamera.FieldOfViewDegrees.Id, 200f).Should().Be(179f);
        _ = PropertyChecks.Commit(SceneSchema.DirectionalLight.Color.Id, new Vector3(1.5f, 0.5f, -1f)).Should().Be(new Vector3(1f, 0.5f, 0f));
        _ = PropertyChecks.Check(SceneSchema.PerspectiveCamera.FieldOfViewDegrees, 200f).Should().Be(RangeCheck.OutOfRange);
    }

    [TestMethod]
    public void Commit_RejectingProperty_LeavesTheValueForValidationToReject()
    {
        _ = PropertyChecks.Commit(SceneSchema.PerspectiveCamera.NearPlane.Id, 0f).Should().Be(0f);
        _ = PropertyChecks.Commit(SceneSchema.DirectionalLight.CascadeCount.Id, 9).Should().Be(9);
    }

    [TestMethod]
    public void Commit_ValueThatCannotBeClamped_IsLeftAsEntered()
    {
        _ = PropertyChecks.Commit(SceneSchema.PerspectiveCamera.FieldOfViewDegrees.Id, float.NaN).Should().Be(float.NaN);
        _ = PropertyChecks.Commit(SceneSchema.DirectionalLight.Color.Id, new Vector3(2f, float.NaN, 0f)).Should().Be(new Vector3(2f, float.NaN, 0f));
        _ = PropertyChecks.Commit(SceneSchema.Node.Visibility.Id, (VisibilitySource)99).Should().Be((VisibilitySource)99);
    }

    [TestMethod]
    public void Require_ValueOutsideTheRange_ThrowsWithTheValue()
    {
        var below = () => PropertyChecks.Require(SceneSchema.SpotLight.Range, 0f);
        var undefined = () => PropertyChecks.Require(SceneSchema.Node.CastsShadows, (ShadowSource)99);
        var inRange = () => PropertyChecks.Require(SceneSchema.SpotLight.Range, 5f);

        _ = below.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be(0f);
        _ = undefined.Should().Throw<ArgumentOutOfRangeException>().Which.ActualValue.Should().Be((ShadowSource)99);
        _ = inRange.Should().NotThrow();
    }

    [TestMethod]
    public void Describe_ValueInRange_Throws()
    {
        var act = () => PropertyChecks.Describe(SceneSchema.SpotLight.Range.Id, RangeCheck.InRange);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Check_PropertyWithoutAFloatRange_Throws()
    {
        var act = () => PropertyChecks.Check(new PropertyId<float>("transform", "/no_such_property"), 1f);

        _ = act.Should().Throw<ArgumentException>();
    }
}
