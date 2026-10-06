// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
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
    [DataRow("Lantern Demo")]
    [DataRow("atomic")]
    public async Task RenameSceneAssetAsync_PreservesIdentityContentsAndDefaultAndCanLoadAndSave(string name)
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        var data = scene.Dehydrate();
        scene.Project.ProjectInfo.DefaultSceneId = scene.Id;
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();
        var staleSave = SceneSaveSnapshot.Capture(scene);

        var result = await service.RenameSceneAssetAsync(scene, name, this.CancellationToken).ConfigureAwait(false);

        _ = result.Changed.Should().BeTrue();
        _ = scene.Name.Should().Be(name);
        _ = scene.Id.Should().Be(data.Id);
        _ = scene.Project.ProjectInfo.DefaultSceneId.Should().Be(scene.Id);
        var path = Path.Combine(workspace.Root, "Content", "Scenes", name + Constants.SceneFileExtension);
        _ = File.Exists(path).Should().BeTrue();
        _ = Directory.GetFiles(Path.GetDirectoryName(path)!, "*" + Constants.SceneFileExtension).Should().ContainSingle();
        var saved = JsonSerializer.Deserialize(await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false), SceneJsonContext.Default.SceneData)!;
        _ = saved.Should().BeEquivalentTo(data with { Name = name });
        _ = service.GetSceneSourceVersion(scene)!.SourcePath.Should().Be(path);
        var staleWrite = () => service.SaveSceneSnapshotAsync(staleSave);
        _ = await staleWrite.Should().ThrowAsync<StorageWriteConflictException>().ConfigureAwait(false);
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();
        var loaded = await service.LoadSceneAsync(scene).ConfigureAwait(false);
        _ = loaded.Should().NotBeNull();
        _ = loaded!.Id.Should().Be(data.Id);
        _ = loaded.Name.Should().Be(name);
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_RepairsIncomingExtraAssetsWithoutSavingLiveEdits()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        var dependent = Scene.CreateAndHydrate(scene.Project, new SceneData
        {
            Id = Guid.NewGuid(),
            Name = "Dependent",
            References = new() { ExtraAssets = ["/Content/Scenes/Atomic.oscene", "/Examples/Materials/wood.omat"] },
        });
        scene.Project.Scenes.Add(dependent);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(dependent)).ConfigureAwait(false);
        scene.RootNodes[0].Name = "Unsaved object edit";

        var result = await service.RenameSceneAssetAsync(scene, "Lantern Demo", this.CancellationToken).ConfigureAwait(false);

        _ = result.Sources.Should().HaveCount(2);
        var saved = JsonSerializer.Deserialize(
            await File.ReadAllTextAsync(service.GetSceneSourceVersion(dependent)!.SourcePath, this.CancellationToken).ConfigureAwait(false),
            SceneJsonContext.Default.SceneData)!;
        _ = saved.References!.ExtraAssets.Should().Equal("/Content/Scenes/Lantern Demo.oscene", "/Examples/Materials/wood.omat");
        _ = saved.Id.Should().Be(dependent.Id);
        var renamed = JsonSerializer.Deserialize(
            await File.ReadAllTextAsync(service.GetSceneSourceVersion(scene)!.SourcePath, this.CancellationToken).ConfigureAwait(false),
            SceneJsonContext.Default.SceneData)!;
        _ = renamed.RootNodes[0].Name.Should().Be("Cube");
        _ = scene.RootNodes[0].Name.Should().Be("Unsaved object edit");
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_ExistingDestinationPreservesSourceAndModel()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene)!;
        var before = await File.ReadAllTextAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false);
        var destination = Path.Combine(Path.GetDirectoryName(source.SourcePath)!, "Taken" + Constants.SceneFileExtension);
        await File.WriteAllTextAsync(destination, "Do not overwrite", this.CancellationToken).ConfigureAwait(false);

        var rename = () => service.RenameSceneAssetAsync(scene, "Taken", this.CancellationToken);

        _ = await rename.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
        _ = scene.Name.Should().Be("Atomic");
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = (await File.ReadAllTextAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Be(before);
        _ = (await File.ReadAllTextAsync(destination, this.CancellationToken).ConfigureAwait(false)).Should().Be("Do not overwrite");
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_WriteFailureRollsBackRepairedReferences()
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: store);
        var scene = CreateRenameScene(workspace.Root);
        var dependent = Scene.CreateAndHydrate(scene.Project, new SceneData
        {
            Id = Guid.NewGuid(),
            Name = "Dependent",
            References = new() { ExtraAssets = ["/Content/Scenes/Atomic.oscene"] },
        });
        scene.Project.Scenes.Add(dependent);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(dependent)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene)!;
        var dependencySource = service.GetSceneSourceVersion(dependent)!;
        var before = await File.ReadAllTextAsync(dependencySource.SourcePath, this.CancellationToken).ConfigureAwait(false);
        store.FailOncePath = source.SourcePath;

        var rename = () => service.RenameSceneAssetAsync(scene, "Demo", this.CancellationToken);

        _ = await rename.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(dependencySource.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Be(before);
        _ = service.GetSceneSourceVersion(dependent).Should().Be(dependencySource);
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = scene.Name.Should().Be("Atomic");
        _ = Directory.GetFiles(Path.GetDirectoryName(source.SourcePath)!, "*.tmp").Should().BeEmpty();
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_MoveFailureRestoresSourceAndReferences()
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: store);
        var scene = CreateRenameScene(workspace.Root);
        var dependent = Scene.CreateAndHydrate(scene.Project, new SceneData
        {
            Id = Guid.NewGuid(),
            Name = "Dependent",
            References = new() { ExtraAssets = ["/Content/Scenes/Atomic.oscene"] },
        });
        scene.Project.Scenes.Add(dependent);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(dependent)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene)!;
        var dependency = service.GetSceneSourceVersion(dependent)!;
        var original = await File.ReadAllBytesAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false);
        var originalDependency = await File.ReadAllBytesAsync(dependency.SourcePath, this.CancellationToken).ConfigureAwait(false);
        var destination = Path.Combine(Path.GetDirectoryName(source.SourcePath)!, "Demo" + Constants.SceneFileExtension);
        store.AfterWrite = path =>
        {
            if (string.Equals(path, source.SourcePath, StringComparison.OrdinalIgnoreCase))
            {
                store.AfterWrite = null;
                File.WriteAllText(destination, "Another writer's asset");
            }
        };

        var rename = () => service.RenameSceneAssetAsync(scene, "Demo", this.CancellationToken);

        _ = await rename.Should().ThrowAsync<TargetExistsException>().ConfigureAwait(false);
        _ = scene.Name.Should().Be("Atomic");
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = service.GetSceneSourceVersion(dependent).Should().Be(dependency);
        _ = (await File.ReadAllBytesAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Equal(original);
        _ = (await File.ReadAllBytesAsync(dependency.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Equal(originalDependency);
        _ = (await File.ReadAllTextAsync(destination, this.CancellationToken).ConfigureAwait(false)).Should().Be("Another writer's asset");
        _ = Directory.GetFiles(Path.GetDirectoryName(source.SourcePath)!, "*.tmp").Should().BeEmpty();
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_ExternalReferenceEditIsNotOverwritten()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        var dependent = Scene.CreateAndHydrate(scene.Project, new SceneData
        {
            Id = Guid.NewGuid(),
            Name = "Dependent",
            References = new() { ExtraAssets = ["/Content/Scenes/Atomic.oscene"] },
        });
        scene.Project.Scenes.Add(dependent);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(dependent)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene)!;
        var dependency = service.GetSceneSourceVersion(dependent)!;
        var external = (await File.ReadAllTextAsync(dependency.SourcePath, this.CancellationToken).ConfigureAwait(false)) + "\n ";
        await File.WriteAllTextAsync(dependency.SourcePath, external, this.CancellationToken).ConfigureAwait(false);

        var rename = () => service.RenameSceneAssetAsync(scene, "Demo", this.CancellationToken);

        _ = await rename.Should().ThrowAsync<StorageWriteConflictException>().ConfigureAwait(false);
        _ = scene.Name.Should().Be("Atomic");
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = service.GetSceneSourceVersion(dependent).Should().Be(dependency);
        _ = (await File.ReadAllTextAsync(dependency.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Be(external);
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_CancelledPlanningPreservesSource()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene);
        using var cancellation = new CancellationTokenSource();
        await cancellation.CancelAsync().ConfigureAwait(false);

        var rename = () => service.RenameSceneAssetAsync(scene, "Demo", cancellation.Token);

        _ = await rename.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = scene.Name.Should().Be("Atomic");
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_ExternalSourceEditIsNotOverwritten()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene)!;
        var external = (await File.ReadAllTextAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false)) + "\n ";
        await File.WriteAllTextAsync(source.SourcePath, external, this.CancellationToken).ConfigureAwait(false);

        var rename = () => service.RenameSceneAssetAsync(scene, "Demo", this.CancellationToken);

        _ = await rename.Should().ThrowAsync<StorageWriteConflictException>().ConfigureAwait(false);
        _ = scene.Name.Should().Be("Atomic");
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
        _ = (await File.ReadAllTextAsync(source.SourcePath, this.CancellationToken).ConfigureAwait(false)).Should().Be(external);
    }

    [TestMethod]
    [DataRow("")]
    [DataRow(" ")]
    [DataRow("../Other")]
    [DataRow("Other\\Scene")]
    [DataRow("Invalid:Scene")]
    [DataRow("CON")]
    [DataRow("LPT1.test")]
    [DataRow("Trailing.")]
    public async Task RenameSceneAssetAsync_InvalidNameDoesNotChangeFiles(string name)
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateRenameScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var source = service.GetSceneSourceVersion(scene);

        var rename = () => service.RenameSceneAssetAsync(scene, name, this.CancellationToken);

        _ = await rename.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = scene.Name.Should().Be("Atomic");
        _ = service.GetSceneSourceVersion(scene).Should().Be(source);
    }

    [TestMethod]
    public async Task RenameSceneAssetAsync_SameNameDoesNotWriteOrCreateHistoryData()
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: store);
        var scene = CreateRenameScene(workspace.Root);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        store.Fail = true;

        var result = await service.RenameSceneAssetAsync(scene, " Atomic ", this.CancellationToken).ConfigureAwait(false);

        _ = result.Changed.Should().BeFalse();
        _ = result.Sources.Should().BeEmpty();
    }

    private static Scene CreateRenameScene(string root)
    {
        var scene = CreateAtomicScene(root);
        scene.Project.ProjectInfo.AuthoringMounts.Add(new("Content", "Content"));
        scene.Project.Scenes.Add(scene);
        return scene;
    }
}
