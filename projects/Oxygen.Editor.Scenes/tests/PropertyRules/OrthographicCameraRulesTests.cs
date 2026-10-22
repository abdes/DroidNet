// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value and cross-field rules of orthographic cameras, through <see cref="SetProperties"/>:
/// <c>Ortho</c> has size 10, near plane 0.1 and far plane 100.
/// </summary>
/// <remarks>
/// A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the node and the property;
/// a rule across fields is <see cref="SceneEditCodes.PropertiesInconsistent"/> naming the node and no property
/// (scene-edits.md 3.8).
/// </remarks>
[TestClass]
public sealed class OrthographicCameraRulesTests
{
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("Size_Tiny", SampleScene.Ortho, SceneSchema.OrthographicCamera.OrthographicSize, 1e-4f),
        PropertyCase.Of("NearPlane_Tiny", SampleScene.Ortho, SceneSchema.OrthographicCamera.NearPlane, 1e-4f),
        PropertyCase.Of("AspectRatio_Small", SampleScene.Ortho, SceneSchema.OrthographicCamera.AspectRatio, 0.01f),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("Size_Zero", SampleScene.Ortho, SceneSchema.OrthographicCamera.OrthographicSize, 0f),
        PropertyCase.Of("Size_Negative", SampleScene.Ortho, SceneSchema.OrthographicCamera.OrthographicSize, -1f),
        PropertyCase.Of("NearPlane_Zero", SampleScene.Ortho, SceneSchema.OrthographicCamera.NearPlane, 0f),
        PropertyCase.Of("AspectRatio_Zero", SampleScene.Ortho, SceneSchema.OrthographicCamera.AspectRatio, 0f),
        PropertyCase.Of("ApertureF_Zero", SampleScene.Ortho, SceneSchema.OrthographicCamera.ApertureF, 0f),
        PropertyCase.Of("Size_NaN", SampleScene.Ortho, SceneSchema.OrthographicCamera.OrthographicSize, float.NaN),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> NearNotBelowFar =>
    [
        PropertyCase.Of("NearEqualsFar", SampleScene.Ortho, SceneSchema.OrthographicCamera.NearPlane, 100f),
        PropertyCase.Of("FarBelowNear", SampleScene.Ortho, SceneSchema.OrthographicCamera.FarPlane, 0.05f),
    ];

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
    [DynamicData(nameof(NearNotBelowFar))]
    public async Task NearNotBelowFar_IsInconsistent(PropertyCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(row.Edit()).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, row.Node);
        SceneAssert.Unchanged(fixture, before);
    }
}
