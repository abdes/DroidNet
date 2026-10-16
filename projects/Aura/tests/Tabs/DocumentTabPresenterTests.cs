// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Runtime.CompilerServices;
using AwesomeAssertions;
using DroidNet.Aura.Documents;
using DroidNet.Aura.Windowing;
using DroidNet.Documents;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Controls.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class DocumentTabPresenterTests : TabStripTestsBase
{
    private Mock<ILoggerFactory> mockLoggerFactory = null!;

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();
        _ = this.mockLoggerFactory
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());
    });

    [TestMethod]
    public Task DocumentOpened_AddsTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 1" };

        // Act
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().ContainSingle();
        var added = tabStrip.Items[0];
        _ = added.ContentId.Should().Be(id.Value);
        _ = added.Header.Should().Be(meta.Title);
        _ = tabStrip.SelectedItem.Should().Be(added);
    });

    [TestMethod]
    public Task Constructor_HydratesExistingDocuments_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        var docId = DocumentId.New();
        var metadata = new TestDocumentMetadata { DocumentId = docId, Title = "Preloaded" };

        using var presenter = new DocumentTabPresenter(
            tabStrip,
            CreateStatefulServiceMock(ctx.Id, [metadata], docId).Object,
            ctx,
            DispatcherQueue.GetForCurrentThread(),
            this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().ContainSingle();
        var hydrated = tabStrip.Items[0];
        _ = hydrated.ContentId.Should().Be(docId.Value);
        _ = hydrated.Header.Should().Be(metadata.Title);
        _ = tabStrip.SelectedItem.Should().Be(hydrated);
    });

    [TestMethod]
    public Task Constructor_SelectsActiveDocumentFromState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        var first = new TestDocumentMetadata { DocumentId = DocumentId.New(), Title = "Scene 1" };
        var second = new TestDocumentMetadata { DocumentId = DocumentId.New(), Title = "Scene 2" };

        using var presenter = new DocumentTabPresenter(
            tabStrip,
            CreateStatefulServiceMock(ctx.Id, [first, second], second.DocumentId).Object,
            ctx,
            DispatcherQueue.GetForCurrentThread(),
            this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().HaveCount(2);
        var selected = tabStrip.SelectedItem as Controls.TabItem;
        _ = selected.Should().NotBeNull();
        var active = selected!;
        _ = active.ContentId.Should().Be(second.DocumentId.Value);
        _ = active.Header.Should().Be(second.Title);
    });

    [TestMethod]
    public Task Constructor_SkipsHydration_WhenStateUnavailable_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        var ds = new TestDocumentService();
        var meta = new TestDocumentMetadata { Title = "Pre-opened" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);

        // Act - instantiate presenter after documents already existed; service lacks IDocumentServiceState
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - no hydration occurred
        _ = tabStrip.Items.Should().BeEmpty();

        // When a new document opens after presenter creation, normal behavior still applies
        var newMeta = new TestDocumentMetadata { Title = "Fresh" };
        _ = await ds.OpenDocumentAsync(ctx.Id, newMeta, -1, shouldSelect: true).ConfigureAwait(false);
        await WaitForRenderCompletion().ConfigureAwait(true);
        _ = tabStrip.Items.Should().ContainSingle();
        _ = tabStrip.Items[0].ContentId.Should().Be(newMeta.DocumentId.Value);
    });

    [TestMethod]
    public Task TabCloseRequested_CallsCloseAsync_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 2" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - request close from tabSplitter
        var tab = tabStrip.Items[0];
        tab.IsClosable = true;

        tabStrip.HandleTabCloseRequest(tab);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.CloseCallCount.Should().Be(1);
        _ = ds.ClosedIds.Should().Contain(id);
    });

    [TestMethod]
    public Task DocumentClose_Veto_PreventsClose_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "VetoDoc" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Add a veto handler
        ds.DocumentClosing += (s, e) => e.AddVetoTask(Task.FromResult(false));

        // Act - request close from tabStrip
        var tab = tabStrip.Items[0];
        tab.IsClosable = true;
        tabStrip.HandleTabCloseRequest(tab);
        await WaitUntilAsync(() => ds.CloseCallCount > 0 || ds.ClosedIds.Count > 0, TimeSpan.FromSeconds(1)).ConfigureAwait(true);

        // Assert - close was vetoed
        _ = ds.CloseCallCount.Should().Be(1);
        _ = ds.ClosedIds.Should().NotContain(id);
        _ = tabStrip.Items.Should().ContainSingle();
    });

    [TestMethod]
    public Task DocumentClose_ForceBypassesVeto_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "ForceDoc" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Add a veto handler that would normally veto
        ds.DocumentClosing += (s, e) => e.AddVetoTask(Task.FromResult(false));

        // Act - force close from test service (bypass veto)
        _ = await ds.CloseDocumentAsync(ctx.Id, id, force: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - closed despite veto
        _ = ds.ClosedIds.Should().Contain(id);
        _ = tabStrip.Items.Should().BeEmpty();
    });

    [TestMethod]
    public Task DocumentClosed_RemovesTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 3" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - close the doc from service
        _ = await ds.CloseDocumentAsync(ctx.Id, id, force: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().BeEmpty();
        _ = ds.ClosedIds.Should().Contain(id);
    });

    [TestMethod]
    public Task DocumentAttached_AddsTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Attached Doc" };

        // Act
        _ = await ds.AttachDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().ContainSingle();
        var added = tabStrip.Items[0];
        _ = added.Header.Should().Be(meta.Title);
    });

    [TestMethod]
    public Task DocumentDetached_RemovesTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc to Detach" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        _ = await ds.DetachDocumentAsync(ctx.Id, id).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().BeEmpty();
    });

    [TestMethod]
    public Task DocumentActivated_SelectsTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 4" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - select via service
        _ = await ds.SelectDocumentAsync(ManagedWindowFactory.CreateForWindow(window).Id, id).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        var tab = tabStrip.Items[0];
        _ = tabStrip.SelectedItem.Should().Be(tab);
        _ = tabStrip.SelectedIndex.Should().Be(0);
    });

    [TestMethod]
    public Task MetadataPropertyChanged_UpdatesTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 5" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - the document changes its state; nobody tells the service
        meta.Title = "Doc 5 - Renamed";
        meta.IsDirty = true;
        meta.IsPinnedHint = true;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        var tab = tabStrip.Items[0];
        _ = tab.Header.Should().Be("Doc 5 - Renamed");
        _ = tab.IsDirty.Should().BeTrue();
        _ = tab.IsPinned.Should().BeTrue();
    });

    [TestMethod]
    public Task MetadataPropertyChanged_OffTheUiThread_UpdatesTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 6" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        await Task.Run(() => meta.Title = "Doc 6 - Renamed").ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items[0].Header.Should().Be("Doc 6 - Renamed");
    });

    [TestMethod]
    public Task ClosedOrDetachedTab_StopsFollowingItsMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var closed = new TestDocumentMetadata { Title = "Closed" };
        var detached = new TestDocumentMetadata { Title = "Detached" };
        _ = await ds.OpenDocumentAsync(ctx.Id, closed, -1, shouldSelect: true).ConfigureAwait(true);
        _ = await ds.OpenDocumentAsync(ctx.Id, detached, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        _ = await ds.CloseDocumentAsync(ctx.Id, closed.DocumentId, force: true).ConfigureAwait(true);
        _ = await ds.DetachDocumentAsync(ctx.Id, detached.DocumentId).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = closed.HasListeners.Should().BeFalse();
        _ = detached.HasListeners.Should().BeFalse();
    });

    [TestMethod]
    public Task DisposedPresenter_StopsFollowingMetadata_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 7" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        presenter.Dispose();

        // Assert
        _ = meta.HasListeners.Should().BeFalse();
    });

    [TestMethod]
    public Task RefusedDetach_LeavesTheTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService { RefusesDetach = true };
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Stays" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        tabStrip.DetachTab(tabStrip.Items[0]);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.DetachedIds.Should().BeEmpty();
        _ = tabStrip.Items.Should().ContainSingle();
        _ = tabStrip.Items[0].ContentId.Should().Be(meta.DocumentId.Value);
    });

    [TestMethod]
    public Task TabDetachRequested_CallsDetachAsync_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 2" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - request detach from tabStrip (simulate tear-out start)
        var tab = tabStrip.Items[0];

        tabStrip.DetachTab(tab);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.DetachedIds.Should().Contain(id);
    });

    [TestMethod]
    public Task DocumentClose_ServiceVeto_PreventsClose_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "SvcVetoDoc" };
        _ = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        var id = meta.DocumentId;
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Add a veto handler
        ds.DocumentClosing += (s, e) => e.AddVetoTask(Task.FromResult(false));

        // Act - call CloseDocumentAsync directly (service API)
        var result = await ds.CloseDocumentAsync(ctx.Id, id, force: false).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert - close was vetoed
        _ = result.Should().BeFalse();
        _ = ds.ClosedIds.Should().NotContain(id);
        _ = tabStrip.Items.Should().ContainSingle();
    });

    private static async Task WaitUntilAsync(Func<bool> condition, TimeSpan timeout)
    {
        var sw = System.Diagnostics.Stopwatch.StartNew();
        while (!condition() && sw.Elapsed < timeout)
        {
            await Task.Delay(10).ConfigureAwait(false);
        }

        if (!condition())
        {
            throw new InvalidOperationException("Condition not met within timeout");
        }
    }

    private static Mock<IDocumentService> CreateStatefulServiceMock(WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, DocumentId? activeDocumentId)
    {
        var mock = new Mock<IDocumentService>(MockBehavior.Loose);
        var stateMock = mock.As<IDocumentServiceState>();
        _ = stateMock
            .Setup(s => s.GetOpenDocuments(windowId))
            .Returns(documents);
        _ = stateMock
            .Setup(s => s.GetActiveDocumentId(windowId))
            .Returns(activeDocumentId);

        _ = mock
            .Setup(s => s.SelectDocumentAsync(It.IsAny<WindowId>(), It.IsAny<DocumentId>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.CloseDocumentAsync(It.IsAny<WindowId>(), It.IsAny<DocumentId>(), It.IsAny<bool>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.DetachDocumentAsync(It.IsAny<WindowId>(), It.IsAny<DocumentId>()))
            .ReturnsAsync((IDocumentMetadata?)null);
        _ = mock
            .Setup(s => s.OpenDocumentAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<int>(), It.IsAny<bool>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.AttachDocumentAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<int>(), It.IsAny<bool>()))
            .ReturnsAsync(value: true);

        return mock;
    }

    /// <summary>
    /// Helper to create a ManagedWindow for tests (avoids using the ManagedWindowFactory dependency).
    /// This mirrors the minimal parts of the window context used by DocumentTabPresenter.
    /// </summary>
    private static class ManagedWindowFactory
    {
        public static ManagedWindow CreateForWindow(Window window) => new()
        {
            DispatcherQueue = VisualUserInterfaceTestsApp.DispatcherQueue,
            Id = new Microsoft.UI.WindowId((ulong)window.GetHashCode()),
            Window = window,
            Category = new WindowCategory("Test"),
            CreatedAt = DateTimeOffset.UtcNow,
        };
    }

    private sealed class TestDocumentService : IDocumentService
    {
        private readonly Dictionary<DocumentId, IDocumentMetadata> store = [];

        public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

        public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

        public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

        public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

        public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

        public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

        public List<DocumentId> ClosedIds { get; } = [];

        public List<DocumentId> DetachedIds { get; } = [];

        public int CloseCallCount { get; private set; }

        public bool RefusesDetach { get; init; }

        public Task<bool> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
        {
            this.store[metadata.DocumentId] = metadata;
            this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, metadata, indexHint, shouldSelect));
            return Task.FromResult(true);
        }

        public async Task<bool> CloseDocumentAsync(WindowId windowId, DocumentId documentId, bool force = false)
        {
            this.CloseCallCount++;

            var metadata = this.store.TryGetValue(documentId, out var value)
                ? value
                : new TestDocumentMetadata { Title = "Unknown", DocumentId = documentId };

            // Fire DocumentClosing then DocumentClosed
            var closing = new DocumentClosingEventArgs(windowId, metadata, force);
            this.DocumentClosing?.Invoke(this, closing);

            if (!force)
            {
                var allowed = await closing.WaitForVetoResultAsync().ConfigureAwait(false);
                if (!allowed)
                {
                    return false;
                }
            }

            this.DocumentClosed?.Invoke(this, new DocumentClosedEventArgs(windowId, metadata));
            this.ClosedIds.Add(documentId);

            _ = this.store.Remove(documentId);
            return true;
        }

        public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, DocumentId documentId)
        {
            if (this.RefusesDetach || !this.store.Remove(documentId, out var metadata))
            {
                return Task.FromResult<IDocumentMetadata?>(null);
            }

            this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
            this.DetachedIds.Add(documentId);
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
        {
            this.store[metadata.DocumentId] = metadata;
            this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
            return Task.FromResult(true);
        }

        public Task<bool> SelectDocumentAsync(WindowId windowId, DocumentId documentId)
        {
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
            return Task.FromResult(true);
        }

        // Return all stored documents for test purposes
        public IReadOnlyList<IDocumentMetadata> GetOpenDocuments(WindowId windowId)
            => [.. this.store.Values];

        // Return the first document's ID if any exist, otherwise null
        public DocumentId? GetActiveDocumentId(WindowId windowId)
            => this.store.Count > 0 ? this.store.Keys.First() : null;
    }

    private sealed class TestDocumentMetadata : IDocumentMetadata
    {
        public event PropertyChangedEventHandler? PropertyChanged;

        public DocumentId DocumentId { get; init; } = DocumentId.New();

        public string Title { get; set => this.Set(ref field, value); } = string.Empty;

        public Uri? IconUri { get; set => this.Set(ref field, value); }

        public bool IsDirty { get; set => this.Set(ref field, value); }

        public bool IsPinnedHint { get; set => this.Set(ref field, value); }

        public bool IsClosable { get; set => this.Set(ref field, value); } = true;

        public bool HasListeners => this.PropertyChanged is not null;

        private void Set<T>(ref T field, T value, [CallerMemberName] string? property = null)
        {
            if (EqualityComparer<T>.Default.Equals(field, value))
            {
                return;
            }

            field = value;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(property));
        }
    }
}
