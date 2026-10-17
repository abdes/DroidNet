// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Nodes;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Components;

/// <summary>
/// What is specific to <see cref="RemoveComponent"/>. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class RemoveComponentTests
{
    [TestMethod]
    public async Task PointLightFromLamp_RemovesAndRaisesComponentRemovedWithItsKind()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new RemoveComponent(SampleScene.Lamp, SampleScene.LampLight)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Node(SampleScene.Lamp).Components.Select(static component => component.Id).Should().Equal(SceneBuilder.TransformId(SampleScene.Lamp));
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.ComponentRemoved(SampleScene.Lamp, SampleScene.LampLight, SceneComponents.PointLight));
    }

    [TestMethod]
    public async Task Undo_RestoresTheComponentAtItsIndex()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        // A light after the geometry makes the geometry's index differ from the end of the list.
        var light = ComponentId.New();
        _ = await fixture.Authoring.ExecuteAsync(new AddComponent(SampleScene.Mesh, new PointLightData { Id = light, Name = "Light" }, index: 2)).ConfigureAwait(false);

        _ = await SceneAssert.RoundTripAsync(fixture, new RemoveComponent(SampleScene.Mesh, SampleScene.MeshGeometry)).ConfigureAwait(false);

        _ = fixture.Node(SampleScene.Mesh).Components.Select(static component => component.Id)
            .Should().Equal(SceneBuilder.TransformId(SampleScene.Mesh), SampleScene.MeshGeometry, light);
    }

    [TestMethod]
    public async Task Transform_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveComponent(SampleScene.B, SceneBuilder.TransformId(SampleScene.B))).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentNotRemovable, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task UnknownComponent_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new RemoveComponent(SampleScene.B, ComponentId.New())).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }
}
