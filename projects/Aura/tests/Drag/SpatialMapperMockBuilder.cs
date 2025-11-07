// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Collections.Generic;
using DroidNet.Coordinates;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Fluent builder for creating configured <see cref="Mock{ISpatialMapper}"/> instances for testing.
/// </summary>
internal sealed class SpatialMapperMockBuilder
{
    private IReadOnlyList<SpatialPoint<PhysicalScreenSpace>>? physicalResponses;
    private WindowInfo? windowInfo;
    private WindowMonitorInfo? monitorInfo;

    /// <summary>
    /// Configures the mapper to return a sequence of physical screen points when ToPhysicalScreen is called.
    /// Each call dequeues the next point in the sequence.
    /// </summary>
    /// <param name="responses">The sequence of physical screen points to return.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public SpatialMapperMockBuilder WithPhysicalResponses(IReadOnlyList<SpatialPoint<PhysicalScreenSpace>> responses)
    {
        this.physicalResponses = responses;
        return this;
    }

    /// <summary>
    /// Configures the window information returned by the mapper.
    /// </summary>
    /// <param name="info">The window information.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public SpatialMapperMockBuilder WithWindowInfo(WindowInfo info)
    {
        this.windowInfo = info;
        return this;
    }

    /// <summary>
    /// Configures the monitor information returned by the mapper.
    /// </summary>
    /// <param name="info">The monitor information.</param>
    /// <returns>This builder for fluent chaining.</returns>
    public SpatialMapperMockBuilder WithMonitorInfo(WindowMonitorInfo info)
    {
        this.monitorInfo = info;
        return this;
    }

    /// <summary>
    /// Builds the configured <see cref="Mock{ISpatialMapper}"/> instance.
    /// </summary>
    /// <returns>A configured Mock instance ready for use.</returns>
    public Mock<ISpatialMapper> Build()
    {
        var mock = new Mock<ISpatialMapper>(MockBehavior.Strict);

        // Setup default or configured WindowInfo
        var actualWindowInfo = this.windowInfo ?? new WindowInfo(
            new Point(0, 0),
            new Size(800, 600),
            new Point(0, 0),
            new Size(800, 600),
            96);
        _ = mock.SetupGet(m => m.WindowInfo).Returns(actualWindowInfo);

        // Setup default or configured WindowMonitorInfo
        var actualMonitorInfo = this.monitorInfo ?? new WindowMonitorInfo(IntPtr.Zero, 0, 0, 0, 0, 96);
        _ = mock.SetupGet(m => m.WindowMonitorInfo).Returns(actualMonitorInfo);

        // Setup ToPhysicalScreen with queued responses
        if (this.physicalResponses is { Count: > 0 })
        {
            var queue = new Queue<SpatialPoint<PhysicalScreenSpace>>(this.physicalResponses);
            var last = queue.Peek();

            _ = mock.Setup(m => m.ToPhysicalScreen(It.IsAny<SpatialPoint<ScreenSpace>>()))
                .Returns<SpatialPoint<ScreenSpace>>(_ =>
                {
                    if (queue.Count > 0)
                    {
                        last = queue.Dequeue();
                    }

                    return last;
                });
        }
        else
        {
            // Default: return a simple 2x scale transformation
            _ = mock.Setup(m => m.ToPhysicalScreen(It.IsAny<SpatialPoint<ScreenSpace>>()))
                .Returns<SpatialPoint<ScreenSpace>>(point =>
                    new SpatialPoint<PhysicalScreenSpace>(new Point(point.Point.X * 2, point.Point.Y * 2)));
        }

        return mock;
    }
}
