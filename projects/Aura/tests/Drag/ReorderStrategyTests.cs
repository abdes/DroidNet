// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Controls;
using DroidNet.Coordinates;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Unit tests for <see cref="ReorderStrategy"/> drag strategy implementation.
/// Tests validate transform application, item displacement logic, and drag completion
/// during in-TabStrip drag operations.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("ReorderStrategyTests")]
[TestCategory("UITest")]
public class ReorderStrategyTests : VisualUserInterfaceTests
{
    private const double DefaultItemWidth = 120;

    private const double DefaultItemHeight = 40;

    private Grid? testRoot;

    private StackPanel? stripHost;

    private SpatialMapper? mapper;

    public TestContext TestContext { get; set; }

    [TestMethod]
    public Task StrategyCanBeInstantiated_WithDefaultLogger_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var strategy = new ReorderStrategy();

        // Assert
        _ = strategy.Should().NotBeNull();
        _ = strategy.IsActive.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task MultipleStrategies_CanBeCreated_Independently_Async() => EnqueueAsync(async () =>
    {
        // Arrange & Act
        var strategy1 = new ReorderStrategy();
        var strategy2 = new ReorderStrategy();
        var strategy3 = new ReorderStrategy();

        // Assert
        _ = strategy1.Should().NotBeNull();
        _ = strategy2.Should().NotBeNull();
        _ = strategy3.Should().NotBeNull();
        _ = strategy1.Should().NotBe(strategy2);
        _ = strategy2.Should().NotBe(strategy3);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_RequiresNonNullContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var startPoint = this.ToPhysical(10, 10);

        // Act & Assert
        var act = () => strategy.InitiateDrag(null!, startPoint);
        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("context");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ActivatesStrategy_WithValidContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag();

        // Assert
        _ = setup.Strategy.IsActive.Should().BeTrue();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_ThrowsWhenAlreadyActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag();

        // Act & Assert
        var act = () => setup.Strategy.InitiateDrag(setup.Context, this.ToPhysical(50, 25));
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("*already active*");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesNullSourceVisualItem_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(visualElement: null);

        // Assert
        _ = setup.Strategy.IsActive.Should().BeTrue();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_AppliesTransformToSourceVisualItem_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var visual = new Border();
        var stripElement = new Border();
        var setup = this.StartDrag(visualElement: visual);

        // Assert
        _ = setup.Context.DraggedVisualElement.RenderTransform.Should().BeOfType<TranslateTransform>();
        var transform = (TranslateTransform)setup.Context.DraggedVisualElement.RenderTransform;
        _ = transform.X.Should().Be(0);
        _ = transform.Y.Should().Be(0);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_CallsTakeSnapshot_OnSourceStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(strategy: new ReorderStrategy(this.LoggerFactory));

        // Assert
        setup.Strip.VerifyTakeSnapshot(Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_DoesNotInsertItems_OnSourceStrip_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), DefaultItemWidth)
            .WithItemSnapshot(1, "DraggedTab", new Point(DefaultItemWidth + 10, 0), DefaultItemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedIndex: 1,
            visualElement: new Border());

        // Assert
        setup.Strip.Verify(m => m.InsertItemAt(It.IsAny<int>(), It.IsAny<object>()), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task InitiateDrag_HandlesSnapshotFailure_Gracefully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var builder = new TabStripMockBuilder().WithItemSnapshot(0, "TestTab", new Point(0, 0), DefaultItemWidth);
        var tabItem = builder.GetItem(0);
        var mockStrip = builder.Build();
        var visual = new Border();

        var startPhysical = this.ToPhysical(50, 25);

        // Create context without calling StartDrag (which would overwrite the mock setup)
        var context = this.CreateDragContext(mockStrip, tabItem, new Point(50, 25), visual);

        // Set up TakeSnapshot to throw AFTER creating context but BEFORE InitiateDrag
        _ = mockStrip.Setup(m => m.TakeSnapshot()).Throws(new InvalidOperationException("snapshot failed"));

        // Act & Assert - InitiateDrag should propagate the exception
        var act = () => strategy.InitiateDrag(context, startPhysical);
        _ = act.Should().Throw<InvalidOperationException>().WithMessage("snapshot failed");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();

        // Act & Assert
        var act = () => strategy.OnDragPositionChanged(this.ToPhysical(100, 50));
        _ = act.Should().NotThrow();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_DoesNotTransformWithoutOverlap_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(visualElement: new Border());

        var initialTransform = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        var initialX = initialTransform?.X ?? 0;

        // Act
        setup.Strategy.OnDragPositionChanged(this.ToPhysical(150, 75));

        // Assert
        var updatedTransform = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        _ = updatedTransform.Should().NotBeNull();
        _ = updatedTransform!.X.Should().Be(initialX);
        setup.Strip.VerifyApplyTransform(Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_DoesNotTransformWithoutOverlap_WithLogging_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(strategy: new ReorderStrategy(this.LoggerFactory), visualElement: new Border(), startX: 100, startY: 50);

        var initialTransform = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        var initialX = initialTransform?.X ?? 0;

        // Act
        setup.Strategy.OnDragPositionChanged(this.ToPhysical(150, 75));

        // Assert
        var updatedTransform = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        _ = updatedTransform.Should().NotBeNull();
        _ = updatedTransform!.X.Should().Be(initialX, "No horizontal movement should occur when cursor moves vertically");
        setup.Strip.VerifyApplyTransform(Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task OnDragPositionChanged_DoesNotApplyHotspotTransformWithoutDisplacement_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            visualElement: new Border(),
            startX: 100,
            startY: 50);

        // Act
        setup.Strategy.OnDragPositionChanged(this.ToPhysical(150, 50));

        // Assert
        var transform = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        _ = transform.Should().NotBeNull();
        _ = transform!.X.Should().Be(0);
        setup.Strip.VerifyApplyTransform(Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_IsIgnored_WhenStrategyNotActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();

        // Act
        var result = strategy.CompleteDrag(drop: false);

        // Assert
        _ = result.Should().BeNull();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesCrossStripDrop_ByDelegating_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = new TabStripMockBuilder()
            .WithName("Source")
            .WithItemSnapshot(0, "TestTab", new Point(0, 0), DefaultItemWidth);
        var sourceStrip = builder.Build();
        var draggedItem = builder.GetItem(0);
        var setup = this.StartDrag(stripMock: sourceStrip, draggedItem: draggedItem);

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().NotBeNull();
        _ = setup.Strategy.IsActive.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_CompletesSuccessfully_WhenActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(strategy: new ReorderStrategy(this.LoggerFactory));

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().NotBeNull();
        _ = setup.Strategy.IsActive.Should().BeFalse();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task StrategyCanBeReused_AfterCompleteDrag_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy();
        var builder1 = CreateDefaultStripBuilder("Tab1");

        // Act - First usage
        var firstUsage = this.StartDrag(builder: builder1, strategy: strategy);
        _ = firstUsage.Strategy.IsActive.Should().BeTrue();

        _ = strategy.CompleteDrag(drop: false);
        _ = strategy.IsActive.Should().BeFalse();

        // Act - Second usage
        var builder2 = CreateDefaultStripBuilder("Tab2");
        var secondUsage = this.StartDrag(
            builder: builder2,
            strategy: strategy,
            startX: 100,
            startY: 50);

        // Assert
        _ = secondUsage.Strategy.IsActive.Should().BeTrue();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_HandlesDropOnSameStrip_Successfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var setup = this.StartDrag(strategy: new ReorderStrategy(this.LoggerFactory), visualElement: new Border());

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().NotBeNull();
        _ = setup.Context.DraggedVisualElement.RenderTransform.Should().BeOfType<TranslateTransform>();
        var transform = (TranslateTransform)setup.Context.DraggedVisualElement.RenderTransform;
        _ = transform.X.Should().Be(0);
        _ = transform.Y.Should().Be(0);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_DoesNotInvokeMove_WhenDropIndexUnchanged_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = CreateDefaultStripBuilder();
        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory));

        // Act
        _ = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        var tabItem = builder.GetItem(0);
        setup.Strip.VerifyMoveItem(It.IsAny<int>(), It.IsAny<int>(), Times.Never());
        setup.Strip.Verify(m => m.InsertItemAt(It.IsAny<int>(), tabItem), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_RemovesTransform_AfterCommit_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = CreateDefaultStripBuilder("DraggedTab");
        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            visualElement: new Border());

        _ = (setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform).Should().NotBeNull();

        // Act
        _ = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        var transformAfterDrop = setup.Context.DraggedVisualElement.RenderTransform as TranslateTransform;
        _ = transformAfterDrop.Should().NotBeNull();
        _ = transformAfterDrop!.X.Should().Be(0);
        _ = transformAfterDrop.Y.Should().Be(0);

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_CommitsReorderAtTargetPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "DraggedTab", new Point(0, 0), DefaultItemWidth)
            .WithItemSnapshot(1, "Tab1", new Point(DefaultItemWidth + 10, 0), DefaultItemWidth)
            .WithItemSnapshot(2, "Tab2", new Point((DefaultItemWidth + 10) * 2, 0), DefaultItemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedIndex: 0,
            visualElement: new Border(),
            startX: DefaultItemWidth / 2);

        // Nudge the pointer slightly away from exact boundaries to avoid floating-point
        // tie-breaking that can make this test flaky on some platforms.
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((DefaultItemWidth + 10) * 1.5, DefaultItemHeight / 2));

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        var draggedItem = builder.GetItem(0);
        _ = result.Should().Be(1); // Should return the final drop index after moving past the neighbor
        setup.Strip.VerifyMoveItem(0, 1, Times.Once());
        setup.Strip.Verify(m => m.InsertItemAt(It.IsAny<int>(), draggedItem), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task SlidesIntoGap_AlignsEdges_NoOverlap_MultipleSlides_Async() => EnqueueAsync(async () =>
    {
        // Arrange - four evenly spaced tabs to emulate the 'New Tab' scenario
        const double itemWidth = DefaultItemWidth;
        const int spacing = 10;
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), itemWidth)
            .WithItemSnapshot(1, "Tab1", new Point(itemWidth + spacing, 0), itemWidth)
            .WithItemSnapshot(2, "Tab2", new Point((itemWidth + spacing) * 2, 0), itemWidth)
            .WithItemSnapshot(3, "Tab3", new Point((itemWidth + spacing) * 3, 0), itemWidth);

        var setup = this.StartDrag(builder: builder, strategy: new ReorderStrategy(this.LoggerFactory), draggedIndex: 0, visualElement: new Border(), startX: itemWidth / 2);

        // Capture offsets applied by the TabStrip mock for verification
        var appliedOffsets = new Dictionary<Guid, double>();
        _ = setup.Strip.Setup(m => m.ApplyTransformToItem(It.IsAny<Guid>(), It.IsAny<double>()))
            .Callback<Guid, double>((id, offset) => appliedOffsets[id] = offset);

        // Act - slide past first neighbor (index 1)
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((itemWidth + spacing) * 1.5, DefaultItemHeight / 2));

        // Assert - first neighbor (Tab1) should have been moved left to align to gapLeft (0)
        var tab1 = builder.GetItem(1);
        const double tab1Origin = itemWidth + spacing;
        const double expectedTab1Offset = 0 - tab1Origin; // alignedLeft - originalLayoutOrigin (0 - 130)
        _ = appliedOffsets.Should().ContainKey(tab1.ContentId);
        _ = appliedOffsets[tab1.ContentId].Should().BeApproximately(expectedTab1Offset, 0.5);

        // Act - continue sliding past the second neighbor (index 2)
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((itemWidth + spacing) * 2.5, DefaultItemHeight / 2));

        // Assert - second neighbor (Tab2) should have been moved left to align to new gapLeft (130)
        var tab2 = builder.GetItem(2);
        const double tab2Origin = (itemWidth + spacing) * 2;
        const double expectedTab2Offset = tab1Origin - tab2Origin; // alignedLeft 130 - layoutOrigin(260) => -130
        _ = appliedOffsets.Should().ContainKey(tab2.ContentId);
        _ = appliedOffsets[tab2.ContentId].Should().BeApproximately(expectedTab2Offset, 0.5);

        // Final Overlap Check - compute transformed bounds and ensure no overlap
        var tab1Left = tab1Origin + appliedOffsets[tab1.ContentId]; // original layout origin + offset => should be 0
        var tab1Right = tab1Left + itemWidth;
        var tab2Left = tab2Origin + appliedOffsets[tab2.ContentId];
        var tab2Right = tab2Left + itemWidth;

        _ = tab1Right.Should().BeLessThanOrEqualTo(tab2Left, "Tab1 must not overlap Tab2 after sliding into gaps");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task ResolveGapVisualIndex_MapsToCorrectDropIndex_AfterSlides_Async() => EnqueueAsync(async () =>
    {
        // Arrange - 7 tabs to simulate frequently seen cases
        const double itemWidth = DefaultItemWidth;
        const int spacing = 10;
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), itemWidth)
            .WithItemSnapshot(1, "Tab1", new Point((itemWidth + spacing) * 1, 0), itemWidth)
            .WithItemSnapshot(2, "Tab2", new Point((itemWidth + spacing) * 2, 0), itemWidth)
            .WithItemSnapshot(3, "Tab3", new Point((itemWidth + spacing) * 3, 0), itemWidth)
            .WithItemSnapshot(4, "DraggedTab", new Point((itemWidth + spacing) * 4, 0), itemWidth)
            .WithItemSnapshot(5, "Tab5", new Point((itemWidth + spacing) * 5, 0), itemWidth)
            .WithItemSnapshot(6, "Tab6", new Point((itemWidth + spacing) * 6, 0), itemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedIndex: 4,
            visualElement: new Border(),
            startX: ((itemWidth + spacing) * 4) + (itemWidth / 2));

        // Act - move the pointer a bit past the middle of Tab5 but not enough to cross Tab6
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((itemWidth + spacing) * 5.5, DefaultItemHeight / 2));

        // Commit drop
        var resultIndex = setup.Strategy.CompleteDrag(drop: true);

        // The dragged item should end up after Tab5 => new index should be 5
        _ = resultIndex.Should().Be(5);

        // Validate that MoveItem called with from 4 to 5
        setup.Strip.VerifyMoveItem(4, 5, Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task ResolveGapVisualIndex_BeforeFirstSlot_ReturnsZero_Async() => EnqueueAsync(async () =>
    {
        // Arrange - 3 tabs, start dragging the middle one to the left
        const double itemWidth = DefaultItemWidth;
        const int spacing = 10;
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), itemWidth)
            .WithItemSnapshot(1, "DraggedTab", new Point(itemWidth + spacing, 0), itemWidth)
            .WithItemSnapshot(2, "Tab2", new Point((itemWidth + spacing) * 2, 0), itemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedIndex: 1,
            visualElement: new Border(),
            startX: ((itemWidth + spacing) * 1) + (itemWidth / 2));

        // Act - slide beyond the leftmost slot
        setup.Strategy.OnDragPositionChanged(this.ToPhysical(0, DefaultItemHeight / 2));

        // Assert - completing drop should produce index 0
        var result = setup.Strategy.CompleteDrag(drop: true);
        _ = result.Should().Be(0);
        setup.Strip.VerifyMoveItem(1, 0, Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task ResolveGapVisualIndex_AfterLastSlot_ReturnsLastIndex_Async() => EnqueueAsync(async () =>
    {
        // Arrange - 3 tabs, start dragging the middle one to the right
        const double itemWidth = DefaultItemWidth;
        const int spacing = 10;
        var builder = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), itemWidth)
            .WithItemSnapshot(1, "DraggedTab", new Point(itemWidth + spacing, 0), itemWidth)
            .WithItemSnapshot(2, "Tab2", new Point((itemWidth + spacing) * 2, 0), itemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedIndex: 1,
            visualElement: new Border(),
            startX: ((itemWidth + spacing) * 1) + (itemWidth / 2));

        // Act - slide to the position after the last slot
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((itemWidth + spacing) * 2.5, DefaultItemHeight / 2));

        // Assert - completing drop should produce last index 2
        var result = setup.Strategy.CompleteDrag(drop: true);
        _ = result.Should().Be(2);
        setup.Strip.VerifyMoveItem(1, 2, Times.Once());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_DoesNotRemoveItem_OnSameStripDrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var builder = new TabStripMockBuilder()
            .WithName("Source")
            .WithItemSnapshot(0, "DraggedTab", new Point(0, 0), DefaultItemWidth);

        var setup = this.StartDrag(
            builder: builder,
            strategy: new ReorderStrategy(this.LoggerFactory),
            visualElement: new Border());

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().NotBeNull();
        setup.Strip.VerifyRemoveItemAt(It.IsAny<int>(), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ResetsAllState_AfterCompletion_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var builder1 = CreateDefaultStripBuilder("DraggedTab");
        var initialDrag = this.StartDrag(
            builder: builder1,
            strategy: strategy,
            visualElement: new Border());
        _ = initialDrag.Strategy.IsActive.Should().BeTrue();

        // Act
        _ = strategy.CompleteDrag(drop: false);

        // Assert
        _ = strategy.IsActive.Should().BeFalse();

        var builder2 = new TabStripMockBuilder()
            .WithItemSnapshot(0, "Tab0", new Point(0, 0), DefaultItemWidth)
            .WithItemSnapshot(1, "NewTab", new Point(DefaultItemWidth + 10, 0), DefaultItemWidth);
        var resumedDrag = this.StartDrag(
            builder: builder2,
            strategy: strategy,
            draggedIndex: 1,
            visualElement: new Border(),
            startX: 100,
            startY: 50);
        _ = resumedDrag.Strategy.IsActive.Should().BeTrue();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    protected override async Task TestSetupAsync()
    {
        await base.TestSetupAsync().ConfigureAwait(true);

        this.testRoot = new Grid();
        this.stripHost = new StackPanel
        {
            Orientation = Orientation.Horizontal,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
            Spacing = 8,
        };

        this.testRoot.Children.Add(this.stripHost);
        await LoadTestContentAsync(this.testRoot).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        // Initialize the localMapper with the main window and the stripHost as the reference element
        this.mapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, this.stripHost);
    }

    private static TabItem CreateTabItem(string header = "TestTab") => new() { Header = header };

    private static Border CreateTestVisualElement()
        => new()
        {
            Width = DefaultItemWidth,
            Height = DefaultItemHeight,
            HorizontalAlignment = HorizontalAlignment.Left,
            VerticalAlignment = VerticalAlignment.Top,
        };

    /// <summary>
    /// Creates a configured mock strip builder with a single item at default position.
    /// </summary>
    private static TabStripMockBuilder CreateDefaultStripBuilder(string itemHeader = "TestTab", int itemIndex = 0)
        => new TabStripMockBuilder()
            .WithItemSnapshot(itemIndex, itemHeader, new Point(0, 0), DefaultItemWidth);

    /// <summary>
    /// Converts element-space coordinates to physical screen space using the localMapper.
    /// </summary>
    private SpatialPoint<PhysicalScreenSpace> ToPhysical(double x, double y)
    {
        if (this.mapper is null)
        {
            throw new InvalidOperationException("Mapper not initialized. Ensure TestSetupAsync has been called.");
        }

        var elementPoint = new Point(x, y).AsElement();
        return this.mapper.Convert<ElementSpace, PhysicalScreenSpace>(elementPoint);
    }

    /// <summary>
    /// Initiates a drag operation with the provided builder and returns the setup context.
    /// </summary>
    private DragSetup StartDrag(
        TabStripMockBuilder builder,
        ReorderStrategy? strategy = null,
        int draggedIndex = 0,
        FrameworkElement? visualElement = null,
        double? startX = null,
        double startY = 25,
        Point? hotspotOffset = null)
    {
        var activeStrategy = strategy ?? new ReorderStrategy();
        var mockStrip = builder.Build();
        var draggedItem = builder.GetItem(draggedIndex);

        // Use provided startX or default to item's left + 60 pixels
        var effectiveStartX = startX ?? 60;
        var startPhysical = this.ToPhysical(effectiveStartX, startY);

        // Use provided hotspot offset or default to (60, 25)
        var effectiveHotspotOffset = hotspotOffset ?? new Point(60, 25);

        var context = this.CreateDragContext(mockStrip, draggedItem, effectiveHotspotOffset, visualElement);

        activeStrategy.InitiateDrag(context, startPhysical);
        return new DragSetup(activeStrategy, mockStrip, context);
    }

    /// <summary>
    /// Overload for backward compatibility - creates a default strip builder if none provided.
    /// </summary>
    private DragSetup StartDrag(
        ReorderStrategy? strategy = null,
        Mock<ITabStrip>? stripMock = null,
        TabItem? draggedItem = null,
        int draggedIndex = 0,
        FrameworkElement? visualElement = null,
        double? startX = null,
        double startY = 25,
        Point? hotspotOffset = null)
    {
        // If a stripMock is provided, use legacy path for backward compatibility
        if (stripMock != null)
        {
            var activeStrategy = strategy ?? new ReorderStrategy();
            var item = draggedItem ?? CreateTabItem();
            var effectiveStartX = startX ?? 60;
            var effectiveHotspotOffset = hotspotOffset ?? new Point(60, 25);

            var startPhysical = this.ToPhysical(effectiveStartX, startY);
            var context = this.CreateDragContext(stripMock, item, effectiveHotspotOffset, visualElement);
            activeStrategy.InitiateDrag(context, startPhysical);
            return new DragSetup(activeStrategy, stripMock, context);
        }

        // Otherwise create a default builder
        var builder = draggedItem != null
            ? new TabStripMockBuilder().WithItemSnapshot(draggedIndex, draggedItem.Header ?? "TestTab", new Point(0, 0), DefaultItemWidth)
            : CreateDefaultStripBuilder("TestTab", draggedIndex);

        return this.StartDrag(builder, strategy, draggedIndex, visualElement, startX, startY, hotspotOffset);
    }

    private DragContext CreateDragContext(
        Mock<ITabStrip> strip,
        IDragPayload draggedItem,
        Point hotspotOffset,
        FrameworkElement? visualElement)
    {
        var stripRoot = this.PrepareStripElement(stripElement: null);
        var element = this.PrepareVisualElement(visualElement, stripRoot);

        if (element.RenderTransform is not TranslateTransform)
        {
            element.RenderTransform = new TranslateTransform();
        }

        stripRoot.UpdateLayout();
        element.UpdateLayout();

        var localMapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, stripRoot);

        return new DragContext(
            strip.Object,
            draggedItem,
            hotspotOffset,
            stripRoot,
            element,
            localMapper);
    }

    private FrameworkElement PrepareStripElement(FrameworkElement? stripElement)
    {
        var root = stripElement ?? this.stripHost ?? throw new InvalidOperationException("Strip host must be initialized before creating a drag context.");

        if (root.Parent is null && this.testRoot is Panel panel)
        {
            panel.Children.Add(root);
        }

        return root;
    }

    private FrameworkElement PrepareVisualElement(FrameworkElement? visualElement, FrameworkElement stripRoot)
    {
        var element = visualElement ?? CreateTestVisualElement();

        if (ReferenceEquals(element, stripRoot))
        {
            return element;
        }

        if (element.Parent is null)
        {
            switch (stripRoot)
            {
                case Panel panel when !panel.Children.Contains(element):
                    panel.Children.Add(element);
                    break;
                case ContentControl contentControl when contentControl.Content is null:
                    contentControl.Content = element;
                    break;
                case Border border when border.Child is null:
                    border.Child = element;
                    break;
                default:
                    if (this.stripHost is Panel fallback && !fallback.Children.Contains(element))
                    {
                        fallback.Children.Add(element);
                    }

                    break;
            }
        }

        if (element.RenderTransform is not TranslateTransform)
        {
            element.RenderTransform = new TranslateTransform();
        }

        return element;
    }

    private sealed record DragSetup(ReorderStrategy Strategy, Mock<ITabStrip> Strip, DragContext Context);
}
