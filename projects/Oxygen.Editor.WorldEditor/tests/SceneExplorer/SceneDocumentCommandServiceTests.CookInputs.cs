// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Qualifies saved scene reads against save, replacement, and gesture ownership.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>Waits for a scene save before reading its acknowledged source version.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneCookReadWaitsForSaveAcknowledgement()
    {
        var writing = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var saved = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        var manager = new Mock<IProjectManagerService>();
        _ = manager.Setup(value => value.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()))
            .Callback(writing.SetResult).Returns(saved.Task);
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        context.Metadata.IsDirty = true;
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.Metadata)).Returns(scene);
        var source = SavedSource(scene);
        _ = manager.Setup(value => value.GetSceneSourceVersion(scene)).Returns(source);

        var save = fixture.Sut.SaveSceneAsync(context);
        await writing.Task.WaitAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var read = fixture.Sut.AcquireCookReadAsync(context, this.TestContext.CancellationToken);
        _ = read.IsCompleted.Should().BeFalse();
        saved.SetResult(true);
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        using var lease = await read.ConfigureAwait(false);

        _ = lease.Should().NotBeNull();
        _ = lease!.State.SavedRevision.Should().Be(context.Metadata.ChangeVersion);
        _ = lease.State.SavedContentHash.Should().Be(source.Version.Sha256);
        _ = lease.State.IsDirty.Should().BeFalse();
    }

    /// <summary>Drains a cook read before saving or replacing the captured scene model.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneCookReadBlocksSaveAndReplacementUntilReleased()
    {
        var manager = new Mock<IProjectManagerService>();
        _ = manager.Setup(value => value.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>())).ReturnsAsync(value: true);
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.Metadata)).Returns(scene);
        _ = manager.Setup(value => value.GetSceneSourceVersion(scene)).Returns(SavedSource(scene));
        using var lease = await fixture.Sut.AcquireCookReadAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = lease.Should().NotBeNull();
        var save = fixture.Sut.SaveSceneAsync(context);
        var replace = SceneAuthoringGate.BeginReplacementAsync(scene, this.TestContext.CancellationToken);
        _ = save.IsCompleted.Should().BeFalse();
        _ = replace.IsCompleted.Should().BeFalse();
        manager.Verify(value => value.SaveSceneSnapshotAsync(It.IsAny<SceneSaveSnapshot>()), Times.Never());

        lease!.Dispose();
        _ = (await save.ConfigureAwait(false)).Succeeded.Should().BeTrue();
        using var replacement = await replace.ConfigureAwait(false);
        _ = replacement.Should().NotBeNull();
    }

    /// <summary>Reports unsaved scene previews without closing their gesture or adding an undo entry.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneCookReadDetectsPreviewWithoutCommittingIt()
    {
        var manager = new Mock<IProjectManagerService>();
        var fixture = CreateFixture(projectManager: manager.Object);
        var scene = CreateSaveScene();
        var node = scene.RootNodes[0];
        var context = CreateContext(scene);
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.Metadata)).Returns(scene);
        _ = manager.Setup(value => value.GetSceneSourceVersion(scene)).Returns(SavedSource(scene));
        ConfigureTransformSessionPropertySync(fixture, scene, node, []);
        _ = fixture.Sync.Setup(value => value.CancelPreviewSyncAsync(
                scene.Id, node.Id, It.IsAny<Func<CancellationToken, Task<SyncOutcome>>>(), It.IsAny<CancellationToken>()))
            .Returns((Guid _, Guid _, Func<CancellationToken, Task<SyncOutcome>> apply, CancellationToken token) => apply(token));
        var session = EditSessionToken.Begin(SceneOperationKinds.EditTransform, [node.Id], "PositionX");
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(7), session).ConfigureAwait(false);

        using var lease = await fixture.Sut.AcquireCookReadAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = lease.Should().NotBeNull();
        _ = lease!.State.IsDirty.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        session.Cancel();
        _ = await fixture.Sut.EditTransformAsync(context, [node.Id], PositionXEdit(0), session).ConfigureAwait(false);
        _ = node.Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(0);
        _ = context.History.UndoStack.Should().BeEmpty();
    }

    /// <summary>Does not hand a read lease to a callback for a replaced scene.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneCookReadRejectsReplacedScene()
    {
        var fixture = CreateFixture();
        var scene = CreateSaveScene();
        var context = CreateContext(scene);
        _ = fixture.Sync.Setup(value => value.GetDocumentScene(context.Metadata)).Returns(new Scene(scene.Project) { Name = "Replacement" });

        using var lease = await fixture.Sut.AcquireCookReadAsync(context, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = lease.Should().BeNull();
        using var replacement = await SceneAuthoringGate.BeginReplacementAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = replacement.Should().NotBeNull();
    }

    private static SceneSourceVersion SavedSource(Scene scene)
        => new(Path.Combine("H:/SceneSaveTests/Content/Scenes", scene.Name + ".oscene.json"), new FileVersion(Exists: true, new string('A', 64)));
}
