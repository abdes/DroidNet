// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Catalog.FileSystem;
using Testably.Abstractions.Testing;

namespace Oxygen.Managed.Assets.Tests;

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
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
            events);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/Wood01.png"));
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Meshes/Hero.geo"));
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
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
            events);

        var scope = new AssetQueryScope(
            Roots: [new Uri("asset:///Content/Textures/")],
            Traversal: AssetQueryTraversal.Descendants);

        // Act
        var results = await catalog.QueryAsync(new AssetQuery(scope), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/Wood01.png"));
        _ = results.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/UI/Button.png"));
        _ = results.Select(r => r.Uri).Should().NotContain(new Uri("asset:///Content/Meshes/Hero.geo"));
    }

    [TestMethod]
    public async Task RefreshAsync_WhenFileWasCreatedAfterInitialQuery_ShouldUpdateSnapshotWithoutWatcherEvent()
    {
        // Arrange
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Materials");

        var storage = new NativeStorageProvider(fs);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
            events);

        var initial = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await fs.File.WriteAllTextAsync(
            @"C:\Project\Content\Materials\Red.omat.json",
            "{}",
            this.TestContext.CancellationToken).ConfigureAwait(false);

        // Act
        var stale = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var refreshed = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = initial.Should().BeEmpty();
        _ = stale.Should().BeEmpty();
        _ = refreshed.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Materials/Red.omat.json"));
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
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
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
            .Should().Be(new Uri("asset:///Content/Textures/Wood01.png"));
        _ = received.Single(c => c.Kind == AssetChangeKind.Relocated).Uri
            .Should().Be(new Uri("asset:///Content/Textures/Wood02.png"));

        var snapshot = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = snapshot.Select(r => r.Uri).Should().Contain(new Uri("asset:///Content/Textures/Wood02.png"));
        _ = snapshot.Select(r => r.Uri).Should().NotContain(new Uri("asset:///Content/Textures/Wood01.png"));
    }

    [TestMethod]
    public async Task Changes_WhenHiddenTemporaryFileReplacesKnownFile_ShouldEmitUpdated()
    {
        // Arrange
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Materials");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Materials\Wood.omat.json", "{}", this.TestContext.CancellationToken).ConfigureAwait(true);

        var storage = new NativeStorageProvider(fs);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            storage,
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
            events);
        _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);

        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);

        // Act: an atomic save writes a hidden temporary file, then moves it over the destination.
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Created, @"C:\Project\Content\Materials\.oxygen-0123.tmp"));
        events.Emit(new FileSystemCatalogEvent(
            FileSystemCatalogEventKind.Renamed,
            FullPath: @"C:\Project\Content\Materials\Wood.omat.json",
            OldFullPath: @"C:\Project\Content\Materials\.oxygen-0123.tmp"));
        await Task.Delay(150, this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = received.Should().ContainSingle()
            .Which.Should().Be(new AssetChange(AssetChangeKind.Updated, new Uri("asset:///Content/Materials/Wood.omat.json")));
    }

    /// <summary>A folder deleted outside the editor removes its files and is reported, without touching other files.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FolderDeletedOutsideRemovesItsFilesAndIsReported()
    {
        var (fs, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        var folders = new FolderRecorder(catalog, expected: 1);
        using var folderSubscription = folders;

        fs.Directory.Delete(@"C:\Project\Content\Textures", recursive: true);
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Deleted, @"C:\Project\Content\Textures"));

        _ = (await folders.Reported.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().Equal(new Uri("asset:///Content/Textures"));
        _ = received.Should().Equal(new AssetChange(AssetChangeKind.Removed, new Uri("asset:///Content/Textures/Wood01.png")));
    }

    /// <summary>A folder created or moved in outside the editor adds its files and is reported.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FolderCreatedOutsideAddsItsFilesAndIsReported()
    {
        var (fs, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        var folders = new FolderRecorder(catalog, expected: 1);
        using var folderSubscription = folders;

        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Props");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Props\Crate.geo", "c", this.TestContext.CancellationToken).ConfigureAwait(false);
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Created, @"C:\Project\Content\Props"));

        _ = (await folders.Reported.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().Equal(new Uri("asset:///Content/Props"));
        _ = received.Should().Equal(new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/Props/Crate.geo")));
    }

    /// <summary>A folder renamed outside the editor moves its files and reports the old and the new folder.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FolderRenamedOutsideMovesItsFilesAndReportsBothFolders()
    {
        var (fs, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        var folders = new FolderRecorder(catalog, expected: 2);
        using var folderSubscription = folders;

        fs.Directory.Move(@"C:\Project\Content\Textures", @"C:\Project\Content\Images");
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Renamed, @"C:\Project\Content\Images", @"C:\Project\Content\Textures"));

        _ = (await folders.Reported.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Should().BeEquivalentTo([new Uri("asset:///Content/Textures"), new Uri("asset:///Content/Images")]);
        _ = received.Should().BeEquivalentTo(
            [
                new AssetChange(AssetChangeKind.Removed, new Uri("asset:///Content/Textures/Wood01.png")),
                new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/Images/Wood01.png")),
            ]);
    }

    /// <summary>
    /// A file created or renamed under the root is applied as that one file: telling it from a folder does not fail
    /// and fall back to a rescan that would report every known file as updated.
    /// </summary>
    /// <param name="renamed">Whether the file arrives by a rename, as a move into the folder does.</param>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FileArrivingUnderTheRootReportsOnlyThatFile(bool renamed)
    {
        var (fs, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);

        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Stone.png", "s", this.TestContext.CancellationToken).ConfigureAwait(false);
        events.Emit(renamed
            ? new FileSystemCatalogEvent(FileSystemCatalogEventKind.Renamed, @"C:\Project\Content\Textures\Stone.png", @"C:\Elsewhere\Stone.png")
            : new FileSystemCatalogEvent(FileSystemCatalogEventKind.Created, @"C:\Project\Content\Textures\Stone.png"));
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = received.Should().Equal(new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/Textures/Stone.png")));
    }

    /// <summary>
    /// A refresh that runs before the watcher's events reports what it finds, so the events that arrive after it find
    /// nothing left to change and the change is reported once.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RefreshReportsAMoveTheWatcherHasNotDeliveredYet()
    {
        var (fs, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        fs.File.Move(@"C:\Project\Content\Textures\Wood01.png", @"C:\Project\Content\Meshes\Wood01.png");

        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Deleted, @"C:\Project\Content\Textures\Wood01.png"));
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Created, @"C:\Project\Content\Meshes\Wood01.png"));
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = received.Should().BeEquivalentTo(
            [
                new AssetChange(AssetChangeKind.Removed, new Uri("asset:///Content/Textures/Wood01.png")),
                new AssetChange(AssetChangeKind.Added, new Uri("asset:///Content/Meshes/Wood01.png")),
            ]);
    }

    /// <summary>
    /// A folder's own Changed event, which the watcher raises when a file inside it changes, is not a folder change: it
    /// neither rescans nor lists the folder as a file.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task FolderChangedEventIsNotAFolderChangeOrAFile()
    {
        var (_, events, catalog) = await CreatePrimedAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var source = events;
        using var catalogLifetime = catalog;
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        var folders = new List<Uri>();
        using var folderSub = catalog.FolderChanges.Subscribe(folders.Add);

        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Changed, @"C:\Project\Content\Textures"));
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = received.Should().BeEmpty();
        _ = folders.Should().BeEmpty();
        _ = (await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false))
            .Select(static record => record.Uri).Should().NotContain(new Uri("asset:///Content/Textures"));
    }

    /// <summary>A folder the catalog leaves out is neither listed nor reported, so another catalog can cover it alone.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ExcludedFolderIsNeitherListedNorReported()
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Textures");
        await fs.File.WriteAllTextAsync(@"C:\Project\Readme.md", "r", this.TestContext.CancellationToken).ConfigureAwait(false);
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Wood01.png", "x", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var events = new ManualEventSource();
        using var catalog = new FileSystemAssetCatalog(
            new NativeStorageProvider(fs),
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project", MountPoint = "project", ExcludedFolders = ["Content"] },
            events);
        var listed = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var received = new List<AssetChange>();
        using var sub = catalog.Changes.Subscribe(received.Add);
        var folders = new List<Uri>();
        using var folderSub = catalog.FolderChanges.Subscribe(folders.Add);

        fs.Directory.Delete(@"C:\Project\Content\Textures", recursive: true);
        events.Emit(new FileSystemCatalogEvent(FileSystemCatalogEventKind.Deleted, @"C:\Project\Content\Textures"));
        await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = listed.Select(static record => record.Uri).Should().Equal(new Uri("asset:///project/Readme.md"));
        _ = received.Should().BeEmpty();
        _ = folders.Should().BeEmpty();
    }

    // Content holds Textures/Wood01.png and Meshes/Hero.geo, already listed.
    private static async Task<(MockFileSystem Files, ManualEventSource Events, FileSystemAssetCatalog Catalog)> CreatePrimedAsync(CancellationToken cancellationToken)
    {
        var fs = new MockFileSystem();
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Textures");
        _ = fs.Directory.CreateDirectory(@"C:\Project\Content\Meshes");
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Textures\Wood01.png", "x", cancellationToken).ConfigureAwait(false);
        await fs.File.WriteAllTextAsync(@"C:\Project\Content\Meshes\Hero.geo", "y", cancellationToken).ConfigureAwait(false);
        var events = new ManualEventSource();
        var catalog = new FileSystemAssetCatalog(
            new NativeStorageProvider(fs),
            new FileSystemAssetCatalogOptions { RootFolderPath = @"C:\Project\Content", MountPoint = "Content" },
            events);
        _ = await catalog.QueryAsync(new AssetQuery(AssetQueryScope.All), cancellationToken).ConfigureAwait(false);
        return (fs, events, catalog);
    }

    /// <summary>Records a catalog's folder changes; completes once the expected number arrived.</summary>
    private sealed class FolderRecorder : IDisposable
    {
        private readonly List<Uri> received = [];
        private readonly TaskCompletionSource<IReadOnlyList<Uri>> reported = new(TaskCreationOptions.RunContinuationsAsynchronously);
        private readonly IDisposable subscription;

        public FolderRecorder(IAssetFolderSource source, int expected)
            => this.subscription = source.FolderChanges.Subscribe(folder =>
            {
                lock (this.received)
                {
                    this.received.Add(folder);
                    if (this.received.Count >= expected)
                    {
                        _ = this.reported.TrySetResult([.. this.received]);
                    }
                }
            });

        public Task<IReadOnlyList<Uri>> Reported => this.reported.Task;

        public void Dispose() => this.subscription.Dispose();
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
