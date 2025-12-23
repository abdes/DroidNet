// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Subjects;
using AwesomeAssertions;
using Oxygen.Assets.Catalog;
using Oxygen.Assets.Catalog.FileSystem;
using Oxygen.Storage.Native;
using Testably.Abstractions.Testing;

namespace Oxygen.Assets.Tests;

/// <summary>
/// Unit tests for <see cref="FileSystemAssetCatalog"/>.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
public sealed class FileSystemAssetCatalogTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task QueryAsync_ShouldEnumerateFilesUnderRoot()
    {
        // Arrange
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Textures");
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Meshes");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Wood01.png", "x", this.TestContext.CancellationToken).ConfigureAwait(true);
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Meshes\Hero.geo", "y", this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", Authority = "Content" },
            events);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset://Content/Textures/Wood01.png"));
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset://Content/Meshes/Hero.geo"));
    }

    [TestMethod]
    public async Task QueryAsync_WithScopeDescendants_ShouldFilterToFolder()
    {
        // Arrange
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Textures\UI");
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Meshes");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Wood01.png", "x", this.TestContext.CancellationToken).ConfigureAwait(true);
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\UI\Button.png", "y", this.TestContext.CancellationToken).ConfigureAwait(true);
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Meshes\Hero.geo", "z", this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", Authority = "Content" },
            events);

        var scope = new AssetQueryScope(
            Roots: [new Uri("asset://Content/Textures/")],
            Traversal: AssetQueryTraversal.Descendants);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset://Content/Textures/Wood01.png"));
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset://Content/Textures/UI/Button.png"));
        _ = results.Select(r => r.Uri).Should().NotContain(new Uri("asset://Content/Meshes/Hero.geo"));
    }

    [TestMethod]
    public async Task Changes_WhenRenamed_ShouldEmitRelocatedAndUpdateSnapshot()
    {
        // Arrange
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Textures");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Wood01.png", "x", this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", Authority = "Content" },
            events);

        // Prime snapshot
        _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);

        // Act
        events.Emit(new FileSystemCatalogEvent(
            FileSystemCatalogEventKind.Renamed,
            FullPath: @"C:\Project\Content\Textures\Wood02.png",
            OldFullPath: @"C:\Project\Content\Textures\Wood01.png"));

        // Allow the buffer window to flush.
        await Task.Delay(150, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = received.Should().ContainSingle(c => c.Kind == AssetChangeKind.Relocated);
        _ = received.Single(c => c.Kind == AssetChangeKind.Relocated).PreviousUri
            .Should().Be(new Uri("asset://Content/Textures/Wood01.png"));
        _ = received.Single(c => c.Kind == AssetChangeKind.Relocated).Uri
            .Should().Be(new Uri("asset://Content/Textures/Wood02.png"));

        var snapshot = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = snapshot.Select(r => r.Uri).Should().Contain(new Uri("asset://Content/Textures/Wood02.png"));
        _ = snapshot.Select(r => r.Uri).Should().NotContain(new Uri("asset://Content/Textures/Wood01.png"));
    }

    private sealed class ManualEventSource : IFileSystemCatalogEventSource
    {
        private readonly Subject<FileSystemCatalogEvent> subject = new();
        private bool disposed;

        public IObservable<FileSystemCatalogEvent> Events => this.subject;

        public void Emit(FileSystemCatalogEvent ev) => this.subject.OnNext(ev);

        public void Dispose()
        {
            if (this.disposed)
            {
                return;
            }

            this.disposed = true;
            this.subject.OnCompleted();
            this.subject.Dispose();
        }
    }
}
