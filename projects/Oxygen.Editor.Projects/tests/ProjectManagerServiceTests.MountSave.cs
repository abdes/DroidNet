// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.World;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>Verifies the commit point used by immediate content-mount changes.</summary>
public partial class ProjectManagerServiceTests
{
    /// <summary>Confirmed mount edits preserve the prior manifest when atomic replacement fails.</summary>
    /// <returns>The asynchronous failed-save regression.</returns>
    [TestMethod]
    public async Task MountSaveFailurePreservesTheAcceptedManifest()
    {
        using var workspace = new AtomicWorkspace();
        var store = new ControlledAtomicStore();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: store);
        var original = new ProjectInfo("Mounts", Category.Games, workspace.Root) { AuthoringMounts = [new("Content", "Content")] };
        _ = (await service.SaveProjectInfoAsync(original).ConfigureAwait(false)).Should().BeTrue();
        var path = Path.Combine(workspace.Root, Constants.ProjectFileName);
        var before = await File.ReadAllBytesAsync(path, this.CancellationToken).ConfigureAwait(false);
        var candidate = new ProjectInfo(original.Id, original.Name, original.Category, original.Location)
        {
            AuthoringMounts = [.. original.AuthoringMounts],
            LocalFolderMounts = [new("Library", @"C:\Library")],
        };
        store.Fail = true;
        Func<Task> save = () => service.SaveProjectInfoAsync(candidate, original, this.CancellationToken);
        _ = await save.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        _ = (await File.ReadAllBytesAsync(path, this.CancellationToken).ConfigureAwait(false)).Should().Equal(before);
        store.Fail = false;
        await save().ConfigureAwait(false);
        var saved = ProjectInfo.FromJson(await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false));
        _ = saved.LocalFolderMounts.Should().Equal(candidate.LocalFolderMounts);
    }

    /// <summary>An external project edit cannot be overwritten by a mount dialog based on older configuration.</summary>
    /// <returns>The asynchronous conflict regression.</returns>
    [TestMethod]
    public async Task MountSaveRejectsExternalConfigurationChanges()
    {
        using var workspace = new AtomicWorkspace();
        var service = new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()));
        var original = new ProjectInfo("Mounts", Category.Games, workspace.Root) { AuthoringMounts = [new("Content", "Content")] };
        _ = (await service.SaveProjectInfoAsync(original).ConfigureAwait(false)).Should().BeTrue();
        var path = Path.Combine(workspace.Root, Constants.ProjectFileName);
        var external = new ProjectInfo(original.Id, "Externally edited", original.Category, original.Location) { AuthoringMounts = [.. original.AuthoringMounts] };
        var externalJson = ProjectInfo.ToJson(external);
        await File.WriteAllTextAsync(path, externalJson, this.CancellationToken).ConfigureAwait(false);
        Func<Task> save = () => service.SaveProjectInfoAsync(original, original, this.CancellationToken);
        _ = await save.Should().ThrowAsync<IOException>().WithMessage("*changed outside the editor*").ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.CancellationToken).ConfigureAwait(false)).Should().Be(externalJson);
    }
}
