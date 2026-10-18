// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>
/// The value and cross-field rules of perspective cameras, through <see cref="SetProperties"/>: <c>Cam</c> has near
/// plane 0.1 and far plane 100.
/// </summary>
/// <remarks>
/// A value a descriptor rejects is <see cref="SceneEditCodes.PropertyOutOfRange"/> naming the node and the property;
/// a rule across fields is <see cref="SceneEditCodes.PropertiesInconsistent"/> naming the node and no property
/// (scene-edits.md 3.8).
/// </remarks>
[TestClass]
public sealed class PerspectiveCameraRulesTests
{
    public static IEnumerable<TestDataRow<PropertyCase>> InRange =>
    [
        PropertyCase.Of("FieldOfView_1", SampleScene.Cam, Camera.FieldOfViewDegrees, 1f),
        PropertyCase.Of("FieldOfView_179", SampleScene.Cam, Camera.FieldOfViewDegrees, 179f),
        PropertyCase.Of("NearPlane_Tiny", SampleScene.Cam, Camera.NearPlane, 1e-4f),
        PropertyCase.Of("FarPlane_Large", SampleScene.Cam, Camera.FarPlane, 1e6f),
        PropertyCase.Of("AspectRatio_Small", SampleScene.Cam, Camera.AspectRatio, 0.01f),
        PropertyCase.Of("ApertureF_Small", SampleScene.Cam, Camera.ApertureF, 1e-3f),
        PropertyCase.Of("ShutterRate_Small", SampleScene.Cam, Camera.ShutterRate, 1e-3f),
        PropertyCase.Of("Iso_Small", SampleScene.Cam, Camera.Iso, 1e-3f),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> OutOfRange =>
    [
        PropertyCase.Of("FieldOfView_BelowOne", SampleScene.Cam, Camera.FieldOfViewDegrees, 0.99f),
        PropertyCase.Of("FieldOfView_Above179", SampleScene.Cam, Camera.FieldOfViewDegrees, 179.01f),
        PropertyCase.Of("FieldOfView_NaN", SampleScene.Cam, Camera.FieldOfViewDegrees, float.NaN),
        PropertyCase.Of("NearPlane_Zero", SampleScene.Cam, Camera.NearPlane, 0f),
        PropertyCase.Of("NearPlane_Negative", SampleScene.Cam, Camera.NearPlane, -1f),
        PropertyCase.Of("FarPlane_Infinity", SampleScene.Cam, Camera.FarPlane, float.PositiveInfinity),
        PropertyCase.Of("AspectRatio_Zero", SampleScene.Cam, Camera.AspectRatio, 0f),
        PropertyCase.Of("ApertureF_Zero", SampleScene.Cam, Camera.ApertureF, 0f),
        PropertyCase.Of("ShutterRate_Zero", SampleScene.Cam, Camera.ShutterRate, 0f),
        PropertyCase.Of("Iso_Zero", SampleScene.Cam, Camera.Iso, 0f),
        PropertyCase.Of("AspectMode_Undefined", SampleScene.Cam, Camera.AspectMode, (CameraAspectMode)99),
    ];

    public static IEnumerable<TestDataRow<PropertyCase>> NearNotBelowFar =>
    [
        PropertyCase.Of("NearEqualsFar", SampleScene.Cam, Camera.NearPlane, 100f),
        PropertyCase.Of("NearAboveFar", SampleScene.Cam, Camera.NearPlane, 200f),
        PropertyCase.Of("FarEqualsNear", SampleScene.Cam, Camera.FarPlane, 0.1f),
        PropertyCase.Of("FarBelowNear", SampleScene.Cam, Camera.FarPlane, 0.05f),
    ];

    private static NodeId SecondCam { get; } = NodeId.FromValue(SampleScene.Tagged(1, "CB"));

    private static PerspectiveCameraDescriptors Camera => SceneProperties.PerspectiveCamera;

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

    [TestMethod]
    public async Task NearAndFarTogether_ValidatesTheResult()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new PropertyEdit.Builder().Set(Camera.NearPlane, 150f).Set(Camera.FarPlane, 300f).Build();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.Cam, edit))).ConfigureAwait(false);

        SceneAssert.Applied(result);
        var camera = fixture.Node(SampleScene.Cam).Components.OfType<PerspectiveCamera>().Single();
        _ = camera.NearPlane.Should().Be(150f);
        _ = camera.FarPlane.Should().Be(300f);
    }

    [TestMethod]
    public async Task TwoCamerasOneInvalid_ChangesNeither()
    {
        var scene = SampleScene.Builder()
            .Node(
                SecondCam,
                "Cam2",
                components: [new PerspectiveCameraData { Id = ComponentId.FromValue(SampleScene.Tagged(2, "CB")), Name = "Camera", NearPlane = 0.1f, FarPlane = 5f, AspectMode = CameraAspectMode.Auto }])
            .Build();
        using var fixture = await AuthoringFixture.CreateAsync(scene).ConfigureAwait(false);
        var near = new PropertyEdit.Builder().Set(Camera.NearPlane, 10f).Build();
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(Edit((SampleScene.Cam, near), (SecondCam, near))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertiesInconsistent, SecondCam);
        SceneAssert.Unchanged(fixture, before);
    }

    private static SetProperties Edit(params (NodeId node, PropertyEdit edit)[] edits)
        => new(edits.ToDictionary(static pair => pair.node, static pair => pair.edit), "Cameras");
}
