// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Aura.Documents;
using DroidNet.Coordinates;
using DroidNet.Documents;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Controls.Tests;

/// <summary>
///     A tab dragged out of a window's strip moves its document: the strip it is dropped on attaches it, a drop outside
///     every strip attaches it to a new window, and a document that no window takes goes back where it came from.
/// </summary>
public partial class DocumentTabPresenterTests
{
    private const ulong SourceWindow = 101;
    private const ulong TargetWindow = 102;
    private const ulong NewWindow = 103;

    [TestMethod]
    public Task ADraggedOutTab_IsAttachedWhereItIsDropped_Async() => EnqueueAsync(async () =>
    {
        var (source, target) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        var transfers = new DocumentTransfers();
        using var sourcePresenter = this.Present(source, ds, SourceWindow, transfers);
        using var targetPresenter = this.Present(target, ds, TargetWindow, transfers);
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];

        // Act: the drag leaves the source strip, enters the target strip, which inserts the tab, and drops it there.
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dropped = (TabItem)dragged.ShallowClone();
        target.Items.Insert(0, dropped);
        target.TryCompleteDrag(dropped, target, newIndex: 0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.DetachedIds.Should().Equal(meta.DocumentId);
        _ = ds.Attached.Should().Equal((new WindowId(TargetWindow), meta.DocumentId, 0));
        _ = source.Items.Should().BeEmpty();
        _ = target.Items.Should().ContainSingle().Which.Should().BeSameAs(dropped, "the dropped tab is the document's tab");
    });

    [TestMethod]
    public Task ATabDroppedOutsideEveryStrip_GoesToTheWindowCreatedForIt_Async() => EnqueueAsync(async () =>
    {
        var (source, _) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        var windows = new Mock<IDocumentWindowFactory>();
        _ = windows.Setup(value => value.CreateWindowAsync(It.IsAny<IDocumentMetadata>(), It.IsAny<SpatialPoint<PhysicalScreenSpace>>()))
            .ReturnsAsync(new WindowId(NewWindow));
        using var presenter = this.Present(source, ds, SourceWindow, new DocumentTransfers(windows.Object));
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];
        var dropPoint = new Point(400, 300).AsScreen();

        // Act
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        source.TearOutTab(dragged, dropPoint);
        source.TryCompleteDrag(dragged, destinationStrip: null, newIndex: null);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert: the window opens where the tab was dropped, in physical pixels.
        var expected = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, source).ToPhysicalScreen(dropPoint);
        windows.Verify(value => value.CreateWindowAsync(meta, expected), Times.Once());
        _ = ds.Attached.Should().Equal((new WindowId(NewWindow), meta.DocumentId, -1));
    });

    [TestMethod]
    public Task ATornOutTabWithoutAWindow_GoesBackWhereItCameFrom_Async() => EnqueueAsync(async () =>
    {
        var (source, _) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        using var presenter = this.Present(source, ds, SourceWindow, new DocumentTransfers());
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];

        // Act
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        source.TearOutTab(dragged, new SpatialPoint<ScreenSpace>(new Point(400, 300)));
        source.TryCompleteDrag(dragged, destinationStrip: null, newIndex: null);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.Attached.Should().Equal((new WindowId(SourceWindow), meta.DocumentId, -1));
        _ = source.Items.Should().ContainSingle().Which.ContentId.Should().Be(meta.DocumentId.Value);
    });

    [TestMethod]
    public Task ATabRefusedWhereItIsDropped_GoesBackWhereItCameFrom_Async() => EnqueueAsync(async () =>
    {
        var (source, target) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        _ = ds.RefusesAttach.Add(new WindowId(TargetWindow));
        var transfers = new DocumentTransfers();
        using var sourcePresenter = this.Present(source, ds, SourceWindow, transfers);
        using var targetPresenter = this.Present(target, ds, TargetWindow, transfers);
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];

        // Act
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dropped = (TabItem)dragged.ShallowClone();
        target.Items.Insert(0, dropped);
        target.TryCompleteDrag(dropped, target, newIndex: 0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.Attached.Should().Equal((new WindowId(SourceWindow), meta.DocumentId, -1));
        _ = target.Items.Should().BeEmpty("the refused tab is not the target's");
        _ = source.Items.Should().ContainSingle().Which.ContentId.Should().Be(meta.DocumentId.Value);
    });

    [TestMethod]
    public Task ADragPassingOverAStrip_TakesTheTabItInsertedAlong_Async() => EnqueueAsync(async () =>
    {
        var (source, target) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        var transfers = new DocumentTransfers();
        using var sourcePresenter = this.Present(source, ds, SourceWindow, transfers);
        using var targetPresenter = this.Present(target, ds, TargetWindow, transfers);
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var passing = (TabItem)dragged.ShallowClone();
        target.Items.Insert(0, passing);

        // Act: the drag leaves the target strip again.
        target.DetachTab(passing);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = target.Items.Should().BeEmpty();
        _ = ds.DetachedIds.Should().Equal(meta.DocumentId);
        _ = ds.Attached.Should().BeEmpty("the drag still moves the document");
    });

    /// <summary>
    ///     A drag that ended in another window leaves no tab of its document in a strip it only passed over, even when
    ///     that strip never saw the drag leave.
    /// </summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public Task ATabLeftByADragThatEndedElsewhere_IsRemoved_Async() => EnqueueAsync(async () =>
    {
        var (source, target) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        var transfers = new DocumentTransfers();
        using var sourcePresenter = this.Present(source, ds, SourceWindow, transfers);
        using var targetPresenter = this.Present(target, ds, TargetWindow, transfers);
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        target.Items.Insert(0, (TabItem)dragged.ShallowClone());

        // Act: the drag ends on the source strip, which takes the document back.
        source.TryCompleteDrag(dragged, destinationStrip: null, newIndex: null);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.Attached.Should().Equal((new WindowId(SourceWindow), meta.DocumentId, -1));
        _ = target.Items.Should().BeEmpty();
        _ = source.Items.Should().ContainSingle().Which.ContentId.Should().Be(meta.DocumentId.Value);
    });

    /// <summary>
    ///     A strip whose tabs come from another source, such as a window of another project, does not take a document:
    ///     only the source that detached it attaches it again, back where it came from.
    /// </summary>
    /// <returns>A task for the test.</returns>
    [TestMethod]
    public Task ATabDroppedOnAStripOfAnotherSource_GoesBackWhereItCameFrom_Async() => EnqueueAsync(async () =>
    {
        var (source, target) = await this.CreateStripsAsync().ConfigureAwait(true);
        var sourceTabs = new TestDocumentTabSource();
        var otherTabs = new TestDocumentTabSource();
        var transfers = new DocumentTransfers();
        using var sourcePresenter = this.Present(source, sourceTabs, SourceWindow, transfers);
        using var targetPresenter = this.Present(target, otherTabs, TargetWindow, transfers);
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await sourceTabs.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dragged = source.Items[0];
        source.DetachTab(dragged);
        await WaitForRenderCompletion().ConfigureAwait(true);
        var dropped = (TabItem)dragged.ShallowClone();
        target.Items.Insert(0, dropped);

        // Act
        target.TryCompleteDrag(dropped, target, newIndex: 0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = otherTabs.Attached.Should().BeEmpty();
        _ = sourceTabs.Attached.Should().Equal((new WindowId(SourceWindow), meta.DocumentId, -1));
        _ = target.Items.Should().BeEmpty();
        _ = source.Items.Should().ContainSingle().Which.ContentId.Should().Be(meta.DocumentId.Value);
    });

    [TestMethod]
    public Task ATabReorderedWithinItsStrip_MovesNoDocument_Async() => EnqueueAsync(async () =>
    {
        var (source, _) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        using var presenter = this.Present(source, ds, SourceWindow, new DocumentTransfers());
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        source.TryCompleteDrag(source.Items[0], source, newIndex: 0);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.DetachedIds.Should().BeEmpty();
        _ = ds.Attached.Should().BeEmpty();
        _ = source.Items.Should().ContainSingle();
    });

    [TestMethod]
    public Task WithoutTransfers_ADraggedOutTabKeepsItsDocument_Async() => EnqueueAsync(async () =>
    {
        var (source, _) = await this.CreateStripsAsync().ConfigureAwait(true);
        var ds = new TestDocumentTabSource();
        using var presenter = new DocumentTabPresenter(source, ds, ManagedWindowFactory.CreateForWindow(VisualUserInterfaceTestsApp.MainWindow, SourceWindow), DispatcherQueue.GetForCurrentThread());
        var meta = new TestDocumentMetadata { Title = "Material" };
        _ = await ds.OpenDocumentAsync(new WindowId(SourceWindow), meta).ConfigureAwait(true);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Act
        source.DetachTab(source.Items[0]);
        await WaitForRenderCompletion().ConfigureAwait(true);

        // Assert
        _ = ds.DetachedIds.Should().BeEmpty();
        _ = source.Items.Should().ContainSingle();
    });

    // Two strips, each standing in for the tab strip of a window.
    private async Task<(TestableTabStrip Source, TestableTabStrip Target)> CreateStripsAsync()
    {
        var source = this.CreateTabStrip(0);
        var target = this.CreateTabStrip(0);
        var panel = new StackPanel();
        panel.Children.Add(source);
        panel.Children.Add(target);
        await LoadTestContentAsync(panel).ConfigureAwait(true);
        return (source, target);
    }

    private DocumentTabPresenter Present(TabStrip strip, IDocumentTabSource ds, ulong window, DocumentTransfers transfers)
        => new(
            strip,
            ds,
            ManagedWindowFactory.CreateForWindow(VisualUserInterfaceTestsApp.MainWindow, window),
            DispatcherQueue.GetForCurrentThread(),
            this.mockLoggerFactory.Object.CreateLogger<DocumentTabPresenter>(),
            transfers);
}
