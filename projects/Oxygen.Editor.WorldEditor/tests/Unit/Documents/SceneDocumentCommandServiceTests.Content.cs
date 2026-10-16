// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies scene documents: decoding, saving, copies and relocations of scene files.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    private static readonly Uri GameplayInputContext = new("asset:///Content/Input/Gameplay.oimap");

    /// <summary>An opened scene takes its name from its file, whatever name the file records.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenedSceneIsNamedAfterItsFile()
    {
        using var scenes = new SceneDocumentFiles(CreateFixture().Sut);
        scenes.Write("Intro", SceneDocumentFiles.Data(SceneId.New(), "Recorded name"));

        var (document, content) = await scenes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = content.Scene.Name.Should().Be("Intro");
        _ = document.IsDirty.Should().BeFalse();
    }

    /// <summary>A file that is not a scene of the project does not open, and says why.</summary>
    /// <param name="defect">What is wrong with the file.</param>
    /// <param name="problem">What the problem names.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow("json", "not a valid scene", DisplayName = "Not JSON")]
    [DataRow("duplicate-node", "duplicate identities", DisplayName = "Duplicate node identity")]
    [DataRow("foreign-layout", "explorer layout", DisplayName = "Layout naming a foreign node")]
    public async Task FileThatIsNotASceneDoesNotOpen(string defect, string problem)
    {
        using var scenes = new SceneDocumentFiles(CreateFixture().Sut);
        var valid = SceneDocumentFiles.Data(SceneId.New(), "Broken", "A");
        switch (defect)
        {
            case "json":
                scenes.WriteText("Broken", "{ not json");
                break;
            case "duplicate-node":
                scenes.Write("Broken", valid with { RootNodes = [valid.RootNodes[0], valid.RootNodes[0]] });
                break;
            default:
                scenes.Write("Broken", valid with { ExplorerLayout = [new ExplorerEntryData { NodeId = NodeId.New() }] });
                break;
        }

        var result = await scenes.Documents.OpenAsync(scenes.FileOf("Broken"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Document.Should().BeNull();
        _ = result.Problem.Should().Contain(problem);
    }

    /// <summary>A save finishes the gesture in progress once; a late preview of that gesture changes nothing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveFinishesTheGestureOnceAndRejectsItsLatePreview()
    {
        var fixture = CreateFixture();
        using var scenes = new SceneDocumentFiles(fixture.Sut);
        scenes.Write("Main", SceneDocumentFiles.Data(SceneId.New(), "Main", "Node"));
        var (document, content) = await scenes.OpenAsync("Main", this.TestContext.CancellationToken).ConfigureAwait(false);
        var scene = content.Scene;
        var node = scene.RootNodes[0];
        var context = new SceneDocumentCommandContext(document.Id, scene, document.History);
        ConfigureTransformSessionPropertySync(fixture, scene, node, []);
        var session = EditSessionToken.Begin([node.Id], "PositionX");
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(7f), session).ConfigureAwait(false);
        _ = content.HasEditsInProgress.Should().BeTrue();

        _ = (await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = content.HasEditsInProgress.Should().BeFalse();
        _ = document.IsDirty.Should().BeFalse();
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = scenes.Read("Main").RootNodes.Single().Components.OfType<TransformData>().Single().Position.X.Should().Be(7);
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(99f), session).ConfigureAwait(false);
        session.Commit();
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(99f), session).ConfigureAwait(false);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(7);
        _ = document.IsDirty.Should().BeFalse();
        _ = document.History.UndoStack.Should().ContainSingle();
        await document.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(0);
    }

    /// <summary>A copy gets an identity of its own and the copy's name; the open scene keeps its edits unsaved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SaveCopyGivesTheCopyItsOwnIdentityAndName()
    {
        var fixture = CreateFixture();
        using var scenes = new SceneDocumentFiles(fixture.Sut);
        var id = SceneId.New();
        scenes.Write("Main", SceneDocumentFiles.Data(id, "Main"));
        var (document, content) = await scenes.OpenAsync("Main", this.TestContext.CancellationToken).ConfigureAwait(false);
        var context = new SceneDocumentCommandContext(document.Id, content.Scene, document.History);
        _ = fixture.Sync.Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), null)).Returns(Task.CompletedTask);
        _ = (await fixture.Sut.CreateNodeAsync(context, parentNodeId: null, parentFolderId: null, "Added").ConfigureAwait(false)).Succeeded.Should().BeTrue();

        _ = (await document.SaveCopyAsync(scenes.FileOf("Copy"), this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();

        var copy = scenes.Read("Copy");
        _ = copy.Id.Should().NotBe(id);
        _ = copy.Name.Should().Be("Copy");
        _ = copy.RootNodes.Should().ContainSingle().Which.Name.Should().Be("Added");
        _ = scenes.Read("Main").RootNodes.Should().BeEmpty();
        _ = document.IsDirty.Should().BeTrue();
        _ = content.Scene.Name.Should().Be("Main");
    }

    /// <summary>A scene that was closed or replaced is not saved over its file.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RetiredSceneIsNotSaved()
    {
        using var scenes = new SceneDocumentFiles(CreateFixture().Sut);
        scenes.Write("Main", SceneDocumentFiles.Data(SceneId.New(), "Main"));
        var (document, content) = await scenes.OpenAsync("Main", this.TestContext.CancellationToken).ConfigureAwait(false);
        document.History.AddChange("Edit", static () => { });
        SceneAuthoringGate.Retire(content.Scene);

        var result = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Refused);
    }

    /// <summary>
    /// An open scene follows a relocation in memory: only referencing nodes change, nothing enters history, the scene
    /// takes its file's new name, and only those nodes are named for the runtime refresh, which attaches them again.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenSceneFollowsRelocationWithoutHistory()
    {
        var fixture = CreateFixture();
        var built = CreateScene();
        AssignSlot(AddSlotGeometry(built, "Red", "Cube"), RedMaterial);
        _ = AddSlotGeometry(built, "Plain", "Sphere");
        var tab = SceneTabs.Open(built, fixture.Sut);
        var scene = tab.Scene;
        var red = scene.RootNodes.Single(node => node.Name == "Red");
        var plain = scene.RootNodes.Single(node => node.Name == "Plain");
        var context = CreateContext(scene);
        var content = (SceneContent)tab.Document.Content;
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.DocumentId)).Returns(scene);
        SceneRelocatedEventArgs? relocated = null;
        content.Relocated += (_, args) => relocated = args;
        _ = fixture.Sync.Setup(value => value.AttachGeometryAsync(scene, red, It.IsAny<CancellationToken>()))
            .ReturnsAsync(new SyncOutcome(SyncStatus.Accepted, SceneOperationKinds.EditGeometry, AffectedScope.Empty));

        content.ApplyRelocation(Relocation("C:/Levels/Renamed.oscene.json", RedMaterial, BlueMaterial));

        _ = scene.Name.Should().Be("Renamed");
        _ = relocated.Should().NotBeNull();
        _ = relocated!.GeometryNodes.Should().Equal(red.Id);
        _ = red.Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().Single().Material.Uri.Should().Be(BlueMaterial);
        _ = context.History.UndoStack.Should().BeEmpty();
        fixture.Sync.Verify(value => value.AttachGeometryAsync(scene, red, It.IsAny<CancellationToken>()), Times.Never, "the runtime keeps its view until the asset is mounted");

        await fixture.Sut.RefreshRelocatedRuntimeAsync(context, relocated.GeometryNodes, relocated.Environment).ConfigureAwait(false);

        fixture.Sync.Verify(value => value.AttachGeometryAsync(scene, red, It.IsAny<CancellationToken>()), Times.Once);
        fixture.Sync.Verify(value => value.AttachGeometryAsync(scene, plain, It.IsAny<CancellationToken>()), Times.Never);
    }

    /// <summary>An open scene's starting input contexts follow a relocation without entering history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task OpenSceneStartingInputContextsFollowRelocation()
    {
        var fixture = CreateFixture();
        var tab = SceneTabs.Open(CreateScene(), fixture.Sut);
        var scene = tab.Scene;
        var context = CreateContext(scene);
        _ = await fixture.Sut.EditScenePlayAsync(context, new ScenePlayData
        {
            StartingInputContexts = [new StartingInputContextData(GameplayInputContext, Priority: 4), new StartingInputContextData(MenuContext)],
        }).ConfigureAwait(false);
        context.History.Clear();
        var content = (SceneContent)tab.Document.Content;
        var renamed = new Uri("asset:///Content/Controls/Gameplay.oimap");

        content.ApplyRelocation(Relocation("C:/scene.oscene.json", GameplayInputContext, renamed));

        _ = scene.Play.StartingInputContexts.Should().Equal(
            new StartingInputContextData(renamed, Priority: 4),
            new StartingInputContextData(MenuContext));
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    private static DocumentRelocation Relocation(string path, Uri from, Uri to)
        => new(
            new DocumentFile(path, new Uri("asset:///Content/" + Path.GetFileName(path))),
            new FileVersion(Exists: true, new string('B', 64)),
            reference => string.Equals(reference, from.ToString(), StringComparison.Ordinal) ? to.ToString() : null);
}
