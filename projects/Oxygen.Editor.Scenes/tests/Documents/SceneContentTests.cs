// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Scenes.Tests.Documents;

/// <summary>
/// A scene document's content through the document service (scene-edits.md 3.1 and 3.13): the authoring records into
/// the document's history, follows the document's reloads and closes, and its gestures hold the document's edits.
/// </summary>
[TestClass]
public sealed class SceneContentTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task Decode_AuthoringRecordsIntoTheContextHistory()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);

        var result = await SceneContent.Of(document).Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = SceneContent.Of(document).Authoring.Document.Should().Be(document.Id);
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = document.IsDirty.Should().BeTrue();
    }

    [TestMethod]
    public async Task Of_ReturnsTheDocumentsContent()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);

        var content = SceneContent.Of(document);

        _ = content.Should().BeSameAs(document.Content);
    }

    [TestMethod]
    public async Task HasEditsInProgress_FollowsTheOpenGesture()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var content = SceneContent.Of(await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false));
        var gesture = await BeginMoveOfAAsync(content).ConfigureAwait(false);

        _ = await gesture.PreviewAsync(MoveA(5f)).ConfigureAwait(false);
        var whileMoved = content.HasEditsInProgress;
        _ = await gesture.CommitAsync().ConfigureAwait(false);

        _ = whileMoved.Should().BeTrue("a previewed value is an edit in progress");
        _ = content.HasEditsInProgress.Should().BeFalse("a committed gesture is no longer in progress");
    }

    [TestMethod]
    public async Task FinishEdits_CommitsTheOpenGesture()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var content = SceneContent.Of(document);
        var gesture = await BeginMoveOfAAsync(content).ConfigureAwait(false);
        _ = await gesture.PreviewAsync(MoveA(5f)).ConfigureAwait(false);

        await content.FinishEditsAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = gesture.IsEnded.Should().BeTrue();
        _ = content.HasEditsInProgress.Should().BeFalse();
        _ = document.History.UndoStack.Should().ContainSingle();
        _ = PositionXOf(content, SampleScene.A).Should().Be(5f);
    }

    [TestMethod]
    public async Task Reload_RetiresTheOldAuthoring()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var old = SceneContent.Of(document).Authoring;

        var reloaded = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reloaded.Succeeded.Should().BeTrue(reloaded.Problem);
        _ = old.IsRetired.Should().BeTrue();
        SceneAssert.Retired(await old.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false));
    }

    [TestMethod]
    public async Task Reload_NewAuthoringRecordsIntoTheSameClearedHistory()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await SceneContent.Of(document).Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        var recorders = new List<ChangeRecorder>();
        document.ContentReplaced += (_, args) => recorders.Add(new ChangeRecorder(((SceneContent)args.Current).Authoring));

        _ = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        using var changes = recorders.Single();
        var authoring = SceneContent.Of(document).Authoring;
        _ = authoring.Document.Should().Be(document.Id);
        _ = document.History.UndoStack.Should().BeEmpty("a reload clears the document's history");
        _ = changes.Count.Should().Be(0, "the new authoring raises nothing on creation");
        SceneAssert.Applied(await authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false));
        _ = document.History.UndoStack.Should().ContainSingle("the new authoring records into the document's history");
    }

    [TestMethod]
    public async Task Reload_CancelsTheOpenGesture()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var gesture = await BeginMoveOfAAsync(SceneContent.Of(document)).ConfigureAwait(false);

        var reloaded = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reloaded.Succeeded.Should().BeTrue(reloaded.Problem);
        _ = gesture.IsEnded.Should().BeTrue("disposing the replaced content cancels its gestures");
    }

    [TestMethod]
    public async Task Close_RetiresTheAuthoring()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        var authoring = SceneContent.Of(document).Authoring;
        _ = await authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);

        documents.Open.Close(document);

        _ = authoring.IsRetired.Should().BeTrue();
        SceneAssert.Retired(await authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Crate")).ConfigureAwait(false));
        SceneAssert.Retired(await authoring.UndoAsync().ConfigureAwait(false));
    }

    [TestMethod]
    public async Task EditedScene_SavesAndReopensEqual()
    {
        using var documents = new TestSceneDocuments(TestProject.Create(new MockFileSystem()));
        var document = await documents.OpenAsync(SampleScene.Build()).ConfigureAwait(false);
        _ = await SceneContent.Of(document).Authoring.ExecuteAsync(new RenameNode(SampleScene.B, "Box")).ConfigureAwait(false);
        var saved = await document.SaveAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var edited = StateOf(document);
        var file = document.File;
        documents.Open.Close(document);

        var reopened = await documents.OpenAsync(file).ConfigureAwait(false);

        _ = saved.Succeeded.Should().BeTrue(saved.Message);
        _ = StateOf(reopened).Should().Be(edited);
    }

    private static Task<SceneGesture> BeginMoveOfAAsync(SceneContent content)
        => content.Authoring.BeginGestureAsync([SampleScene.A], [SceneSchema.Transform.PositionX.Id], "Move");

    private static SetProperties MoveA(float x)
        => new(new Dictionary<NodeId, PropertyEdit> { [SampleScene.A] = PropertyEdit.SingleEdit(SceneSchema.Transform.PositionX, x) }, "Move");

    private static float PositionXOf(SceneContent content, NodeId node)
        => content.Scene.AllNodes.Single(candidate => candidate.Id == node).Transform.LocalPosition.X;

    private static string StateOf(Document document)
        => JsonSerializer.Serialize(
            new SceneFileData(SceneContent.Of(document).Scene.Dehydrate(), SceneContent.Of(document).Authoring.Layout),
            SceneFileJsonContext.Default.SceneFileData);
}
