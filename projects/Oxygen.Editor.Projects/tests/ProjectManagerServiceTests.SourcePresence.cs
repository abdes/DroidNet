// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.World.Serialization;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies that an open scene survives its file being deleted or moved outside the editor.</summary>
public partial class ProjectManagerServiceTests
{
    /// <summary>A deleted scene file is reported missing, and saving the scene writes it again.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task DeletedSceneSourceIsMissingUntilSavingWritesItAgain()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var path = service.GetSceneSourceVersion(scene)!.SourcePath;
        File.Delete(path);

        _ = (await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();

        _ = File.Exists(path).Should().BeTrue();
        _ = (await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
    }

    /// <summary>A scene file restored unchanged is in step with the scene again, so later saves succeed normally.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneSourceRestoredUnchangedIsInStepAgain()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var path = service.GetSceneSourceVersion(scene)!.SourcePath;
        var original = await File.ReadAllBytesAsync(path, this.CancellationToken).ConfigureAwait(false);
        File.Delete(path);
        _ = await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false);
        await File.WriteAllBytesAsync(path, original, this.CancellationToken).ConfigureAwait(false);

        _ = (await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();
    }

    /// <summary>A scene file restored with other content is an external change, so saving reports the ordinary conflict.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task SceneSourceRestoredWithOtherContentConflictsOnSave()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var scene = CreateAtomicScene(workspace.Root);
        scene.Project.Scenes.Add(scene);
        _ = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);
        var path = service.GetSceneSourceVersion(scene)!.SourcePath;
        var external = await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false) + "\n ";
        File.Delete(path);
        _ = await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(path, external, this.CancellationToken).ConfigureAwait(false);

        _ = (await service.RefreshSceneSourcePresenceAsync(scene, this.CancellationToken).ConfigureAwait(false)).Should().BeFalse();
        var save = () => service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene));

        _ = await save.Should().ThrowAsync<StorageWriteConflictException>().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false)).Should().Be(external);
    }
}
