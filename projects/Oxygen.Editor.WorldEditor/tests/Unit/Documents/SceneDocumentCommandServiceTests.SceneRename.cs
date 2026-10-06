// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;
using Constants = Oxygen.Editor.Projects.Constants;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RenameSceneAsync_UndoRedoMovesFileAndPreservesDirtyEdits(bool dirty)
    {
        using var workspace = new RenameWorkspace();
        var scene = workspace.Scene;
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var context = CreateContext(scene);
        if (dirty)
        {
            scene.RootNodes[0].Name = "Unsaved";
            context.Metadata.IsDirty = true;
        }

        var fixture = CreateFixture(projectManager: workspace.Manager);
        var oldPath = workspace.Manager.GetSceneSourceVersion(scene)!.SourcePath;
        var renamedPath = Path.Combine(Path.GetDirectoryName(oldPath)!, "Lantern Demo" + Constants.SceneFileExtension);

        var result = await fixture.Sut.RenameSceneAsync(context, "Lantern Demo").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = context.Metadata.Title.Should().Be("Lantern Demo");
        _ = context.Metadata.IsDirty.Should().Be(dirty);
        _ = scene.Name.Should().Be("Lantern Demo");
        _ = File.Exists(oldPath).Should().BeFalse();
        _ = File.Exists(renamedPath).Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = scene.References.ExtraAssets.Should().Equal("/Content/Scenes/Lantern Demo.oscene");
        _ = scene.Project.ProjectInfo.DefaultSceneId.Should().Be(scene.Id);

        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.Name.Should().Be("Main");
        _ = context.Metadata.Title.Should().Be("Main");
        _ = context.Metadata.IsDirty.Should().Be(dirty);
        _ = File.Exists(oldPath).Should().BeTrue();
        _ = File.Exists(renamedPath).Should().BeFalse();
        _ = context.History.RedoStack.Should().ContainSingle();
        _ = scene.References.ExtraAssets.Should().Equal("/Content/Scenes/Main.oscene");

        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = scene.Name.Should().Be("Lantern Demo");
        _ = context.Metadata.Title.Should().Be("Lantern Demo");
        _ = context.Metadata.IsDirty.Should().Be(dirty);
        _ = File.Exists(oldPath).Should().BeFalse();
        _ = File.Exists(renamedPath).Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = context.History.RedoStack.Should().BeEmpty();
        _ = scene.RootNodes[0].Name.Should().Be(dirty ? "Unsaved" : "Lantern");
        var saved = JsonSerializer.Deserialize(
            await File.ReadAllTextAsync(renamedPath, this.TestContext.CancellationToken).ConfigureAwait(false),
            SceneJsonContext.Default.SceneData)!;
        _ = saved.RootNodes[0].Name.Should().Be("Lantern");
        _ = saved.References!.ExtraAssets.Should().Equal("/Content/Scenes/Lantern Demo.oscene");
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RenameSceneAsync_RealCookRegistrationFollowsRenameUndoRedo(bool dirty)
    {
        using var workspace = new RenameWorkspace();
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(workspace.Scene)).ConfigureAwait(false);
        var context = CreateContext(workspace.Scene);
        context.Metadata.IsDirty = dirty;
        var fixture = CreateFixture(projectManager: workspace.Manager);
        _ = fixture.DocumentService.Setup(service => service.UpdateMetadataAsync(
            It.IsAny<Microsoft.UI.WindowId>(), context.DocumentId, context.Metadata))
            .Callback(() => fixture.DocumentService.Raise(
                service => service.DocumentMetadataChanged += null,
                new DocumentMetadataChangedEventArgs(default, context.Metadata)))
            .ReturnsAsync(true);
        var registry = new CookDocumentRegistry();
        var registrar = new SceneCookInputRegistrar(
            registry,
            workspace.Manager,
            new HostingContext { Application = null!, Dispatcher = null!, DispatcherScheduler = null! },
            fixture.DocumentService.Object);
        using var registration = registrar.Register(context, fixture.Sut);

        _ = (await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false)).Succeeded.Should().BeTrue();
        AssertState("Demo");
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertState("Main");
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertState("Demo");
        _ = fixture.Results.Published.Should().BeEmpty();

        void AssertState(string name)
        {
            var state = registry.GetState().Documents.Should().ContainSingle().Which;
            _ = state.DocumentId.Should().Be(context.DocumentId);
            _ = state.DisplayName.Should().Be(name);
            _ = state.SourcePath.Should().Be(workspace.Manager.GetSceneSourceVersion(workspace.Scene)!.SourcePath);
            _ = state.IsDirty.Should().Be(dirty);
            _ = File.Exists(state.SourcePath).Should().BeTrue();
        }
    }

    [TestMethod]
    public async Task RenameSceneAsync_EditArrivingDuringRenameRemainsUnsaved()
    {
        using var workspace = new RenameWorkspace();
        var scene = workspace.Scene;
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var context = CreateContext(scene);
        var manager = new Mock<IProjectManagerService>(MockBehavior.Strict);
        _ = manager.Setup(service => service.RenameSceneAssetAsync(scene, "Demo", It.IsAny<CancellationToken>()))
            .Returns<Scene, string, CancellationToken>(async (target, name, cancellationToken) =>
            {
                await Task.Yield();
                scene.RootNodes[0].Name = "New edit";
                context.Metadata.IsDirty = true;
                return await workspace.Manager.RenameSceneAssetAsync(target, name, cancellationToken).ConfigureAwait(false);
            });
        var fixture = CreateFixture(projectManager: manager.Object);

        var result = await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.HasUnsavedChanges.Should().BeTrue();
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = scene.RootNodes[0].Name.Should().Be("New edit");
        var saved = JsonSerializer.Deserialize(
            await File.ReadAllTextAsync(workspace.Manager.GetSceneSourceVersion(scene)!.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false),
            SceneJsonContext.Default.SceneData)!;
        _ = saved.RootNodes[0].Name.Should().Be("Lantern");
    }

    [TestMethod]
    [DataRow("{broken")]
    [DataRow("{\"Id\":\"not-a-guid\"}")]
    [DataRow("[]")]
    public async Task RenameSceneAsync_MalformedProjectScenePublishesFailureWithoutRenaming(string contents)
    {
        using var workspace = new RenameWorkspace();
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(workspace.Scene)).ConfigureAwait(false);
        var source = workspace.Manager.GetSceneSourceVersion(workspace.Scene)!;
        var malformedPath = Path.Combine(Path.GetDirectoryName(source.SourcePath)!, "Malformed" + Constants.SceneFileExtension);
        await File.WriteAllTextAsync(malformedPath, contents, this.TestContext.CancellationToken).ConfigureAwait(false);
        var context = CreateContext(workspace.Scene);
        var fixture = CreateFixture(projectManager: workspace.Manager);

        var result = await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = context.Metadata.Title.Should().Be("Main");
        _ = context.Metadata.IsDirty.Should().BeFalse();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = workspace.Manager.GetSceneSourceVersion(workspace.Scene).Should().Be(source);
        _ = File.Exists(source.SourcePath).Should().BeTrue();
        _ = fixture.Results.Published.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(Oxygen.Managed.Core.Diagnostics.DiagnosticCodes.DocumentPrefix + "RENAME_FAILED");
        _ = (await File.ReadAllTextAsync(malformedPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(contents);
    }

    [TestMethod]
    public async Task RenameSceneAsync_RejectedRenameLeavesTitleDirtyStateAndHistoryUnchanged()
    {
        using var workspace = new RenameWorkspace();
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(workspace.Scene)).ConfigureAwait(false);
        var source = workspace.Manager.GetSceneSourceVersion(workspace.Scene)!;
        var context = CreateContext(workspace.Scene);
        context.Metadata.IsDirty = true;
        var fixture = CreateFixture(projectManager: workspace.Manager);

        var result = await fixture.Sut.RenameSceneAsync(context, "CON").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = context.Metadata.Title.Should().Be("Main");
        _ = context.Metadata.IsDirty.Should().BeTrue();
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.History.RedoStack.Should().BeEmpty();
        _ = workspace.Manager.GetSceneSourceVersion(workspace.Scene).Should().Be(source);
        _ = File.Exists(source.SourcePath).Should().BeTrue();
        _ = fixture.Results.Published.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(Oxygen.Managed.Core.Diagnostics.DiagnosticCodes.DocumentPrefix + "RENAME_FAILED");
    }

    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task RenameSceneAsync_TabRefreshFailureReportsCommittedRenameWithUndo(bool throws)
    {
        using var workspace = new RenameWorkspace();
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(workspace.Scene)).ConfigureAwait(false);
        var context = CreateContext(workspace.Scene);
        var fixture = CreateFixture(projectManager: workspace.Manager);
        var refresh = fixture.DocumentService.Setup(service => service.UpdateMetadataAsync(
            It.IsAny<Microsoft.UI.WindowId>(), It.IsAny<Guid>(), It.IsAny<DroidNet.Documents.IDocumentMetadata>()));
        if (throws)
        {
            _ = refresh.ThrowsAsync(new InvalidOperationException("Controlled tab refresh failure"));
        }
        else
        {
            _ = refresh.ReturnsAsync(false);
        }

        var result = await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.OperationResultId.Should().NotBeNull();
        _ = workspace.Scene.Name.Should().Be("Demo");
        _ = context.Metadata.Title.Should().Be("Demo");
        _ = File.Exists(workspace.Manager.GetSceneSourceVersion(workspace.Scene)!.SourcePath).Should().BeTrue();
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Results.Published.Should().ContainSingle().Which.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(Oxygen.Managed.Core.Diagnostics.DiagnosticCodes.DocumentPrefix + "RENAME_REFRESH_FAILED");
        _ = refresh.ReturnsAsync(true);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Scene.Name.Should().Be("Main");
    }

    [TestMethod]
    public async Task RenameSceneAsync_FailedUndoRetainsRenameStepAndCanBeRetried()
    {
        using var workspace = new RenameWorkspace();
        var scene = workspace.Scene;
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var oldPath = workspace.Manager.GetSceneSourceVersion(scene)!.SourcePath;
        var context = CreateContext(scene);
        var fixture = CreateFixture(projectManager: workspace.Manager);
        _ = (await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false)).Succeeded.Should().BeTrue();
        await File.WriteAllTextAsync(oldPath, "Another asset", this.TestContext.CancellationToken).ConfigureAwait(false);

        Func<Task> undo = async () => await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await undo.Should().ThrowAsync<DroidNet.Storage.TargetExistsException>().ConfigureAwait(false);

        _ = scene.Name.Should().Be("Demo");
        _ = context.Metadata.Title.Should().Be("Demo");
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = context.History.RedoStack.Should().BeEmpty();
        _ = fixture.Results.Published.Should().ContainSingle().Which.OperationKind.Should().Be(Oxygen.Managed.Core.Diagnostics.SceneOperationKinds.Rename);
        File.Delete(oldPath);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Name.Should().Be("Main");
        _ = context.History.RedoStack.Should().ContainSingle();
    }

    [TestMethod]
    public async Task RenameSceneAsync_FailedRedoRetainsStepAndCanBeRetried()
    {
        using var workspace = new RenameWorkspace();
        var scene = workspace.Scene;
        _ = await workspace.Manager.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var path = workspace.Manager.GetSceneSourceVersion(scene)!.SourcePath;
        var destination = Path.Combine(Path.GetDirectoryName(path)!, "Demo" + Constants.SceneFileExtension);
        var context = CreateContext(scene);
        var fixture = CreateFixture(projectManager: workspace.Manager);
        _ = await fixture.Sut.RenameSceneAsync(context, "Demo").ConfigureAwait(false);
        await context.History.UndoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(destination, "Another asset", this.TestContext.CancellationToken).ConfigureAwait(false);

        Func<Task> redo = async () => await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await redo.Should().ThrowAsync<DroidNet.Storage.TargetExistsException>().ConfigureAwait(false);

        _ = scene.Name.Should().Be("Main");
        _ = context.History.UndoStack.Should().BeEmpty();
        _ = context.History.RedoStack.Should().ContainSingle();
        File.Delete(destination);
        await context.History.RedoAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = scene.Name.Should().Be("Demo");
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    private sealed partial class RenameWorkspace : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("OxygenSceneRename-");

        public RenameWorkspace()
        {
            var info = new ProjectInfo("Rename", Category.Games, this.directory.FullName, "preview.png");
            info.AuthoringMounts.Add(new("Content", "Content"));
            var project = new Project(info) { Name = "Rename" };
            this.Scene = Scene.CreateAndHydrate(project, new SceneData
            {
                Id = Guid.NewGuid(),
                Name = "Main",
                RootNodes = [new() { Id = Guid.NewGuid(), Name = "Lantern", Components = [new TransformData { Name = "Transform" }] }],
                References = new() { ExtraAssets = ["/Content/Scenes/Main.oscene"] },
            });
            project.Scenes.Add(this.Scene);
            info.DefaultSceneId = this.Scene.Id;
            this.Manager = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        }

        public Scene Scene { get; }

        public ProjectManagerService Manager { get; }

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
