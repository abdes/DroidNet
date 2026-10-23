// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value and cross-field rules of directional lights, through <see cref="SetProperties"/>: <c>Sun</c> owns the
/// primary atmosphere slot, <c>Moon</c> owns none.
/// </summary>
/// <remarks>
/// These tests rebuild <c>Sun</c> (color 0.2 / 0.4 / 0.6) and <c>Moon</c> (two cascades), keeping their ids, slots and
/// every other value, so that the boundary row <c>CascadeCount_4</c> and the color channel edits change something.
/// A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the node and the property;
/// a rule across fields is <see cref="SceneEditCodes.PropertiesInconsistent"/> naming the node and no property.
/// </remarks>
[TestClass]
public sealed class DirectionalLightRulesTests
{
    private static readonly Vector3 SunColor = new(0.2f, 0.4f, 0.6f);

    // Each in-range row is the boundary value of its rule; Moon starts with two cascades, so four is a change.
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("CascadeCount_1", SampleScene.Sun, SceneSchema.DirectionalLight.CascadeCount, 1),
        PropertyCase.Of("CascadeCount_4", SampleScene.Moon, SceneSchema.DirectionalLight.CascadeCount, 4),
        PropertyCase.Of("DistributionExponent_1", SampleScene.Sun, SceneSchema.DirectionalLight.DistributionExponent, 1f),
        PropertyCase.Of("TransitionFraction_0", SampleScene.Sun, SceneSchema.DirectionalLight.TransitionFraction, 0f),
        PropertyCase.Of("TransitionFraction_1", SampleScene.Sun, SceneSchema.DirectionalLight.TransitionFraction, 1f),
        PropertyCase.Of("AngularSize_0", SampleScene.Sun, SceneSchema.DirectionalLight.AngularSizeRadians, 0f),
        PropertyCase.Of("AngularSize_Pi", SampleScene.Sun, SceneSchema.DirectionalLight.AngularSizeRadians, MathF.PI),
        PropertyCase.Of("Color_Black", SampleScene.Sun, SceneSchema.DirectionalLight.Color, Vector3.Zero),
        PropertyCase.Of("DiskScale_Zero", SampleScene.Sun, SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, Vector3.Zero),
        PropertyCase.Of("IntensityLux_Zero", SampleScene.Sun, SceneSchema.DirectionalLight.IntensityLux, 0f),
        PropertyCase.Of("Color_White", SampleScene.Sun, SceneSchema.DirectionalLight.Color, Vector3.One),
        PropertyCase.Of("ExposureCompensation_Max", SampleScene.Sun, SceneSchema.DirectionalLight.ExposureCompensation, 10f),
        PropertyCase.Of("ShadowBias_Max", SampleScene.Sun, SceneSchema.DirectionalLight.ShadowBias, 10f),
    ];

    // Each out-of-range row is the first value past the boundary of its rule, or a value no rule accepts.
    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("CascadeCount_0", SampleScene.Sun, SceneSchema.DirectionalLight.CascadeCount, 0),
        PropertyCase.Of("CascadeCount_5", SampleScene.Sun, SceneSchema.DirectionalLight.CascadeCount, 5),
        PropertyCase.Of("DistributionExponent_BelowOne", SampleScene.Sun, SceneSchema.DirectionalLight.DistributionExponent, 0.99f),
        PropertyCase.Of("TransitionFraction_Negative", SampleScene.Sun, SceneSchema.DirectionalLight.TransitionFraction, -0.01f),
        PropertyCase.Of("DistanceFadeoutFraction_AboveOne", SampleScene.Sun, SceneSchema.DirectionalLight.DistanceFadeoutFraction, 1.01f),
        PropertyCase.Of("MaxShadowDistance_Zero", SampleScene.Sun, SceneSchema.DirectionalLight.MaxShadowDistance, 0f),
        PropertyCase.Of("AngularSize_AbovePi", SampleScene.Sun, SceneSchema.DirectionalLight.AngularSizeRadians, MathF.PI + 0.01f),
        PropertyCase.Of("Color_NegativeRed", SampleScene.Sun, SceneSchema.DirectionalLight.Color, new Vector3(-0.1f, 1, 1)),
        PropertyCase.Of("Color_AboveOne", SampleScene.Sun, SceneSchema.DirectionalLight.Color, new Vector3(1, 1, 1.01f)),
        PropertyCase.Of("ExposureCompensation_AboveMax", SampleScene.Sun, SceneSchema.DirectionalLight.ExposureCompensation, 10.01f),
        PropertyCase.Of("ShadowBias_AboveMax", SampleScene.Sun, SceneSchema.DirectionalLight.ShadowBias, 10.01f),
        PropertyCase.Of("ExposureCompensation_NaN", SampleScene.Sun, SceneSchema.DirectionalLight.ExposureCompensation, float.NaN),
        PropertyCase.Of("ShadowBias_Negative", SampleScene.Sun, SceneSchema.DirectionalLight.ShadowBias, -1f),
        PropertyCase.Of("ShadowNormalBias_Negative", SampleScene.Sun, SceneSchema.DirectionalLight.ShadowNormalBias, -1f),
        PropertyCase.Of("DiskScale_Negative", SampleScene.Sun, SceneSchema.DirectionalLight.AtmosphereDiskLuminanceScaleRgb, new Vector3(-1, 1, 1)),
        PropertyCase.Of("IntensityLux_Negative", SampleScene.Sun, SceneSchema.DirectionalLight.IntensityLux, -1f),
        PropertyCase.Of("ShadowResolutionHint_Undefined", SampleScene.Sun, SceneSchema.DirectionalLight.ShadowResolutionHint, (ShadowResolutionHint)99),
        PropertyCase.Of("SplitMode_Undefined", SampleScene.Sun, SceneSchema.DirectionalLight.SplitMode, (DirectionalCsmSplitMode)99),
        PropertyCase.Of("AtmosphereSlot_Undefined", SampleScene.Sun, SceneSchema.DirectionalLight.AtmosphereSlot, (AtmosphereLightSlot)99),
    ];

    [TestMethod]
    [DynamicData(nameof(InRange))]
    public async Task Value_InRange_Applies(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(row.Node, [row.Property], EditPhase.Commit));
    }

    [TestMethod]
    [DynamicData(nameof(OutOfRange))]
    public async Task Value_OutOfRange_IsInvalid(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyOutOfRange, row.Node, row.Property);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DataRow(2, 1, 8f, DisplayName = "TwoCascades_EqualDistances")]
    [DataRow(4, 2, 200f, DisplayName = "FourCascades_ThirdAboveFourth")]
    public async Task ActiveCascadeDistancesNotIncreasing_IsInconsistent(int cascadeCount, int distanceIndex, float distance)
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.CascadeCount, cascadeCount).Set(Distance(distanceIndex), distance).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SampleScene.Sun);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task InactiveCascadeDistanceOutOfOrder_Applies()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.CascadeCount, 2).Set(SceneSchema.DirectionalLight.CascadeDistance3, 5f).Build();

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var sun = LightOf(fixture, SampleScene.Sun);
        _ = sun.CascadeCount.Should().Be(2);
        _ = sun.CascadeDistances.W.Should().Be(5f);
    }

    [TestMethod]
    public async Task IntensityWithBreakingCascadeDistance_ChangesNeither()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.IntensityLux, 5_000f).Set(SceneSchema.DirectionalLight.CascadeDistance0, 500f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SampleScene.Sun);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task AtmosphereSlotOwnedByAnotherLight_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(SetSlot(SampleScene.Moon, AtmosphereLightSlot.Primary)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AtmosphereSlotOccupied, SampleScene.Moon);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ClearOwnerThenAssign_InOneBatch_MovesTheSun()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Move the sun", [SetSlot(SampleScene.Sun, AtmosphereLightSlot.None), SetSlot(SampleScene.Moon, AtmosphereLightSlot.Primary)]);

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);
        var moved = (LightOf(fixture, SampleScene.Sun).AtmosphereSlot, LightOf(fixture, SampleScene.Moon).AtmosphereSlot);
        var entries = fixture.History.UndoStack.Count;
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var restored = (LightOf(fixture, SampleScene.Sun).AtmosphereSlot, LightOf(fixture, SampleScene.Moon).AtmosphereSlot);

        SceneAssert.Applied(result);
        _ = moved.Should().Be((AtmosphereLightSlot.None, AtmosphereLightSlot.Primary));
        _ = entries.Should().Be(1);
        SceneAssert.Applied(undone);
        _ = restored.Should().Be((AtmosphereLightSlot.Primary, AtmosphereLightSlot.None));
    }

    [TestMethod]
    public async Task AssignThenClearOwner_InOneBatch_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var batch = new SceneEditBatch("Move the sun", [SetSlot(SampleScene.Moon, AtmosphereLightSlot.Primary), SetSlot(SampleScene.Sun, AtmosphereLightSlot.None)]);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(batch).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AtmosphereSlotOccupied, SampleScene.Moon);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task OtherPropertyOfTheSun_KeepsItsSlot()
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.IntensityLux, 5f).Build();

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = LightOf(fixture, SampleScene.Sun).AtmosphereSlot.Should().Be(AtmosphereLightSlot.Primary);
    }

    [TestMethod]
    [DataRow(0, DisplayName = "R")]
    [DataRow(1, DisplayName = "G")]
    [DataRow(2, DisplayName = "B")]
    public async Task ColorChannel_Edit_KeepsTheOtherChannels(int channel)
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var edited = channel switch
        {
            0 => new Vector3(0.9f, SunColor.Y, SunColor.Z),
            1 => new Vector3(SunColor.X, 0.9f, SunColor.Z),
            _ => new Vector3(SunColor.X, SunColor.Y, 0.9f),
        };
        var edit = new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.Color, edited).Build();

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);
        var applied = LightOf(fixture, SampleScene.Sun).Color;
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var restored = LightOf(fixture, SampleScene.Sun).Color;

        SceneAssert.Applied(result);
        _ = applied.Should().Be(edited);
        SceneAssert.Applied(undone);
        _ = restored.Should().Be(SunColor);
    }

    [TestMethod]
    [DataRow(0, DisplayName = "Distance0")]
    [DataRow(1, DisplayName = "Distance1")]
    [DataRow(2, DisplayName = "Distance2")]
    [DataRow(3, DisplayName = "Distance3")]
    public async Task CascadeDistance_Edit_ChangesOnlyThatDistance(int index)
    {
        using var fixture = await AuthoringFixture.CreateAsync(LightScene()).ConfigureAwait(false);
        var original = LightOf(fixture, SampleScene.Sun).CascadeDistances;

        // Each value keeps the four distances increasing, so the edit is valid.
        float[] values = [10f, 30f, 70f, 200f];
        var edit = new PropertyEdit.Builder().Set(Distance(index), values[index]).Build();
        var expected = new Vector4(
            index == 0 ? values[0] : original.X,
            index == 1 ? values[1] : original.Y,
            index == 2 ? values[2] : original.Z,
            index == 3 ? values[3] : original.W);

        var result = await fixture.Authoring.ExecuteAsync(Edit(SampleScene.Sun, edit)).ConfigureAwait(false);
        var applied = LightOf(fixture, SampleScene.Sun).CascadeDistances;
        var undone = await fixture.Authoring.UndoAsync().ConfigureAwait(false);
        var restored = LightOf(fixture, SampleScene.Sun).CascadeDistances;

        SceneAssert.Applied(result);
        _ = applied.Should().Be(expected);
        SceneAssert.Applied(undone);
        _ = restored.Should().Be(original);
    }

    private static SceneFileData LightScene()
        => SampleScene.Builder()
            .Without(SampleScene.Sun)
            .Without(SampleScene.Moon)
            .Node(
                SampleScene.Sun,
                "Sun",
                components: [new DirectionalLightData { Id = SampleScene.SunLight, Name = "Light", Color = SunColor, AtmosphereSlot = AtmosphereLightSlot.Primary }])
            .Node(
                SampleScene.Moon,
                "Moon",
                components: [new DirectionalLightData { Id = SampleScene.MoonLight, Name = "Light", AtmosphereSlot = AtmosphereLightSlot.None, CascadeCount = 2 }])
            .Build();

    private static PropertyId<float> Distance(int index)
        => index switch
        {
            0 => SceneSchema.DirectionalLight.CascadeDistance0,
            1 => SceneSchema.DirectionalLight.CascadeDistance1,
            2 => SceneSchema.DirectionalLight.CascadeDistance2,
            _ => SceneSchema.DirectionalLight.CascadeDistance3,
        };

    private static DirectionalLightComponent LightOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<DirectionalLightComponent>().Single();

    private static SetProperties Edit(NodeId node, PropertyEdit edit)
        => new(new Dictionary<NodeId, PropertyEdit> { [node] = edit }, "Light");

    private static SetProperties SetSlot(NodeId node, AtmosphereLightSlot slot)
        => Edit(node, new PropertyEdit.Builder().Set(SceneSchema.DirectionalLight.AtmosphereSlot, slot).Build());
}
