// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Publication;
using Testably.Abstractions;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Controls folder enumeration to verify catalog initialization boundaries.</summary>
[TestClass]
public sealed class ProjectAssetCatalogTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Initialization callers and change observers see the complete set of initial mounts.</summary>
    /// <returns>The asynchronous initialization regression.</returns>
    [TestMethod]
    public async Task ConcurrentInitializationAndFirstNotificationObserveEveryMount()
    {
        using var fixture = new Fixture();
        await using var catalog = fixture.CreateCatalog();
        var observations = new ConcurrentQueue<Task<IReadOnlyList<AssetRecord>>>();
        var notified = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var subscription = catalog.Changes.Subscribe(change =>
        {
            observations.Enqueue(catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken));
            _ = notified.TrySetResult();
        });
        var initialization = catalog.InitializeAsync();
        await fixture.RootEntered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = catalog.InitializeAsync().Should().BeSameAs(initialization);
        var query = catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken);
        fixture.RootRelease.SetResult();
        await fixture.ContentEntered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = observations.Should().BeEmpty();
        _ = query.IsCompleted.Should().BeFalse();
        fixture.ContentRelease.SetResult();
        await initialization.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertComplete(await query.ConfigureAwait(false));
        await notified.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = observations.TryPeek(out var observed).Should().BeTrue();
        AssertComplete(await observed!.ConfigureAwait(false));
    }

    /// <summary>Canceling one query does not cancel the shared project initialization.</summary>
    /// <returns>The asynchronous waiter-cancellation regression.</returns>
    [TestMethod]
    public async Task CanceledQueryLeavesSharedInitializationAvailable()
    {
        using var fixture = new Fixture();
        await using var catalog = fixture.CreateCatalog();
        var initialization = catalog.InitializeAsync();
        await fixture.RootEntered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var cancellation = new CancellationTokenSource();
        var query = catalog.QueryAsync(new(AssetQueryScope.All), cancellation.Token);
        await cancellation.CancelAsync().ConfigureAwait(false);
        Func<Task> observe = async () => _ = await query.ConfigureAwait(false);
        _ = await observe.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        _ = initialization.IsCompleted.Should().BeFalse();
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await initialization.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertComplete(await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>A failed root scan can be retried without retaining partial registrations.</summary>
    /// <returns>The asynchronous retry regression.</returns>
    [TestMethod]
    public async Task FailedInitializationCanRetryWithoutDuplicatingCatalogs()
    {
        using var fixture = new Fixture { RootFailure = new IOException("Scan failed") };
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await using var catalog = fixture.CreateCatalog();
        Func<Task> initialize = catalog.InitializeAsync;
        _ = await initialize.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        fixture.RootFailure = null;
        await catalog.InitializeAsync().WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var records = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertComplete(records);
        _ = fixture.RootEnumerations.Should().Be(2, "retry must discard the failed root registration");
    }

    /// <summary>Closing a project cancels pending initialization without publishing its late entries.</summary>
    /// <returns>The asynchronous disposal regression.</returns>
    [TestMethod]
    public async Task DisposeDuringInitializationSuppressesLateNotifications()
    {
        using var fixture = new Fixture();
        await using var catalog = fixture.CreateCatalog();
        var notifications = new ConcurrentQueue<AssetChange>();
        using var subscription = catalog.Changes.Subscribe(notifications.Enqueue);
        var query = catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken);
        await fixture.RootEntered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        catalog.Dispose();
        Func<Task> observe = async () => _ = await query.ConfigureAwait(false);
        _ = await observe.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        _ = notifications.Should().BeEmpty();
        Func<Task> initialize = catalog.InitializeAsync;
        _ = await initialize.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(false);
    }

    /// <summary>Querying before project activation does not prevent later initialization.</summary>
    /// <returns>The asynchronous activation regression.</returns>
    [TestMethod]
    public async Task InitializationWithoutAProjectCanRunAfterActivation()
    {
        using var fixture = new Fixture();
        fixture.SetProjectAvailable(available: false);
        await using var catalog = fixture.CreateCatalog();
        await catalog.InitializeAsync().ConfigureAwait(false);
        _ = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        fixture.SetProjectAvailable(available: true);
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        AssertComplete(await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>Concurrent consumers of one accepted publication share its initial scan.</summary>
    [TestMethod]
    public async Task ConcurrentRefreshesOfTheSamePublicationShareInitialization()
    {
        using var fixture = new Fixture();
        using var selected = await fixture.Publication.AcquireReadAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var catalog = fixture.CreateCatalog();
        var first = catalog.RefreshAsync(selected, this.TestContext.CancellationToken);
        await fixture.RootEntered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = catalog.RefreshAsync(selected, this.TestContext.CancellationToken);
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await Task.WhenAll(first, second).WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.RootEnumerations.Should().Be(1);
        AssertComplete(await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>A malformed head blocks native admission without hiding authored assets or repeatedly rebuilding their catalog.</summary>
    [TestMethod]
    public async Task DamagedPublicationRetainsAuthoringAndCanRecoverAfterRepair()
    {
        using var fixture = new Fixture();
        var cooked = Directory.CreateDirectory(Path.Combine(fixture.Project.ProjectRoot, ".cooked"));
        var head = Path.Combine(cooked.FullName, "head.json");
        await File.WriteAllTextAsync(head, "{broken", this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await using var catalog = fixture.CreateCatalog();
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using (var damaged = await catalog.ReadSnapshotAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false))
        {
            AssertComplete(damaged.Records);
            _ = damaged.Publication.Should().BeNull();
            _ = damaged.PublicationError.Should().NotBeNullOrEmpty();
        }

        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = fixture.RootEnumerations.Should().Be(1);
        Func<Task> admit = async () => { using var selected = await fixture.Publication.AcquireReadAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false); };
        _ = await admit.Should().ThrowAsync<Exception>().ConfigureAwait(false);
        File.Delete(head);
        await catalog.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var repaired = await catalog.ReadSnapshotAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertComplete(repaired.Records);
        _ = repaired.Publication.Should().NotBeNull();
        _ = repaired.PublicationError.Should().BeNull();
    }

    /// <summary>An older automatic observation cannot displace an explicitly accepted publication.</summary>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task AcceptedPublicationWinsOverDelayedAutomaticObservation(bool fail)
    {
        using var fixture = new Fixture();
        using var accepted = await fixture.Publication.AcquireReadAsync(fixture.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        fixture.ReadOverride = async (path, token) =>
        {
            entered.TrySetResult();
            await release.Task.WaitAsync(token).ConfigureAwait(false);
            return fail ? throw new InvalidDataException("An obsolete observation failed.") : await files.ReadAsync(path, token).ConfigureAwait(false);
        };
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await using var catalog = fixture.CreateCatalog();
        var automatic = catalog.RefreshAsync(this.TestContext.CancellationToken);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            await catalog.RefreshAsync(accepted, this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        finally
        {
            release.TrySetResult();
        }

        await automatic.ConfigureAwait(false);
        using var snapshot = await catalog.ReadSnapshotAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        AssertComplete(snapshot.Records);
        _ = snapshot.Publication.Should().NotBeNull();
        _ = snapshot.PublicationError.Should().BeNull();
        _ = fixture.RootEnumerations.Should().Be(1);
    }

    /// <summary>Shutdown waits for independent catalog initialization after cancelling its readers.</summary>
    [TestMethod]
    public async Task AsyncDisposalDrainsInitializationBeforeReleasingItsLifetime()
    {
        using var fixture = new Fixture();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var cancelled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        fixture.ReadOverride = async (path, token) =>
        {
            entered.TrySetResult();
            try
            {
                await Task.Delay(Timeout.Infinite, token).ConfigureAwait(false);
            }
            finally
            {
                cancelled.TrySetResult();
                await release.Task.ConfigureAwait(false);
            }

            return await files.ReadAsync(path, token).ConfigureAwait(false);
        };
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        await using var catalog = fixture.CreateCatalog();
        var initialization = catalog.InitializeAsync();
        Task disposal = Task.CompletedTask;
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            disposal = catalog.DisposeAsync().AsTask();
            await cancelled.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = disposal.IsCompleted.Should().BeFalse();
        }
        finally
        {
            release.TrySetResult();
            await disposal.ConfigureAwait(false);
        }

        _ = await ((Func<Task>)(() => initialization)).Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    private static void AssertComplete(IReadOnlyList<AssetRecord> records)
    {
        _ = records.Should().Contain(record => record.Uri == new Uri("asset:///project/Root.txt"));
        _ = records.Should().Contain(record => record.Uri == new Uri("asset:///Content/Shape.ogeo"));
    }

    private sealed class Fixture : IDisposable
    {
        private readonly Mock<IStorageProvider> storage = new();
        private readonly Mock<IProjectContextService> context = new();
        private readonly ProjectContext project;
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-Catalog-");
        private int rootEnumerations;

        public Fixture()
        {
            var root = this.directory.FullName;
            var content = Path.Combine(root, "Content");
            this.project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Catalog", Category = Category.Games,
                ProjectRoot = root, AuthoringMounts = [new("Content", "Content")],
                LocalFolderMounts = [], Scenes = [],
            };
            var files = new NativeAtomicFileStore(new RealFileSystem());
            var observedFiles = new Mock<IAtomicFileStore>();
            _ = observedFiles.Setup(value => value.ReadAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .Returns((string path, CancellationToken token) => this.ReadOverride?.Invoke(path, token) ?? files.ReadAsync(path, token));
            this.Publication = new(Mock.Of<IContentCookCoordinator>(), this.context.Object, observedFiles.Object,
                new ProjectManagerService(new NativeStorageProvider(new RealFileSystem()), atomicFiles: files));
            this.SetProjectAvailable(available: true);
            _ = this.storage.Setup(value => value.Normalize(It.IsAny<string>())).Returns(string.Empty);
            _ = this.storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>()))
                .Returns((string parent, string relative) => Path.Combine(parent, relative.Replace('/', Path.DirectorySeparatorChar)));
            var rootFolder = MakeFolder(root, this.EnumerateRootAsync);
            var contentFolder = MakeFolder(content, this.EnumerateContentAsync);
            var missing = new Mock<IFolder>();
            _ = missing.Setup(value => value.ExistsAsync()).ReturnsAsync(value: false);
            _ = this.storage.Setup(value => value.GetFolderFromPathAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((string path, CancellationToken _) => string.Equals(path, root, StringComparison.Ordinal) ? rootFolder : string.Equals(path, content, StringComparison.Ordinal) ? contentFolder : missing.Object);
        }

        public TaskCompletionSource RootEntered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource RootRelease { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ContentEntered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ContentRelease { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int RootEnumerations => Volatile.Read(ref this.rootEnumerations);

        public Exception? RootFailure { get; set; }

        public Func<string, CancellationToken, Task<FileSnapshot>>? ReadOverride { get; set; }

        public ProjectAssetCatalog CreateCatalog()
        {
            var empty = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
            var discovery = new Mock<IBuiltinCatalogDiscovery>();
            _ = discovery.SetupGet(value => value.Snapshot).Returns(empty);
            _ = discovery.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(empty);
            _ = discovery.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).ReturnsAsync(empty);
            return new(this.context.Object, this.storage.Object, discovery.Object, this.Publication);
        }

        public void SetProjectAvailable(bool available)
            => _ = this.context.SetupGet(static value => value.ActiveProject).Returns(available ? this.project : null);

        public ProjectContext Project => this.project;

        public CookPublicationService Publication { get; }

        public void Dispose() => this.directory.Delete(recursive: true);

        private static IFolder MakeFolder(string path, Func<string, CancellationToken, IAsyncEnumerable<IDocument>> documents)
        {
            var folder = new Mock<IFolder>();
            _ = folder.SetupGet(value => value.Location).Returns(path);
            _ = folder.Setup(value => value.ExistsAsync()).ReturnsAsync(value: true);
            _ = folder.Setup(value => value.GetFoldersAsync(It.IsAny<CancellationToken>())).Returns(EmptyFolders());
            _ = folder.Setup(value => value.GetDocumentsAsync(It.IsAny<CancellationToken>()))
                .Returns((CancellationToken token) => documents(path, token));
            return folder.Object;
        }

        private static async IAsyncEnumerable<IFolder> EmptyFolders()
        {
            await Task.CompletedTask.ConfigureAwait(false);
            yield break;
        }

        private async IAsyncEnumerable<IDocument> EnumerateRootAsync(string path, [EnumeratorCancellation] CancellationToken token)
        {
            _ = Interlocked.Increment(ref this.rootEnumerations);
            _ = this.RootEntered.TrySetResult();
            await this.RootRelease.Task.WaitAsync(token).ConfigureAwait(false);
            yield return this.RootFailure is { } failure ? throw failure
                : Mock.Of<IDocument>(document => document.Location == Path.Combine(path, "Root.txt"));
        }

        private async IAsyncEnumerable<IDocument> EnumerateContentAsync(string path, [EnumeratorCancellation] CancellationToken token)
        {
            _ = this.ContentEntered.TrySetResult();
            await this.ContentRelease.Task.WaitAsync(token).ConfigureAwait(false);
            yield return Mock.Of<IDocument>(document => document.Location == Path.Combine(path, "Shape.ogeo"));
        }
    }
}
