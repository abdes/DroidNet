// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);

        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = tabStrip.Items.Should().ContainSingle();
        var added = tabStrip.Items[0];
        _ = added.ContentId.Should().Be(id);
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
        var docId = Guid.NewGuid();
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
        _ = hydrated.ContentId.Should().Be(docId);
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
        var first = new TestDocumentMetadata { DocumentId = Guid.NewGuid(), Title = "Scene 1" };
        var second = new TestDocumentMetadata { DocumentId = Guid.NewGuid(), Title = "Scene 2" };

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
        _ = active.ContentId.Should().Be(second.DocumentId);
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
        var newId = await ds.OpenDocumentAsync(ctx.Id, newMeta, -1, shouldSelect: true).ConfigureAwait(false);
        await WaitForRenderCompletion().ConfigureAwait(true);
        _ = tabStrip.Items.Should().ContainSingle();
        _ = tabStrip.Items[0].ContentId.Should().Be(newId);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(true);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
    public Task DocumentMetadataChanged_UpdatesTab_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var tabStrip = this.CreateTabStrip(0);
        await LoadTestContentAsync(tabStrip).ConfigureAwait(true);

        var window = VisualUserInterfaceTestsApp.MainWindow;
        var ds = new TestDocumentService();
        var ctx = ManagedWindowFactory.CreateForWindow(window);
        using var presenter = new DocumentTabPresenter(tabStrip, ds, ctx, DispatcherQueue.GetForCurrentThread(), this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>());
        var meta = new TestDocumentMetadata { Title = "Doc 5" };
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act - update metadata
        var newMeta = new TestDocumentMetadata { Title = "Doc 5 - Renamed", IsDirty = true, IsPinnedHint = true };
        _ = await ds.UpdateMetadataAsync(ctx.Id, id, newMeta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        var tab = tabStrip.Items[0];
        _ = tab.Header.Should().Be(newMeta.Title);
        _ = tab.IsPinned.Should().BeTrue();
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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
        var id = await ds.OpenDocumentAsync(ctx.Id, meta, -1, shouldSelect: true).ConfigureAwait(false);
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

    private static Mock<IDocumentService> CreateStatefulServiceMock(WindowId windowId, IReadOnlyList<IDocumentMetadata> documents, Guid? activeDocumentId)
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
            .Setup(s => s.SelectDocumentAsync(It.IsAny<WindowId>(), It.IsAny<Guid>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.CloseDocumentAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<bool>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.DetachDocumentAsync(It.IsAny<WindowId>(), It.IsAny<Guid>()))
            .ReturnsAsync((IDocumentMetadata?)null);
        _ = mock
            .Setup(s => s.OpenDocumentAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<int>(), It.IsAny<bool>()))
            .ReturnsAsync(Guid.NewGuid());
        _ = mock
            .Setup(s => s.AttachDocumentAsync(It.IsAny<WindowId>(), It.IsAny<IDocumentMetadata>(), It.IsAny<int>(), It.IsAny<bool>()))
            .ReturnsAsync(value: true);
        _ = mock
            .Setup(s => s.UpdateMetadataAsync(It.IsAny<WindowId>(), It.IsAny<Guid>(), It.IsAny<IDocumentMetadata>()))
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
        private readonly Dictionary<Guid, IDocumentMetadata> store = [];

        public event EventHandler<DocumentOpenedEventArgs>? DocumentOpened;

        public event EventHandler<DocumentClosingEventArgs>? DocumentClosing;

        public event EventHandler<DocumentClosedEventArgs>? DocumentClosed;

        public event EventHandler<DocumentDetachedEventArgs>? DocumentDetached;

        public event EventHandler<DocumentAttachedEventArgs>? DocumentAttached;

        public event EventHandler<DocumentMetadataChangedEventArgs>? DocumentMetadataChanged;

        public event EventHandler<DocumentActivatedEventArgs>? DocumentActivated;

        public List<Guid> ClosedIds { get; } = [];

        public List<Guid> DetachedIds { get; } = [];

        public int CloseCallCount { get; private set; }

        public Task<Guid> OpenDocumentAsync(WindowId windowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
        {
            var id = metadata.DocumentId == Guid.Empty ? Guid.NewGuid() : metadata.DocumentId;

            // Ensure metadata has the DocumentId set so UI consumers (like DocumentTabPresenter)
            // can rely on metadata.DocumentId matching the created id. We avoid reflection by
            // producing a test-metadata copy when the supplied metadata type does not expose a
            // writable DocumentId property.
            IDocumentMetadata effectiveMetadata;
            if (metadata is TestDocumentMetadata tm)
            {
                tm.DocumentId = id;
                effectiveMetadata = tm;
            }
            else
            {
                // Create a test-friendly copy of the metadata with the assigned id so the
                // presenter receives a metadata instance that contains the correct DocumentId.
                effectiveMetadata = new TestDocumentMetadata
                {
                    DocumentId = id,
                    Title = metadata.Title ?? string.Empty,
                    IconUri = metadata is { } ? metadata!.IconUri : null,
                    IsDirty = metadata is { } && metadata!.IsDirty,
                    IsPinnedHint = metadata is { } && metadata!.IsPinnedHint,
                };
            }

            this.store[id] = effectiveMetadata;

            // Fire document opened
            this.DocumentOpened?.Invoke(this, new DocumentOpenedEventArgs(windowId, effectiveMetadata, indexHint, shouldSelect));
            return Task.FromResult(id);
        }

        public async Task<bool> CloseDocumentAsync(WindowId windowId, Guid documentId, bool force = false)
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

            var closed = new DocumentClosedEventArgs(windowId, metadata!);
            this.DocumentClosed?.Invoke(this, closed);
            this.ClosedIds.Add(documentId);

            _ = this.store.Remove(documentId);
            return true;
        }

        public Task<IDocumentMetadata?> DetachDocumentAsync(WindowId windowId, Guid documentId)
        {
            if (!this.store.TryGetValue(documentId, out var metadata))
            {
                return Task.FromResult<IDocumentMetadata?>(null);
            }

            _ = this.store.Remove(documentId);
            this.DocumentDetached?.Invoke(this, new DocumentDetachedEventArgs(windowId, metadata));
            this.DetachedIds.Add(documentId);
            return Task.FromResult<IDocumentMetadata?>(metadata);
        }

        public Task<bool> AttachDocumentAsync(WindowId targetWindowId, IDocumentMetadata metadata, int indexHint = -1, bool shouldSelect = true)
        {
            // Add to store and fire attached
            var id = metadata.DocumentId == Guid.Empty ? Guid.NewGuid() : metadata.DocumentId;
            this.store[id] = metadata;
            this.DocumentAttached?.Invoke(this, new DocumentAttachedEventArgs(targetWindowId, metadata, indexHint));
            return Task.FromResult(true);
        }

        public Task<bool> UpdateMetadataAsync(WindowId windowId, Guid documentId, IDocumentMetadata metadata)
        {
            // Ensure the metadata contains the document id so the presenter can find the tab.
            IDocumentMetadata effective;
            if (metadata is TestDocumentMetadata tm)
            {
                tm.DocumentId = documentId;
                effective = tm;
            }
            else
            {
                effective = new TestDocumentMetadata
                {
                    DocumentId = documentId,
                    Title = metadata.Title ?? string.Empty,
                    IconUri = metadata is { } ? metadata!.IconUri : null,
                    IsDirty = metadata is { } && metadata!.IsDirty,
                    IsPinnedHint = metadata is { } && metadata!.IsPinnedHint,
                };
            }

            this.store[documentId] = effective;
            this.DocumentMetadataChanged?.Invoke(this, new DocumentMetadataChangedEventArgs(windowId, effective));
            return Task.FromResult(true);
        }

        public Task<bool> SelectDocumentAsync(WindowId windowId, Guid documentId)
        {
            this.DocumentActivated?.Invoke(this, new DocumentActivatedEventArgs(windowId, documentId));
            return Task.FromResult(true);
        }

        // Return all stored documents for test purposes
        public IReadOnlyList<IDocumentMetadata> GetOpenDocuments(WindowId windowId)
            => [.. this.store.Values];

        // Return the first document's ID if any exist, otherwise null
        public Guid? GetActiveDocumentId(WindowId windowId)
            => this.store.Keys.FirstOrDefault();
    }

    private sealed class TestDocumentMetadata : IDocumentMetadata
    {
        public Guid DocumentId { get; set; } = Guid.NewGuid();

        public string Title { get; set; } = string.Empty;

        public Uri? IconUri { get; set; }

        public bool IsDirty { get; set; }

        public bool IsPinnedHint { get; set; }

        public bool IsClosable { get; set; } = true;
    }
}
