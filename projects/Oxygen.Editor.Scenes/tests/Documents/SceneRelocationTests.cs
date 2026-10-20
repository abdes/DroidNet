// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Documents;

/// <summary>
/// An open scene following a relocation that moved the assets it references (scene-edits.md 3.11): the scene is
/// re-pointed outside its history, and its views hear what changed.
/// </summary>
[TestClass]
public sealed class SceneRelocationTests
{
    private static readonly AssetPath MovedCube = AssetPath.Parse("/Content/Shapes/Cube.ogeo");
    private static readonly AssetPath Sky = AssetPath.Parse("/Content/Sky.ocube");
    private static readonly AssetPath MovedSky = AssetPath.Parse("/Content/Skies/Sky.ocube");

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task GeometryMoved_RetargetsAndRaisesGeometryChangedWithoutAnEntry()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        using var changes = new ChangeRecorder(SceneContent.Of(document).Authoring);

        await document.FollowRelocationAsync(Relocation(document, SampleScene.CubeGeometry, MovedCube), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = GeometryOf(document, SampleScene.Mesh).Geometry.Should().Be(MovedCube);
        SceneAssert.Changes(changes.Only(), new SceneChange.GeometryChanged(SampleScene.Mesh));
        _ = document.History.UndoStack.Should().BeEmpty("a relocation's undo is global, not the scene's");
        _ = document.IsDirty.Should().BeFalse();
    }

    [TestMethod]
    public async Task CubemapMoved_RaisesScenePropertiesChanged()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var data = SampleScene.Builder()
            .Environment(new SceneEnvironmentData { SkySphere = new SkySphereEnvironmentData { Enabled = true, Cubemap = Sky } })
            .Build();
        var document = await documents.OpenAsync(data).ConfigureAwait(false);
        using var changes = new ChangeRecorder(SceneContent.Of(document).Authoring);

        await document.FollowRelocationAsync(Relocation(document, Sky, MovedSky), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = SceneContent.Of(document).Scene.Environment.SkySphere.Cubemap.Should().Be(MovedSky);
        _ = changes.Only().Should().ContainSingle()
            .Which.Should().BeOfType<SceneChange.PropertiesChanged>()
            .Which.Node.Should().BeNull("the environment is the scene's own");
        _ = document.History.UndoStack.Should().BeEmpty();
    }

    [TestMethod]
    public async Task AfterRetire_ChangesNothing()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var replaced = SceneContent.Of(document);
        using var changes = new ChangeRecorder(replaced.Authoring);
        _ = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        replaced.ApplyRelocation(Relocation(document, SampleScene.CubeGeometry, MovedCube));

        _ = replaced.Scene.AllNodes.Single(node => node.Id == SampleScene.Mesh).Components.OfType<GeometryComponent>().Single()
            .Geometry.Should().Be(SampleScene.CubeGeometry);
        _ = changes.Count.Should().Be(0);
    }

    // The relocation moved one asset and left the scene file where it was.
    private static DocumentRelocation Relocation(Document document, AssetPath from, AssetPath to)
        => new(document.File, document.Version, asset => asset == from ? to : null);

    private static GeometryComponent GeometryOf(Document document, NodeId node)
        => SceneContent.Of(document).Scene.AllNodes.Single(candidate => candidate.Id == node).Components.OfType<GeometryComponent>().Single();
}
