// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Runtime.CompilerServices;
using AwesomeAssertions;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
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
        var fixture = new Fixture();
        using var catalog = fixture.CreateCatalog();
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
        var fixture = new Fixture();
        using var catalog = fixture.CreateCatalog();
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
        var fixture = new Fixture { RootFailure = new IOException("Scan failed") };
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        using var catalog = fixture.CreateCatalog();
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
        var fixture = new Fixture();
        using var catalog = fixture.CreateCatalog();
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
        var fixture = new Fixture();
        fixture.SetProjectAvailable(available: false);
        using var catalog = fixture.CreateCatalog();
        await catalog.InitializeAsync().ConfigureAwait(false);
        _ = (await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeEmpty();
        fixture.SetProjectAvailable(available: true);
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        AssertComplete(await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false));
    }

    /// <summary>An added folder is not registered when its first scan fails.</summary>
    /// <returns>The asynchronous folder-registration regression.</returns>
    [TestMethod]
    public async Task FailedAdditionalFolderLeavesTheExistingCatalogIntact()
    {
        var fixture = new Fixture();
        fixture.RootRelease.SetResult();
        fixture.ContentRelease.SetResult();
        using var catalog = fixture.CreateCatalog();
        var before = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        var extra = fixture.AddFailingFolder();
        Func<Task> add = () => catalog.AddFolderAsync(extra, "Extra");
        _ = await add.Should().ThrowAsync<IOException>().ConfigureAwait(false);
        var after = await catalog.QueryAsync(new(AssetQueryScope.All), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = after.Should().Equal(before);
    }

    private static void AssertComplete(IReadOnlyList<AssetRecord> records)
    {
        _ = records.Should().Contain(record => record.Uri == new Uri("asset:///project/Root.txt"));
        _ = records.Should().Contain(record => record.Uri == new Uri("asset:///Content/Shape.ogeo"));
    }

    private sealed class Fixture
    {
        private readonly Mock<IStorageProvider> storage = new();
        private readonly Mock<IProjectContextService> context = new();
        private readonly ProjectContext project;
        private readonly Dictionary<string, IFolder> extraFolders = [with(StringComparer.Ordinal)];
        private int rootEnumerations;

        public Fixture()
        {
            var root = Path.Combine(Path.GetTempPath(), "Oxygen-Catalog-" + Guid.NewGuid().ToString("N"));
            var content = Path.Combine(root, "Content");
            this.project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(), Name = "Catalog", Category = Category.Games,
                ProjectRoot = root, AuthoringMounts = [new("Content", "Content")],
                LocalFolderMounts = [], Scenes = [],
            };
            this.SetProjectAvailable(available: true);
            _ = this.storage.Setup(value => value.Normalize(It.IsAny<string>())).Returns(string.Empty);
            _ = this.storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>()))
                .Returns((string parent, string relative) => Path.Combine(parent, relative.Replace('/', Path.DirectorySeparatorChar)));
            var rootFolder = MakeFolder(root, this.EnumerateRootAsync);
            var contentFolder = MakeFolder(content, this.EnumerateContentAsync);
            var missing = new Mock<IFolder>();
            _ = missing.Setup(value => value.ExistsAsync()).ReturnsAsync(value: false);
            _ = this.storage.Setup(value => value.GetFolderFromPathAsync(It.IsAny<string>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((string path, CancellationToken _) => string.Equals(path, root, StringComparison.Ordinal) ? rootFolder : string.Equals(path, content, StringComparison.Ordinal) ? contentFolder : this.extraFolders.GetValueOrDefault(path, missing.Object));
        }

        public TaskCompletionSource RootEntered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource RootRelease { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ContentEntered { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public TaskCompletionSource ContentRelease { get; } = new(TaskCreationOptions.RunContinuationsAsynchronously);

        public int RootEnumerations => Volatile.Read(ref this.rootEnumerations);

        public Exception? RootFailure { get; set; }

        public ProjectAssetCatalog CreateCatalog() => new(this.context.Object, this.storage.Object);

        public void SetProjectAvailable(bool available)
            => _ = this.context.SetupGet(static value => value.ActiveProject).Returns(available ? this.project : null);

        public IFolder AddFailingFolder()
        {
            var path = Path.Combine(this.project.ProjectRoot, "Extra");
            var folder = MakeFolder(path, (location, token) => throw new IOException("Extra folder failed"));
            this.extraFolders.Add(path, folder);
            return folder;
        }

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
