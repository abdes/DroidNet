// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using DroidNet.Coordinates;
using Moq;
using Windows.Foundation;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
/// Fluent builder for creating <see cref="Mock{IDragVisualService}"/> instances in drag tests.
/// </summary>
internal sealed class DragVisualServiceMockBuilder
{
    private DragSessionToken sessionToken = new() { Id = Guid.NewGuid() };
    private Action<DragVisualDescriptor, Point>? startSessionCallback;
    private DragVisualDescriptor? descriptor;

    /// <summary>
    /// Configures the session token returned by <see cref="IDragVisualService.StartSession"/>.
    /// </summary>
    public DragVisualServiceMockBuilder WithSessionToken(DragSessionToken token)
    {
        this.sessionToken = token;
        return this;
    }

    /// <summary>
    /// Configures the descriptor returned by <see cref="IDragVisualService.GetDescriptor"/>.
    /// </summary>
    public DragVisualServiceMockBuilder WithDescriptor(DragVisualDescriptor descriptor)
    {
        this.descriptor = descriptor;
        return this;
    }

    /// <summary>
    /// Configures a callback to capture descriptor and hotspot when <see cref="IDragVisualService.StartSession"/> is called.
    /// </summary>
    public DragVisualServiceMockBuilder CaptureStartSession(Action<DragVisualDescriptor, Point> callback)
    {
        this.startSessionCallback = callback;
        return this;
    }

    /// <summary>
    /// Builds the configured <see cref="Mock{IDragVisualService}"/> with strict behavior.
    /// </summary>
    public Mock<IDragVisualService> Build()
    {
        var mock = new Mock<IDragVisualService>(MockBehavior.Strict);

        var startSetup = mock.Setup(s => s.StartSession(It.IsAny<DragVisualDescriptor>(), It.IsAny<Point>()));
        if (this.startSessionCallback is not null)
        {
            startSetup.Callback(this.startSessionCallback);
        }

        startSetup.Returns(this.sessionToken);

        mock.Setup(s => s.UpdatePosition(It.IsAny<DragSessionToken>(), It.IsAny<SpatialPoint<PhysicalScreenSpace>>()));
        mock.Setup(s => s.EndSession(It.IsAny<DragSessionToken>()));

        // Setup GetDescriptor if a descriptor was provided
        if (this.descriptor is not null)
        {
            mock.Setup(s => s.GetDescriptor(this.sessionToken)).Returns(this.descriptor);
        }

        return mock;
    }
}
