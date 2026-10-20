// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Projects.Tests;

/// <summary>The test kit's projects are the projects the editor saves and opens.</summary>
[TestClass]
internal sealed class TestProjectTests
{
    /// <summary>A project the kit saves loads through the project manager and opens as the context it was saved from.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task SavedProjectOpensAsTheContextItWasSavedFrom()
    {
        var directory = Directory.CreateTempSubdirectory("oxygen-test-project-");
        try
        {
            var disk = new RealFileSystem();
            var saved = TestProject.Create(disk, directory.FullName, [new ProjectMountPoint("Content", "Authoring")]).Context with
            {
                Name = "Saved",
                DefaultSceneId = SceneId.New(),
                LocalFolderMounts = [new("Library", FileSystemPath.From(@"C:\Library"))],
                CookedContentOrder = [new(CookedContentSourceKind.LocalFolder, "Library"), new(CookedContentSourceKind.ProjectOutput)],
            };

            _ = TestProject.SaveProjectFile(saved);
            var info = await new ProjectManagerService(new NativeStorageProvider(disk)).LoadProjectInfoAsync(directory.FullName).ConfigureAwait(false);
            var opened = TestProject.Open(info!, disk).Context;

            _ = disk.Directory.Exists(Path.Combine(directory.FullName, "Authoring")).Should().BeTrue("the kit creates each mount's folder");
            _ = opened.ProjectId.Should().Be(saved.ProjectId);
            _ = opened.Name.Should().Be(saved.Name);
            _ = opened.ProjectRoot.Should().Be(saved.ProjectRoot);
            _ = opened.AuthoringMounts.Should().Equal(saved.AuthoringMounts);
            _ = opened.LocalFolderMounts.Should().Equal(saved.LocalFolderMounts);
            _ = opened.CookedContentOrder.Should().Equal(saved.CookedContentOrder);
            _ = opened.DefaultSceneId.Should().Be(saved.DefaultSceneId);
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }
}
