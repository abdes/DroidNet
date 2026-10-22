// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Components;

/// <summary>
/// What is specific to <see cref="AddComponent"/> (scene-edits.md 3.10.1). Targets, locks, undo and the change stream
/// of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class AddComponentTests
{
    public static IEnumerable<TestDataRow<DuplicateCase>> SecondOfAFamily =>
    [
        Row("TransformOnB", SampleScene.B, new TransformData { Name = "Transform" }),
        Row("GeometryOnMesh", SampleScene.Mesh, new GeometryComponentData { Name = "Geometry", GeometryUri = SampleScene.SphereGeometry }),
        Row("OrthographicCameraOnCam", SampleScene.Cam, new OrthographicCameraData { Name = "Camera", AspectMode = CameraAspectMode.Auto }),
        Row("SpotLightOnLamp", SampleScene.Lamp, new SpotLightData { Name = "Light" }),
        Row("DirectionalLightOnSun", SampleScene.Sun, new DirectionalLightData { Name = "Light" }),
        Row("FogVolumeOnFog", SampleScene.Fog, new LocalFogVolumeData { Name = "Fog" }),
        Row("ScriptingOnScript", SampleScene.Script, new ScriptingData { Name = "Scripting" }),
    ];

    [TestMethod]
    public async Task PointLightToB_AddsAtTheIndexAndRaisesComponentAdded()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var light = ComponentId.New();

        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.B, PointLight(light), index: SceneBuilder.RequiredComponentCount)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.B).Components.Select(static component => component.Id).Should().Equal(SceneBuilder.ComponentIdsOf(SampleScene.B, light));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ComponentAdded(SampleScene.B, light));
    }

    [TestMethod]
    public async Task AtIndex_InsertsThere()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var light = ComponentId.New();

        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.Mesh, PointLight(light), index: SceneBuilder.RequiredComponentCount)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.Mesh).Components.Select(static component => component.Id)
            .Should().Equal(SceneBuilder.ComponentIdsOf(SampleScene.Mesh, light, SampleScene.MeshGeometry));
    }

    [TestMethod]
    public async Task IndexPastTheEnd_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        // B holds only its required components, so their count appends and one more is past the end.
        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.B, PointLight(ComponentId.New()), index: SceneBuilder.RequiredComponentCount + 1)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PositionOutOfRange, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    [DynamicData(nameof(SecondOfAFamily))]
    public async Task SecondOfAFamily_IsInvalid(DuplicateCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(row.Node, row.Component, index: SceneBuilder.RequiredComponentCount)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentDuplicate, row.Node);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task ExistingComponentId_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.B, PointLight(SceneBuilder.TransformId(SampleScene.B)), index: SceneBuilder.RequiredComponentCount)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentDuplicate, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task DirectionalLightTakingTheSunSlot_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();
        var light = new DirectionalLightData { Name = "Light", AtmosphereSlot = AtmosphereLightSlot.Primary };

        var result = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.B, light, index: SceneBuilder.RequiredComponentCount)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AtmosphereSlotOccupied, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    private static TestDataRow<DuplicateCase> Row(string name, NodeId node, ComponentData component)
        => new(new DuplicateCase(node, component)) { DisplayName = name };

    private static PointLightData PointLight(ComponentId id) => new() { Id = id, Name = "Light" };
}
