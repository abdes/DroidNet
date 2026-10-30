// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>
/// Every bound source mount gets a catalog and a watcher, inside or outside the project tree, so its assets are listed
/// and its changes reach the editor as those of <c>Content</c> do; a mount this machine cannot read has neither.
/// </summary>
[TestClass]
public sealed class SharedMountCatalogTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source mount outside the project lists its assets and reports a file added to its folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASourceMountOutsideTheProject_IsCataloguedAndWatched()
    {
        var folder = Directory.CreateTempSubdirectory("Oxygen-SharedCatalog-");
        try
        {
            var studio = Path.Combine(folder.FullName, "Studio");
            _ = Directory.CreateDirectory(Path.Combine(studio, "Materials"));
            await File.WriteAllTextAsync(Path.Combine(studio, "Materials", "Brick.omat.json"), "{}", this.TestContext.CancellationToken).ConfigureAwait(false);
            var project = CreateProject(folder.FullName, $$$"""{"version":1,"mounts":{"Studio":"{{{studio.Replace('\\', '/')}}}","Vendor":"{{{Path.Combine(folder.FullName, "Gone").Replace('\\', '/')}}}"}}""");
            using var session = new TestSession(project);
            await using var catalog = CreateCatalog(session);
            var added = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            using var subscription = catalog.Changes.Subscribe(change =>
            {
                if (change.Identity == AssetPath.Parse("/Studio/Materials/Stone.omat.json"))
                {
                    _ = added.TrySetResult();
                }
            });

            await catalog.InitializeAsync().ConfigureAwait(false);
            var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
            await File.WriteAllTextAsync(Path.Combine(studio, "Materials", "Stone.omat.json"), "{}", this.TestContext.CancellationToken).ConfigureAwait(false);

            _ = records.Should().Contain(static record => record.Identity == AssetPath.Parse("/Studio/Materials/Brick.omat.json"));
            _ = records.Should().NotContain(static record => record.Identity.Mount == "Vendor", "a mount whose folder is missing has no catalog");
            await added.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        finally
        {
            folder.Delete(recursive: true);
        }
    }

    // Content in the project; Studio and Vendor, placeholders the project's own .mounts.json binds.
    private static ProjectContext CreateProject(string folder, string mountsFile)
    {
        var root = Path.Combine(folder, "Project");
        _ = Directory.CreateDirectory(root);
        File.WriteAllText(Path.Combine(root, ProjectMounts.FileName), mountsFile);
        return TestProject.Create(new RealFileSystem(), root, [new ProjectMount("Content", MountKind.Source, "Content"), new ProjectMount("Studio"), new ProjectMount("Vendor")]).Context with
        {
            Name = "Shared",
        };
    }

    private static AssetCatalog CreateCatalog(TestSession session)
    {
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var publication = new CookPublicationService(
            Mock.Of<IContentCookCoordinator>(),
            session,
            files,
            new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: files),
            new EngineContentPakFixture());
        return new(session, new NativeStorageProvider(new RealFileSystem()), new EngineContentPakFixture(), publication);
    }
}
