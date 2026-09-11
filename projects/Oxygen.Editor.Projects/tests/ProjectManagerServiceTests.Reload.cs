// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Checks that reading a possible reload does not implicitly discard authoring or adopt disk changes.</summary>
public partial class ProjectManagerServiceTests
{
    [TestMethod]
    public async Task UnacceptedReload_PreservesTheModelAndOriginalConflictBaseline()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        scene.Project.ActiveScene = scene;
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        await WriteExternalSceneAsync(scene, positionX: 4, this.CancellationToken).ConfigureAwait(false);

        var read = await service.ReadSceneForReloadAsync(scene).ConfigureAwait(false);

        _ = read.Should().NotBeNull();
        _ = read!.Scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(4);
        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
        _ = scene.Project.ActiveScene.Should().BeSameAs(scene);
        scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(9, 0, 0);
        var save = () => service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene));
        _ = await save.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
        _ = scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(9);
    }

    [TestMethod]
    public async Task AcceptedReload_ReplacesTheOriginalOnceAndAdoptsItsBaseline()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        scene.Project.ActiveScene = scene;
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        await WriteExternalSceneAsync(scene, positionX: 4, this.CancellationToken).ConfigureAwait(false);
        var read = await service.ReadSceneForReloadAsync(scene).ConfigureAwait(false);

        var replacement = service.AcceptSceneReload(read!);

        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(replacement);
        _ = scene.Project.ActiveScene.Should().BeSameAs(replacement);
        _ = replacement.Id.Should().Be(scene.Id);
        _ = replacement.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition.X.Should().Be(4);
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(replacement)).ConfigureAwait(false)).Should().BeTrue();
        var repeat = () => service.AcceptSceneReload(read!);
        _ = repeat.Should().ThrowExactly<InvalidOperationException>();
    }

    [TestMethod]
    public async Task ReloadWithDifferentAssetIdentity_PreservesTheOriginalModelAndBaseline()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var foreign = new Scene(scene.Project) { Name = scene.Name };
        var path = Path.Combine(workspace.Root, "Content", "Scenes", scene.Name + Constants.SceneFileExtension);
        await File.WriteAllTextAsync(path, SceneSaveSnapshot.Capture(foreign).Json, this.CancellationToken).ConfigureAwait(false);

        _ = (await service.ReadSceneForReloadAsync(scene).ConfigureAwait(false)).Should().BeNull();

        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
        var save = () => service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene));
        _ = await save.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);
    }

    [TestMethod]
    public async Task AnotherProjectServiceCannotAcceptAnOwnedReload()
    {
        using var workspace = new AtomicWorkspace();
        var storage = new NativeStorageProvider(new RealFileSystem());
        var service = new ProjectManagerService(storage);
        var other = new ProjectManagerService(storage);
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var read = await service.ReadSceneForReloadAsync(scene).ConfigureAwait(false);

        var accept = () => other.AcceptSceneReload(read!);

        _ = accept.Should().ThrowExactly<InvalidOperationException>();
        _ = scene.Project.Scenes.Should().ContainSingle().Which.Should().BeSameAs(scene);
    }

    private static Task WriteExternalSceneAsync(Scene scene, float positionX, CancellationToken cancellationToken)
    {
        var external = Scene.CreateAndHydrate(scene.Project, scene.Dehydrate());
        external.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(positionX, 0, 0);
        var path = Path.Combine(scene.Project.ProjectInfo.Location!, "Content", "Scenes", scene.Name + Constants.SceneFileExtension);
        return File.WriteAllTextAsync(path, SceneSaveSnapshot.Capture(external).Json, cancellationToken);
    }
}
