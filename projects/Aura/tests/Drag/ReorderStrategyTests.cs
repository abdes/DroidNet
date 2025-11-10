// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Controls;
using DroidNet.Coordinates;
using DroidNet.Tests;
using FluentAssertions;
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
        var setup = this.StartDrag(visualElement: visual, stripElement: stripElement);

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
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedItem: CreateTabItem("DraggedTab"),
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
        var tabItem = CreateTabItem();
        var mockStrip = new TabStripMockBuilder().Build();
        var visual = new Border();

        // Create context without calling StartDrag (which would overwrite the mock setup)
        var context = this.CreateDragContext(mockStrip, tabItem, 0, visual, stripElement: null, out var stripRoot);

        // Set up TakeSnapshot to throw AFTER creating context but BEFORE InitiateDrag
        _ = mockStrip.Setup(m => m.TakeSnapshot()).Throws(new InvalidOperationException("snapshot failed"));

        // Act & Assert - InitiateDrag should propagate the exception
        var act = () => strategy.InitiateDrag(context, this.ToPhysical(50, 25));
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
        var sourceStrip = new TabStripMockBuilder().WithName("Source").Build();
        var setup = this.StartDrag(stripMock: sourceStrip);

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
        var mockStrip = new TabStripMockBuilder().Build();
        var item1 = CreateTabItem("Tab1");
        var item2 = CreateTabItem("Tab2");

        // Act - First usage
        var firstUsage = this.StartDrag(strategy: strategy, stripMock: mockStrip, draggedItem: item1);
        _ = firstUsage.Strategy.IsActive.Should().BeTrue();

        _ = strategy.CompleteDrag(drop: false);
        _ = strategy.IsActive.Should().BeFalse();

        // Act - Second usage
        var secondUsage = this.StartDrag(
            strategy: strategy,
            stripMock: mockStrip,
            draggedItem: item2,
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
        var mockStrip = new TabStripMockBuilder().Build();
        var tabItem = CreateTabItem();
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            stripMock: mockStrip,
            draggedItem: tabItem);

        // Act
        _ = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        mockStrip.VerifyMoveItem(It.IsAny<int>(), It.IsAny<int>(), Times.Never());
        mockStrip.Verify(m => m.InsertItemAt(It.IsAny<int>(), tabItem), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_RemovesTransform_AfterCommit_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var draggedItem = CreateTabItem("DraggedTab");
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            draggedItem: draggedItem,
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
        var draggedItem = CreateTabItem("DraggedTab");
        var mockStrip = new TabStripMockBuilder().Build();
        var snapshots = new List<TabStripItemSnapshot>
        {
            new()
            {
                ItemIndex = 0,
                LayoutOrigin = new Point(0, 0).AsElement(),
                Width = DefaultItemWidth,
            },
            new()
            {
                ItemIndex = 1,
                LayoutOrigin = new Point(DefaultItemWidth + 10, 0).AsElement(),
                Width = DefaultItemWidth,
            },
            new()
            {
                ItemIndex = 2,
                LayoutOrigin = new Point((DefaultItemWidth + 10) * 2, 0).AsElement(),
                Width = DefaultItemWidth,
            },
        }.AsReadOnly();
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            stripMock: mockStrip,
            draggedItem: draggedItem,
            draggedIndex: 0,
            visualElement: new Border(),
            startX: DefaultItemWidth / 2,
            snapshots: snapshots);

        // Nudge the pointer slightly away from exact boundaries to avoid floating-point
        // tie-breaking that can make this test flaky on some platforms.
        setup.Strategy.OnDragPositionChanged(this.ToPhysical((DefaultItemWidth + 10) * 1.5, DefaultItemHeight / 2));

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().Be(1); // Should return the final drop index after moving past the neighbor
        mockStrip.VerifyMoveItem(0, 1, Times.Once());
        mockStrip.Verify(m => m.InsertItemAt(It.IsAny<int>(), draggedItem), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_DoesNotRemoveItem_OnSameStripDrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var draggedItem = CreateTabItem("DraggedTab");
        var sourceStrip = new TabStripMockBuilder().WithName("Source").Build();
        var setup = this.StartDrag(
            strategy: new ReorderStrategy(this.LoggerFactory),
            stripMock: sourceStrip,
            draggedItem: draggedItem,
            visualElement: new Border());

        // Act
        var result = setup.Strategy.CompleteDrag(drop: true);

        // Assert
        _ = result.Should().NotBeNull();
        sourceStrip.VerifyRemoveItemAt(It.IsAny<int>(), Times.Never());

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task CompleteDrag_ResetsAllState_AfterCompletion_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var strategy = new ReorderStrategy(this.LoggerFactory);
        var draggedItem = CreateTabItem("DraggedTab");
        var mockStrip = new TabStripMockBuilder().Build();
        var initialDrag = this.StartDrag(
            strategy: strategy,
            stripMock: mockStrip,
            draggedItem: draggedItem,
            visualElement: new Border());
        _ = initialDrag.Strategy.IsActive.Should().BeTrue();

        // Act
        _ = strategy.CompleteDrag(drop: false);

        // Assert
        _ = strategy.IsActive.Should().BeFalse();

        var newItem = CreateTabItem("NewTab");
        var resumedDrag = this.StartDrag(
            strategy: strategy,
            stripMock: mockStrip,
            draggedItem: newItem,
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

    private DragSetup StartDrag(
        ReorderStrategy? strategy = null,
        Mock<ITabStrip>? stripMock = null,
        object? draggedItem = null,
        int draggedIndex = 0,
        FrameworkElement? visualElement = null,
        FrameworkElement? stripElement = null,
        double startX = 50,
        double startY = 25,
        IReadOnlyList<TabStripItemSnapshot>? snapshots = null)
    {
        var activeStrategy = strategy ?? new ReorderStrategy();
        var activeStrip = stripMock ?? new TabStripMockBuilder().Build();

        // Always setup snapshot for the current draggedIndex (overwrites any previous setup)
        var snapshotItems = snapshots ?? new List<TabStripItemSnapshot>
        {
            new()
            {
                ItemIndex = draggedIndex,
                LayoutOrigin = new Point(0, 0).AsElement(),
                Width = DefaultItemWidth,
            },
        }.AsReadOnly();

        // Return fresh copies on each TakeSnapshot call to avoid mutating caller-owned snapshots
        _ = activeStrip.Setup(m => m.TakeSnapshot()).Returns(() =>
            snapshotItems.Select(s => new TabStripItemSnapshot
            {
                ItemIndex = s.ItemIndex,
                LayoutOrigin = s.LayoutOrigin,
                Width = s.Width,
                Offset = s.Offset,
                Container = s.Container,
            }).ToList().AsReadOnly());

        var item = draggedItem ?? CreateTabItem();

        // Convert element coordinates to physical screen coordinates
        var startPhysical = this.ToPhysical(startX, startY);

        // Create a context and let CreateDragContext compute the hotspot in element-space after layout.
        var context = this.CreateDragContext(
            activeStrip,
            item,
            draggedIndex,
            visualElement,
            stripElement,
            out _,
            startPhysical: startPhysical,
            snapshotsForHotspot: snapshots ?? snapshotItems);

        activeStrategy.InitiateDrag(context, startPhysical);
        return new DragSetup(activeStrategy, activeStrip, context);
    }

    private DragContext CreateDragContext(
        Mock<ITabStrip> strip,
        object draggedItem,
        int draggedIndex,
        FrameworkElement? visualElement,
        FrameworkElement? stripElement,
        out FrameworkElement stripRoot,
        SpatialPoint<PhysicalScreenSpace>? startPhysical = null,
        IReadOnlyList<TabStripItemSnapshot>? snapshotsForHotspot = null,
        Windows.Foundation.Point? hotspotOffsets = null)
    {
        stripRoot = this.PrepareStripElement(stripElement);
        var element = this.PrepareVisualElement(visualElement, stripRoot);

        if (element.RenderTransform is not TranslateTransform)
        {
            element.RenderTransform = new TranslateTransform();
        }

        stripRoot.UpdateLayout();
        element.UpdateLayout();

        var localMapper = new SpatialMapper(VisualUserInterfaceTestsApp.MainWindow, stripRoot);

        // Determine hotspot offsets (element-space). Priority:
        // 1) Explicit hotspotOffsets param
        // 2) If a physical start point and snapshots are provided, convert the physical point to element space
        //    and compute offset relative to the snapshot's LayoutOrigin
        // 3) Default to (0,0)
        Windows.Foundation.Point offsets;
        if (hotspotOffsets is not null)
        {
            offsets = hotspotOffsets.Value;
        }
        else if (startPhysical is not null && snapshotsForHotspot?.Count > 0)
        {
            var elementPoint = localMapper.ToElement(startPhysical.Value);
            var snap = snapshotsForHotspot.FirstOrDefault(p => p.ItemIndex == draggedIndex) ?? snapshotsForHotspot[0];
            offsets = new Windows.Foundation.Point(elementPoint.Point.X - snap.LayoutOrigin.Point.X, elementPoint.Point.Y - snap.LayoutOrigin.Point.Y);
        }
        else
        {
            offsets = new Windows.Foundation.Point(0, 0);
        }

        return new DragContext(
            strip.Object,
            draggedItem,
            draggedIndex,
            offsets,
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
