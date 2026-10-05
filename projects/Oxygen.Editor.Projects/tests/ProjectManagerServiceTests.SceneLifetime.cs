// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

public partial class ProjectManagerServiceTests
{
    [TestMethod]
    public async Task StageSceneLoadAsync_UnacceptedReadRetainsAuthoringAndSaveConflictBaseline()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        scene.Project.ActiveScene = scene;
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        await WriteExternalSceneAsync(scene, positionX: 7, this.CancellationToken).ConfigureAwait(false);

        var staged = await service.StageSceneLoadAsync(scene, this.CancellationToken).ConfigureAwait(false);

        _ = staged.Should().NotBeNull();
        _ = staged!.SceneId.Should().Be(scene.Id);
        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
        _ = scene.Project.ActiveScene.Should().BeSameAs(scene);
        var save = () => service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene));
        _ = await save.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AcceptSceneLoad_AdoptsValidatedSourceOnceAndRetirementLeavesOnlyMetadata()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var first = CreateAtomicScene(workspace.Root);
        first.Project.Scenes.Add(first);
        first.Project.ActiveScene = first;
        var second = new Scene(first.Project) { Name = "Second" };
        second.RootNodes.Add(new SceneNode(second) { Name = "Second node" });
        first.Project.Scenes.Add(second);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(second)).ConfigureAwait(false);
        var staged = await service.StageSceneLoadAsync(second, this.CancellationToken).ConfigureAwait(false);

        service.RetireScene(first);
        var accepted = service.AcceptSceneLoad(staged!);

        _ = first.Project.ActiveScene.Should().BeNull();
        _ = first.Project.Scenes.Single(scene => scene.Id == first.Id).RootNodes.Should().BeEmpty();
        _ = first.Project.Scenes.Should().NotContain(first);
        _ = accepted.RootNodes.Should().ContainSingle().Which.Name.Should().Be("Second node");
        _ = first.Project.Scenes.Single(scene => scene.Id == second.Id).Should().BeSameAs(accepted);
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(accepted)).ConfigureAwait(false)).Should().BeTrue();
        var repeat = () => service.AcceptSceneLoad(staged!);
        _ = repeat.Should().ThrowExactly<InvalidOperationException>();
    }

    [TestMethod]
    public async Task StageSceneLoadAsync_ForeignIdentityDoesNotReplaceMetadata()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var other = new Scene(scene.Project) { Name = scene.Name };
        var path = Path.Combine(workspace.Root, "Content", "Scenes", scene.Name + Constants.SceneFileExtension);
        await File.WriteAllTextAsync(path, SceneSaveSnapshot.Capture(other).Json, this.CancellationToken).ConfigureAwait(false);

        _ = (await service.StageSceneLoadAsync(scene, this.CancellationToken).ConfigureAwait(false)).Should().BeNull();

        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
    }

    [TestMethod]
    public async Task StageProjectLoadAsync_DoesNotReplaceAcceptedProjectUntilCommitted()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var first = CreateAtomicScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(first)).ConfigureAwait(false);
        _ = (await service.LoadProjectAsync(first.Project.ProjectInfo).ConfigureAwait(false)).Should().BeTrue();
        var original = service.CurrentProject;
        var nextInfo = new ProjectInfo("Next", Category.Games, workspace.Root, "preview.png");

        var staged = await service.StageProjectLoadAsync(nextInfo, this.CancellationToken).ConfigureAwait(false);

        _ = staged.Should().NotBeNull();
        _ = service.CurrentProject.Should().BeSameAs(original);
        _ = staged!.Project.Scenes.Should().OnlyContain(scene => !scene.RootNodes.Any());
        var accepted = service.AcceptProjectLoad(staged);
        _ = service.CurrentProject.Should().BeSameAs(accepted);
        _ = accepted.ProjectInfo.Id.Should().Be(nextInfo.Id);
        var repeat = () => service.AcceptProjectLoad(staged);
        _ = repeat.Should().ThrowExactly<InvalidOperationException>();
    }

    [TestMethod]
    public async Task StageProjectLoadAsync_CancelledReadRetainsAcceptedProject()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        _ = await service.LoadProjectAsync(scene.Project.ProjectInfo).ConfigureAwait(false);
        var original = service.CurrentProject;
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(false);

        var stage = () => service.StageProjectLoadAsync(scene.Project.ProjectInfo, cancellation.Token);

        _ = await stage.Should().ThrowExactlyAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = service.CurrentProject.Should().BeSameAs(original);
    }

    [TestMethod]
    public void ActiveScene_ReportsOnlyAcceptedGraphAndNotInferredMetadata()
    {
        var project = new Project(new ProjectInfo("Metadata", Category.Games, Path.GetTempPath(), "preview.png")) { Name = "Metadata" };
        var scene = new Scene(project) { Name = "Only" };
        project.Scenes.Add(scene);

        _ = project.ActiveScene.Should().BeNull();
        project.ActiveScene = scene;
        _ = project.ActiveScene.Should().BeSameAs(scene);
        project.ActiveScene = null;

        _ = project.ActiveScene.Should().BeNull();
        _ = project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
    }

    [TestMethod]
    public void RetireScene_UnrelatedRetirementKeepsExplicitlyAcceptedScene()
    {
        var project = new Project(new ProjectInfo("Metadata", Category.Games, Path.GetTempPath(), "preview.png")) { Name = "Metadata" };
        var accepted = new Scene(project) { Name = "Accepted" };
        var other = new Scene(project) { Name = "Other" };
        project.Scenes.Add(accepted);
        project.Scenes.Add(other);
        project.ActiveScene = accepted;
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));

        service.RetireScene(other);

        _ = project.ActiveScene.Should().BeSameAs(accepted);
        _ = project.Scenes.Should().NotContain(other);
    }

    [TestMethod]
    public async Task AcceptProjectLoad_RejectsStagingSupersededByAnotherAcceptedProject()
    {
        using var workspace = new SceneLifetimeWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var olderInfo = new ProjectInfo("Older", Category.Games, workspace.Root, "preview.png");
        var newerInfo = new ProjectInfo("Newer", Category.Games, workspace.Root, "preview.png");

        var older = await service.StageProjectLoadAsync(olderInfo, this.CancellationToken).ConfigureAwait(false);
        var newer = await service.StageProjectLoadAsync(newerInfo, this.CancellationToken).ConfigureAwait(false);
        _ = service.IsProjectLoadCurrent(older!).Should().BeTrue();
        var accepted = service.AcceptProjectLoad(newer!);

        _ = service.IsProjectLoadCurrent(older!).Should().BeFalse();
        _ = service.IsProjectLoadCurrent(newer!).Should().BeFalse();
        var stale = () => service.AcceptProjectLoad(older!);
        _ = stale.Should().ThrowExactly<InvalidOperationException>();
        _ = service.CurrentProject.Should().BeSameAs(accepted);
        _ = accepted.ProjectInfo.Id.Should().Be(newerInfo.Id);
    }
    private sealed class SceneLifetimeWorkspace : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateDirectory(
            Path.Combine(AppContext.BaseDirectory, "SceneLifetimeTests", Guid.NewGuid().ToString("N")));

        public string Root => this.directory.FullName;

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
