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

public partial class ProjectManagerServiceTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task AtomicSaveFailure_PreservesThePreviousSceneOrMissingTarget(bool existing)
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var provider = new NativeStorageProvider(new RealFileSystem());
        var service = new ProjectManagerService(provider, atomicFiles: store);
        var scene = CreateAtomicScene(workspace.Root);
        var snapshot = SceneSaveSnapshot.Capture(scene);
        if (existing)
        {
            _ = (await service.SaveSceneSnapshotAsync(snapshot).ConfigureAwait(false)).Should().BeTrue();
        }

        var path = Path.Combine(workspace.Root, "Content", "Scenes", "Atomic.oscene.json");
        var before = await store.ReadAsync(path, this.CancellationToken).ConfigureAwait(false);
        store.Fail = true;
        scene.RootNodes[0].Components.OfType<TransformComponent>().Single().LocalPosition = new Vector3(9, 8, 7);

        var saved = await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false);

        _ = saved.Should().BeFalse();
        var after = await store.ReadAsync(path, this.CancellationToken).ConfigureAwait(false);
        _ = after.Version.Should().Be(before.Version);
        _ = after.Content.Should().Equal(before.Content);
    }

    [TestMethod]
    public async Task ExternalSceneChange_RejectsSaveWithoutReplacingExternalBytes()
    {
        using var workspace = new AtomicWorkspace();
        var provider = new NativeStorageProvider(new RealFileSystem());
        var service = new ProjectManagerService(provider);
        var scene = CreateAtomicScene(workspace.Root);
        _ = (await service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene)).ConfigureAwait(false)).Should().BeTrue();
        var path = Path.Combine(workspace.Root, "Content", "Scenes", "Atomic.oscene.json");
        var external = SceneSaveSnapshot.Capture(scene).Json + "\n ";
        await File.WriteAllTextAsync(path, external, this.CancellationToken).ConfigureAwait(false);

        var saving = () => service.SaveSceneSnapshotAsync(SceneSaveSnapshot.Capture(scene));
        _ = await saving.Should().ThrowExactlyAsync<StorageWriteConflictException>().ConfigureAwait(false);

        _ = (await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false)).Should().Be(external);
    }

    private static Scene CreateAtomicScene(string root)
    {
        var info = new ProjectInfo("Atomic", Category.Games, root, "preview.png");
        var project = new Project(info) { Name = "Atomic" };
        var scene = new Scene(project) { Name = "Atomic" };
        scene.RootNodes.Add(new SceneNode(scene) { Name = "Cube" });
        return scene;
    }

    private sealed class ControlledAtomicStore : IAtomicFileStore
    {
        private readonly NativeAtomicFileStore inner = new(new RealFileSystem());

        public bool Fail { get; set; }

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default)
            => this.inner.ReadAsync(path, cancellationToken);

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
            => this.Fail ? throw new IOException("Controlled atomic write failure") : this.inner.WriteAsync(path, content, expected, cancellationToken);
    }

    private sealed partial class AtomicWorkspace : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("DroidNetSceneAtomic-");

        public string Root => this.directory.FullName;

        public void Dispose() => this.directory.Delete(recursive: true);
    }
}
