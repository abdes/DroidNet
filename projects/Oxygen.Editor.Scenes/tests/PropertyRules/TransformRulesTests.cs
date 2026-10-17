// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Utils;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value rules and the rotation coupling of a node's transform, through <see cref="SetProperties"/>, on <c>A1</c>.
/// </summary>
/// <remarks>
/// The sample's <c>A1</c> has no roll, so a full turn about Z would leave its displayed orientation unchanged. These
/// tests rebuild <c>A1</c> with the orientation the scenario needs; its id, parent and scale stay the sample's.
/// </remarks>
[TestClass]
public sealed class TransformRulesTests
{
    // A1 is rotated 90 degrees about Y and 30 about Z, so that every in-range row changes what A1 shows.
    private static readonly Vector3 RolledOrientation = new(0, 90, 30);

    // Pitch 90 is the gimbal-lock orientation, where editing one axis can change another displayed axis.
    private static readonly Vector3 GimbalOrientation = new(90, 25, 0);

    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("PositionX_Large", SampleScene.A1, Transform.PositionX, 1e6f),
        PropertyCase.Of("PositionY_Negative", SampleScene.A1, Transform.PositionY, -1e6f),
        PropertyCase.Of("RotationZ_FullTurn", SampleScene.A1, Transform.RotationZ, 360f),
        PropertyCase.Of("ScaleX_Negative", SampleScene.A1, Transform.ScaleX, -2f),
        PropertyCase.Of("ScaleY_Tiny", SampleScene.A1, Transform.ScaleY, 1e-4f),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("PositionX_NaN", SampleScene.A1, Transform.PositionX, float.NaN),
        PropertyCase.Of("PositionZ_Infinity", SampleScene.A1, Transform.PositionZ, float.PositiveInfinity),
        PropertyCase.Of("RotationY_NaN", SampleScene.A1, Transform.RotationY, float.NaN),
        PropertyCase.Of("ScaleX_Zero", SampleScene.A1, Transform.ScaleX, 0f),
        PropertyCase.Of("ScaleZ_Infinity", SampleScene.A1, Transform.ScaleZ, float.PositiveInfinity),
    ];

    private static TransformDescriptors Transform => SceneProperties.Transform;

    [TestMethod]
    [DynamicData(nameof(InRange))]
    public async Task Value_InRange_Applies(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SceneWithA1(RolledOrientation)).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        // A rotation edit also reports the axes it couples to, so the edited property is contained, not alone.
        SceneAssert.Applied(result);
        var raised = fixture.Changes.Only().Should().ContainSingle().Which.Should().BeOfType<SceneChange.PropertiesChanged>().Which;
        _ = raised.Node.Should().Be(row.Node);
        _ = raised.Phase.Should().Be(EditPhase.Commit);
        _ = raised.Properties.Should().Contain(row.Property);
    }

    [TestMethod]
    [DynamicData(nameof(OutOfRange))]
    public async Task Value_OutOfRange_IsInvalid(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, row.Node, row.Property);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow(0, DisplayName = "X")]
    [DataRow(1, DisplayName = "Y")]
    [DataRow(2, DisplayName = "Z")]
    public async Task RotationAxis_Undo_RestoresTheWholeOrientation(int axis)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SceneWithA1(GimbalOrientation)).ConfigureAwait(false);
        var start = OrientationOfA1(fixture);

        var result = await fixture.Authoring.ExecuteAsync(SetAxis(axis, 15f)).ConfigureAwait(false);
        var edited = OrientationOfA1(fixture);
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var afterUndo = OrientationOfA1(fixture);
        var redone = await fixture.Authoring.RedoAsync().ConfigureAwait(false);
        var afterRedo = OrientationOfA1(fixture);

        SceneAssert.Applied(result);
        _ = MathF.Abs(Quaternion.Dot(start, edited)).Should().BeLessThan(1 - 1e-4f, "the edit must change the orientation");
        SceneAssert.Applied(undone);
        SceneAssert.SameRotation(afterUndo, start);
        SceneAssert.Applied(redone);
        SceneAssert.SameRotation(afterRedo, edited);
    }

    [TestMethod]
    [DataRow(0, DisplayName = "X")]
    [DataRow(1, DisplayName = "Y")]
    [DataRow(2, DisplayName = "Z")]
    public async Task RotationAxis_Edit_RaisesAllThreeAxes(int axis)
    {
        using var fixture = await AuthoringFixture.CreateAsync(SceneWithA1(GimbalOrientation)).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(SetAxis(axis, 15f)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var raised = fixture.Changes.Only().Should().ContainSingle().Which.Should().BeOfType<SceneChange.PropertiesChanged>().Which;
        _ = raised.Node.Should().Be(SampleScene.A1);

        // The axes are coupled (3.10): at gimbal lock an edit of one moves the reading of another.
        _ = raised.Properties.Should().BeEquivalentTo([Transform.RotationX.Id, Transform.RotationY.Id, Transform.RotationZ.Id]);
    }

    private static SceneData SceneWithA1(Vector3 eulerDegrees)
        => SampleScene.Builder()
            .Without(SampleScene.A1)
            .Node(
                SampleScene.A1,
                "A1",
                parent: SampleScene.A,
                transform: SampleScene.Transform(rotation: TransformConverter.EulerDegreesToQuaternion(eulerDegrees), scale: new Vector3(2)))
            .Node(SampleScene.A11, "A11", parent: SampleScene.A1)
            .Build();

    private static Quaternion OrientationOfA1(AuthoringFixture fixture)
        => fixture.Node(SampleScene.A1).Components.OfType<TransformComponent>().Single().LocalRotation;

    private static SetProperties SetAxis(int axis, float degrees)
    {
        var property = axis switch
        {
            0 => Transform.RotationX,
            1 => Transform.RotationY,
            _ => Transform.RotationZ,
        };
        var edit = new PropertyEdit.Builder().Set(property, degrees).Build();
        return new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.A1] = edit }, "Rotate A1");
    }
}
