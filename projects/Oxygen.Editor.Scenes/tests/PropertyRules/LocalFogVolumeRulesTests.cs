// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value rules of local fog volumes, through <see cref="SetProperties"/>, on <c>Fog</c>, which has the defaults:
/// extinction 1, falloff 1000, offset 0, phase 0.2, albedo (1, 1, 1), emissive 0, sort priority 0.
/// </summary>
/// <remarks>
/// A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the node and the property.
/// The albedo and emissive boundary rows put the boundary on some channels and keep another inside, so that the row
/// differs from the default.
/// </remarks>
[TestClass]
public sealed class LocalFogVolumeRulesTests
{
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("RadialExtinction_Zero", SampleScene.Fog, Fog.RadialFogExtinctionDescriptor, 0f),
        PropertyCase.Of("HeightFalloff_Zero", SampleScene.Fog, Fog.HeightFogFalloffDescriptor, 0f),
        PropertyCase.Of("HeightOffset_Negative", SampleScene.Fog, Fog.HeightFogOffsetDescriptor, -10f),
        PropertyCase.Of("PhaseG_Max", SampleScene.Fog, Fog.FogPhaseGDescriptor, 0.999f),
        PropertyCase.Of("Albedo_One", SampleScene.Fog, Fog.FogAlbedoDescriptor, new Vector3(1, 0.5f, 1)),
        PropertyCase.Of("Emissive_Zero", SampleScene.Fog, Fog.FogEmissiveDescriptor, new Vector3(0, 0.5f, 0)),
        PropertyCase.Of("SortPriority_Min", SampleScene.Fog, Fog.SortPriorityDescriptor, -127),
        PropertyCase.Of("SortPriority_Max", SampleScene.Fog, Fog.SortPriorityDescriptor, 127),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("RadialExtinction_Negative", SampleScene.Fog, Fog.RadialFogExtinctionDescriptor, -0.01f),
        PropertyCase.Of("PhaseG_One", SampleScene.Fog, Fog.FogPhaseGDescriptor, 1f),
        PropertyCase.Of("Albedo_AboveOne", SampleScene.Fog, Fog.FogAlbedoDescriptor, new Vector3(1, 1.01f, 1)),
        PropertyCase.Of("Emissive_Negative", SampleScene.Fog, Fog.FogEmissiveDescriptor, new Vector3(0, -0.01f, 0)),
        PropertyCase.Of("SortPriority_BelowMin", SampleScene.Fog, Fog.SortPriorityDescriptor, -128),
        PropertyCase.Of("SortPriority_AboveMax", SampleScene.Fog, Fog.SortPriorityDescriptor, 128),
        PropertyCase.Of("HeightOffset_NaN", SampleScene.Fog, Fog.HeightFogOffsetDescriptor, float.NaN),
    ];

    private static LocalFogVolumeDescriptors Fog => SceneProperties.LocalFogVolume;

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
}
