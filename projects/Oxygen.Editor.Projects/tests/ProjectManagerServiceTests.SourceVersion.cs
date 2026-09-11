// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Qualifies acknowledged scene source identity independently of live names and external reads.</summary>
public partial class ProjectManagerServiceTests
{
    /// <summary>Keeps the saved source unchanged until a renamed scene is successfully persisted.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneSourceVersionChangesOnlyAfterSuccessfulSave()
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: store);
        var scene = CreateAtomicScene(workspace.Root);
        _ = service.GetSceneSourceVersion(scene).Should().BeNull();
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var saved = service.GetSceneSourceVersion(scene);
        _ = saved.Should().NotBeNull();

        scene.Name = "Renamed";
        _ = service.GetSceneSourceVersion(scene).Should().Be(saved);
        store.Fail = true;
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeFalse();
        _ = service.GetSceneSourceVersion(scene).Should().Be(saved);

        store.Fail = false;
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();
        var renamed = service.GetSceneSourceVersion(scene)!;
        _ = renamed.SourcePath.Should().Be(Path.Combine(workspace.Root, "Content", "Scenes", "Renamed.oscene.json"));
        _ = renamed.Version.Should().NotBe(saved!.Version);
    }

    /// <summary>Preserves a loaded source baseline until an externally changed reload is accepted.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneSourceVersionTracksAcceptedLoadsAndReloads()
    {
        using var workspace = new AtomicWorkspace();
        var storage = new NativeStorageProvider(new RealFileSystem());
        var writer = new ProjectManagerService(storage);
        var source = CreateAtomicScene(workspace.Root);
        source.Project.Scenes.Add(source);
        _ = await writer.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(source)).ConfigureAwait(false);
        var service = new ProjectManagerService(storage);
        var scene = (await service.LoadSceneAsync(source).ConfigureAwait(false))!;
        var loaded = service.GetSceneSourceVersion(scene);
        _ = loaded.Should().Be(writer.GetSceneSourceVersion(source));
        await WriteExternalSceneAsync(scene, positionX: 4, this.CancellationToken).ConfigureAwait(false);

        var read = await service.ReadSceneForReloadAsync(scene, this.CancellationToken).ConfigureAwait(false);
        _ = service.GetSceneSourceVersion(scene).Should().Be(loaded);
        var replacement = service.AcceptSceneReload(read!);

        _ = service.GetSceneSourceVersion(replacement)!.Version.Should().NotBe(loaded!.Version);
    }

    /// <summary>Separates copies of the same scene identity stored in different projects.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneSourceVersionIsScopedToItsProjectRoot()
    {
        using var firstWorkspace = new AtomicWorkspace();
        using var secondWorkspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var first = CreateAtomicScene(firstWorkspace.Root);
        var secondProject = CreateAtomicScene(secondWorkspace.Root).Project;
        var second = Scene.CreateAndHydrate(secondProject, first.Dehydrate());
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(first)).ConfigureAwait(false);
        var original = service.GetSceneSourceVersion(first);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(second)).ConfigureAwait(false);

        _ = service.GetSceneSourceVersion(first).Should().Be(original);
        _ = service.GetSceneSourceVersion(second)!.SourcePath.Should().NotBe(original!.SourcePath);
    }
}
