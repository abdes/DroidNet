// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Coordinates;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Fluent builder for creating configured <see cref="Mock{ITabStrip}"/> instances.
/// Provides discoverable API for setting up common test scenarios.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed partial class TabStripMockBuilder
{
    private string name = "MockTabStrip";
    private IReadOnlyList<TabStripItemSnapshot>? snapshots;
    private Func<SpatialPoint<ElementSpace>, bool>? hitTestFunc;
    private Func<SpatialPoint<ElementSpace>, double, bool>? hitTestThresholdFunc;
    private Rect? bounds;

    /// <summary>
    /// Sets the name of the TabStrip.
    /// </summary>
    /// <param name="value">The name to use.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithName(string value)
    {
        this.name = value;
        return this;
    }

    /// <summary>
    /// Sets the snapshots to be returned by TakeSnapshot().
    /// </summary>
    /// <param name="value">The snapshots to return.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithSnapshots(IReadOnlyList<TabStripItemSnapshot> value)
    {
        this.snapshots = value;
        return this;
    }

    /// <summary>
    /// Sets custom hit-test behavior.
    /// </summary>
    /// <param name="func">Function to determine hit-test results.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithHitTest(Func<SpatialPoint<ElementSpace>, bool> func)
    {
        this.hitTestFunc = func;
        return this;
    }

    /// <summary>
    /// Sets custom threshold hit-test behavior.
    /// </summary>
    /// <param name="func">Function to determine threshold hit-test results.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithHitTestThreshold(Func<SpatialPoint<ElementSpace>, double, bool> func)
    {
        this.hitTestThresholdFunc = func;
        return this;
    }

    /// <summary>
    /// Sets bounds for default hit-test behavior.
    /// </summary>
    /// <param name="value">The bounds rectangle.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithBounds(Rect value)
    {
        this.bounds = value;
        return this;
    }

    /// <summary>
    /// Configures the mock to always return true for hit tests.
    /// </summary>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder AlwaysHit()
    {
        this.hitTestFunc = _ => true;
        this.hitTestThresholdFunc = (_, _) => true;
        return this;
    }

    /// <summary>
    /// Configures the mock to always return false for hit tests.
    /// </summary>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder NeverHit()
    {
        this.hitTestFunc = _ => false;
        this.hitTestThresholdFunc = (_, _) => false;
        return this;
    }

    /// <summary>
    /// Configures a single-item snapshot for the dragged item at the specified index.
    /// </summary>
    /// <param name="draggedItemIndex">The index of the dragged item.</param>
    /// <param name="layoutOrigin">The layout origin (default: 0,0).</param>
    /// <param name="width">The item width (default: 120).</param>
    /// <returns>This builder for fluent chaining.</returns>
    public TabStripMockBuilder WithDraggedItemSnapshot(int draggedItemIndex, Point? layoutOrigin = null, double width = 120)
    {
        var origin = layoutOrigin ?? new Point(0, 0);
        this.snapshots = new List<TabStripItemSnapshot>
        {
            new()
            {
                ItemIndex = draggedItemIndex,
                LayoutOrigin = new SpatialPoint<ElementSpace>(origin),
                Width = width,
            },
        }.AsReadOnly();
        return this;
    }

    /// <summary>
    /// Builds the configured <see cref="Mock{ITabStrip}"/> instance.
    /// </summary>
    /// <returns>A configured Mock instance ready for use.</returns>
    [SuppressMessage("Design", "MA0051:Method is too long", Justification = "it's ok, having the mock setup in a single place is more clear")]
    public Mock<ITabStrip> Build()
    {
        var mock = new Mock<ITabStrip>();
        var actualBounds = this.bounds ?? new Rect(0, 0, 200, 40);

        // Setup name
        _ = mock.SetupGet(m => m.Name).Returns(this.name);

        // Setup unified threshold hit-test (interface now exposes HitTestWithThreshold which returns an index >=0 on hit)
        if (this.hitTestThresholdFunc != null)
        {
            _ = mock.Setup(m => m.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()))
                .Returns<SpatialPoint<ElementSpace>, double>((point, threshold) => this.hitTestThresholdFunc(point, threshold) ? 0 : -1);
        }
        else if (this.hitTestFunc != null)
        {
            // Convert simple boolean hit-test to the new signature (0 = hit, -1 = miss)
            _ = mock.Setup(m => m.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()))
                .Returns<SpatialPoint<ElementSpace>, double>((point, threshold) => this.hitTestFunc(point) ? 0 : -1);
        }
        else
        {
            // Default bounds-based threshold hit-test (returns 0 for hit, -1 for miss)
            _ = mock.Setup(m => m.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()))
                .Returns<SpatialPoint<ElementSpace>, double>((point, threshold) =>
                {
                    var p = point.Point;
                    var hit = p.X >= actualBounds.Left + threshold && p.X <= actualBounds.Right - threshold
                        && p.Y >= actualBounds.Top + threshold && p.Y <= actualBounds.Bottom - threshold;
                    return hit ? 0 : -1;
                });
        }

        // Setup snapshots: return fresh copies on each call to avoid mutating caller-owned snapshot objects
        if (this.snapshots != null)
        {
            _ = mock.Setup(m => m.TakeSnapshot()).Returns(() =>
                this.snapshots.Select(s => new TabStripItemSnapshot
                {
                    ItemIndex = s.ItemIndex,
                    LayoutOrigin = s.LayoutOrigin,
                    Width = s.Width,
                    Offset = s.Offset,
                    Container = s.Container,
                }).ToList().AsReadOnly());
        }
        else
        {
            _ = mock.Setup(m => m.TakeSnapshot()).Returns(() => new List<TabStripItemSnapshot>().AsReadOnly());
        }

        // Setup no-op methods
        _ = mock.Setup(m => m.RequestPreviewImage(It.IsAny<object>(), It.IsAny<DragVisualDescriptor>()));
        _ = mock.Setup(m => m.CloseTab(It.IsAny<object>()));
        _ = mock.Setup(m => m.TearOutTab(It.IsAny<object>(), It.IsAny<SpatialPoint<ScreenSpace>>()));
        _ = mock.Setup(m => m.TryCompleteDrag(It.IsAny<object>(), It.IsAny<ITabStrip?>(), It.IsAny<int?>()));
        _ = mock.Setup(m => m.ApplyTransformToItem(It.IsAny<int>(), It.IsAny<double>()));
        _ = mock.Setup(m => m.RemoveItemAt(It.IsAny<int>()));
        _ = mock.Setup(m => m.MoveItem(It.IsAny<int>(), It.IsAny<int>()));
        _ = mock.Setup(m => m.InsertItemAt(It.IsAny<int>(), It.IsAny<object>()));
        _ = mock.Setup(m => m.GetContainerForIndex(It.IsAny<int>())).Returns((FrameworkElement?)null);

        return mock;
    }

    /// <summary>
    /// Builds the configured mock and a FrameworkElement-backed wrapper that delegates
    /// ITabStrip calls to the mock. Useful for tests that need a strip instance which
    /// can be hit-tested (the coordinator casts registered strips to FrameworkElement).
    /// </summary>
    /// <returns>Tuple of (Mock, wrapper element).</returns>
    public (Mock<ITabStrip> mock, FrameworkElement element) BuildWithElement()
    {
        var mock = this.Build();
        var wrapper = new DelegatingTabStrip(mock.Object);
        return (mock, wrapper);
    }

    private sealed partial class DelegatingTabStrip(ITabStrip inner) : ContentControl, ITabStrip
    {
        private readonly ITabStrip inner = inner ?? throw new ArgumentNullException(nameof(inner));

        string ITabStrip.Name => this.inner.Name;

        public int HitTestWithThreshold(SpatialPoint<ElementSpace> elementPoint, double threshold)
            => this.inner.HitTestWithThreshold(elementPoint, threshold);

        public void RequestPreviewImage(object item, DragVisualDescriptor descriptor)
            => this.inner.RequestPreviewImage(item, descriptor);

        public void CloseTab(object item) => this.inner.CloseTab(item);

        public void TearOutTab(object item, SpatialPoint<ScreenSpace> dropPoint) => this.inner.TearOutTab(item, dropPoint);

        public void TryCompleteDrag(object item, ITabStrip? destinationStrip, int? newIndex)
        {
            // If the coordinator passes the wrapper instance as the destination, forward the
            // inner mock instance instead so tests that verify against the mock see the
            // expected reference. Otherwise, forward the destination as-is.
            var forwardedDestination = ReferenceEquals(destinationStrip, this) ? this.inner : destinationStrip;
            this.inner.TryCompleteDrag(item, forwardedDestination, newIndex);
        }

        public IReadOnlyList<TabStripItemSnapshot> TakeSnapshot() => this.inner.TakeSnapshot();

        public FrameworkElement? GetContainerForIndex(int index) => this.inner.GetContainerForIndex(index);

        public void ApplyTransformToItem(int itemIndex, double offsetX) => this.inner.ApplyTransformToItem(itemIndex, offsetX);

        public void RemoveItemAt(int index) => this.inner.RemoveItemAt(index);

        public void MoveItem(int fromIndex, int toIndex) => this.inner.MoveItem(fromIndex, toIndex);

        public void InsertItemAt(int index, object item) => this.inner.InsertItemAt(index, item);

        public Task<RealizationResult> InsertItemAsync(int index, object item, CancellationToken cancellationToken, int timeoutMs = 500)
            => this.inner.InsertItemAsync(index, item, cancellationToken, timeoutMs);

        public Task<ExternalDropPreparationResult?> PrepareExternalDropAsync(object payload, SpatialPoint<ElementSpace> pointerPosition, CancellationToken cancellationToken, int timeoutMs = 500)
            => this.inner.PrepareExternalDropAsync(payload, pointerPosition, cancellationToken, timeoutMs);
    }
}
