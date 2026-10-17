// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value and cross-field rules of point and spot lights, through <see cref="SetProperties"/>: <c>Lamp</c> is the
/// point light, <c>Spot</c> the spot light with cones 0.4 / 0.6.
/// </summary>
/// <remarks>
/// A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the node and the property;
/// a rule across fields is <see cref="SceneEditCodes.PropertiesInconsistent"/> naming the node (scene-edits.md 3.8).
/// </remarks>
[TestClass]
public sealed class LocalLightRulesTests
{
    private const float RightAngle = MathF.PI / 2;

    // Each in-range row is the boundary value of its rule.
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("Range_Tiny", SampleScene.Lamp, Point.RangeDescriptor, 1e-6f),
        PropertyCase.Of("Flux_Zero", SampleScene.Lamp, Point.LuminousFluxLumensDescriptor, 0f),
        PropertyCase.Of("SourceRadius_Zero", SampleScene.Lamp, Point.SourceRadiusDescriptor, 0f),
        PropertyCase.Of("Color_Black", SampleScene.Lamp, Point.ColorDescriptor, Vector3.Zero),
        PropertyCase.Of("OuterCone_RightAngle", SampleScene.Spot, SpotLight.OuterConeAngleRadiansDescriptor, RightAngle),
    ];

    // Each out-of-range row is the first value past the boundary of its rule, or a value no rule accepts.
    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("Range_Zero", SampleScene.Lamp, Point.RangeDescriptor, 0f),
        PropertyCase.Of("Range_Negative", SampleScene.Lamp, Point.RangeDescriptor, -1f),
        PropertyCase.Of("Flux_Negative", SampleScene.Lamp, Point.LuminousFluxLumensDescriptor, -1f),
        PropertyCase.Of("SourceRadius_Negative", SampleScene.Lamp, Point.SourceRadiusDescriptor, -1f),
        PropertyCase.Of("ShadowBias_Negative", SampleScene.Lamp, Point.ShadowBiasDescriptor, -1f),
        PropertyCase.Of("Color_NegativeGreen", SampleScene.Lamp, Point.ColorDescriptor, new Vector3(1, -0.1f, 1)),
        PropertyCase.Of("ExposureCompensation_Infinity", SampleScene.Lamp, Point.ExposureCompensationDescriptor, float.PositiveInfinity),
        PropertyCase.Of("InnerCone_Negative", SampleScene.Spot, SpotLight.InnerConeAngleRadiansDescriptor, -0.01f),
        PropertyCase.Of("OuterCone_AboveRightAngle", SampleScene.Spot, SpotLight.OuterConeAngleRadiansDescriptor, RightAngle + 0.01f),
        PropertyCase.Of("ShadowResolutionHint_Undefined", SampleScene.Lamp, Point.ShadowResolutionHintDescriptor, (ShadowResolutionHint)99),
    ];

    private static LocalLightDescriptors Point => SceneProperties.PointLight;

    private static LocalLightDescriptors SpotLight => SceneProperties.SpotLight;

    [TestMethod]
    [DynamicData(nameof(InRange))]
    public async Task Value_InRange_Applies(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(row.Node, [row.Property], EditPhase.Commit));
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
    public async Task InnerConeAboveOuter_IsInconsistent()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(SpotCones(inner: 0.7f, outer: null)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SampleScene.Spot);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task BothConesAtRightAngle_IsInconsistent()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(SpotCones(inner: RightAngle, outer: RightAngle)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SampleScene.Spot);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task RangeAndFlux_RoundTrip()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder()
            .SetRaw(Point.RangeDescriptor.Id, 20f)
            .SetRaw(Point.LuminousFluxLumensDescriptor.Id, 1600f)
            .Build();

        _ = await SceneAssert.RoundTripAsync(fixture, new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Lamp] = edit }, "Lamp")).ConfigureAwait(false);
    }

    private static SetProperties SpotCones(float inner, float? outer)
    {
        var edit = new PropertyEdit.Builder().SetRaw(SpotLight.InnerConeAngleRadiansDescriptor!.Id, inner);
        if (outer is { } value)
        {
            _ = edit.SetRaw(SpotLight.OuterConeAngleRadiansDescriptor!.Id, value);
        }

        return new SetProperties(new Dictionary<NodeId, PropertyEdit> { [SampleScene.Spot] = edit.Build() }, "Spot cones");
    }
}
