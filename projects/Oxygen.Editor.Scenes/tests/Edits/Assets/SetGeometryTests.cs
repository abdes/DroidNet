// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Slots;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Assets;

/// <summary>
/// What is specific to <see cref="SetGeometry"/> (scene-edits.md 4): a geometry component always references a
/// geometry, and override slots stay. Targets, locks, undo and the change stream of every edit are
/// <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetGeometryTests
{
    [TestMethod]
    public async Task NewAsset_ReplacesTheGeometryAndRaisesGeometryChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = GeometryOf(fixture, SampleScene.Mesh).Geometry!.Uri.Should().Be(SampleScene.SphereGeometry);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.GeometryChanged(SampleScene.Mesh));
    }

    [TestMethod]
    public async Task NoAsset_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], geometry: null)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.GeometryRequired, SampleScene.Mesh);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task NewAsset_KeepsOverrideSlots()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var slotsBefore = OverridesOf(fixture, SampleScene.Mesh);

        var result = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.SphereGeometry)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = slotsBefore.Should().NotBeEmpty("the sample's Mesh has an override slot");
        _ = OverridesOf(fixture, SampleScene.Mesh).Should().Equal(slotsBefore);
    }

    [TestMethod]
    public async Task NodeWithoutGeometry_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.B], SampleScene.SphereGeometry)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.ComponentMissing, SampleScene.B);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task SameAsset_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetGeometry([SampleScene.Mesh], SampleScene.CubeGeometry)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    private static GeometryComponent GeometryOf(AuthoringFixture fixture, NodeId node)
        => fixture.Node(node).Components.OfType<GeometryComponent>().Single();

    // Each override as its target and material, so two reads compare by value.
    private static List<string> OverridesOf(AuthoringFixture fixture, NodeId node)
        => [.. GeometryOf(fixture, node).OverrideSlots.OfType<MaterialsSlot>().Select(static slot => $"{slot.Target} -> {slot.Material.Uri}")];
}
