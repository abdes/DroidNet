// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Coordinates;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Fluent builder for creating configured <see cref="Mock{ITabStrip}"/> instances.
/// Provides discoverable API for setting up common test scenarios.
/// </summary>
[ExcludeFromCodeCoverage]
internal sealed class TabStripMockBuilder
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
    public Mock<ITabStrip> Build()
    {
        var mock = new Mock<ITabStrip>();
        var actualBounds = this.bounds ?? new Rect(0, 0, 200, 40);

        // Setup name
        _ = mock.SetupGet(m => m.Name).Returns(this.name);

        // Setup hit-test
        if (this.hitTestFunc != null)
        {
            _ = mock.Setup(m => m.HitTest(It.IsAny<SpatialPoint<ElementSpace>>()))
                .Returns(this.hitTestFunc);
        }
        else
        {
            // Default bounds-based hit-test
            _ = mock.Setup(m => m.HitTest(It.IsAny<SpatialPoint<ElementSpace>>()))
                .Returns<SpatialPoint<ElementSpace>>(point =>
                {
                    var p = point.Point;
                    return p.X >= actualBounds.Left && p.X <= actualBounds.Right
                        && p.Y >= actualBounds.Top && p.Y <= actualBounds.Bottom;
                });
        }

        // Setup threshold hit-test
        if (this.hitTestThresholdFunc != null)
        {
            _ = mock.Setup(m => m.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()))
                .Returns(this.hitTestThresholdFunc);
        }
        else
        {
            // Default bounds-based threshold hit-test
            _ = mock.Setup(m => m.HitTestWithThreshold(It.IsAny<SpatialPoint<ElementSpace>>(), It.IsAny<double>()))
                .Returns<SpatialPoint<ElementSpace>, double>((point, threshold) =>
                {
                    var p = point.Point;
                    return p.X >= actualBounds.Left + threshold && p.X <= actualBounds.Right - threshold
                        && p.Y >= actualBounds.Top + threshold && p.Y <= actualBounds.Bottom - threshold;
                });
        }

        // Setup snapshots
        if (this.snapshots != null)
        {
            _ = mock.Setup(m => m.TakeSnapshot()).Returns(this.snapshots);
        }
        else
        {
            _ = mock.Setup(m => m.TakeSnapshot()).Returns(new List<TabStripItemSnapshot>().AsReadOnly());
        }

        // Setup no-op methods
        mock.Setup(m => m.RequestPreviewImage(It.IsAny<object>(), It.IsAny<DragVisualDescriptor>()));
        mock.Setup(m => m.CloseTab(It.IsAny<object>()));
        mock.Setup(m => m.TearOutTab(It.IsAny<object>(), It.IsAny<SpatialPoint<ScreenSpace>>()));
        mock.Setup(m => m.CompleteDrag(It.IsAny<object>(), It.IsAny<ITabStrip>(), It.IsAny<int?>()));
        mock.Setup(m => m.ApplyTransformToItem(It.IsAny<int>(), It.IsAny<double>()));
        mock.Setup(m => m.RemoveItemAt(It.IsAny<int>()));
        mock.Setup(m => m.InsertItemAt(It.IsAny<int>(), It.IsAny<object>()));

        return mock;
    }
}
