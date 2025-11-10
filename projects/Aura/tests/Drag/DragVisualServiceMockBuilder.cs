// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Coordinates;
using Moq;

namespace DroidNet.Aura.Drag.Tests;

/// <summary>
///     Fluent builder for creating <see cref="Mock{IDragVisualService}"/> instances in drag tests.
/// </summary>
internal sealed class DragVisualServiceMockBuilder
{
    private DragSessionToken sessionToken = new() { Id = Guid.NewGuid() };
    private Action<DragVisualDescriptor, SpatialPoint<PhysicalScreenSpace>, SpatialPoint<ScreenSpace>>? startSessionCallback;
    private DragVisualDescriptor? descriptor;

    /// <summary>
    ///     Configures the session token returned by <see cref="IDragVisualService.StartSession"/>.
    /// </summary>
    /// <param name="token">The <see cref="DragSessionToken"/> to be returned by <c>StartSession</c>.</param>
    public DragVisualServiceMockBuilder WithSessionToken(DragSessionToken token)
    {
        this.sessionToken = token;
        return this;
    }

    /// <summary>
    ///     Configures the descriptor returned by <see cref="IDragVisualService.GetDescriptor"/>.
    /// </summary>
    /// <param name="descriptor">The <see cref="DragVisualDescriptor"/> to be returned by <c>GetDescriptor</c>.</param>
    public DragVisualServiceMockBuilder WithDescriptor(DragVisualDescriptor descriptor)
    {
        this.descriptor = descriptor;
        return this;
    }

    /// <summary>
    ///     Configures a callback to capture descriptor, initial position, and hotspot offsets when
    ///     <see cref="IDragVisualService.StartSession"/> is called.
    /// </summary>
    /// <param name="callback">
    /// The callback to invoke when <see cref="IDragVisualService.StartSession"/> is called, receiving the descriptor,
    /// initial position in <see cref="PhysicalScreenSpace"/>, and hotspot offset in <see cref="ScreenSpace"/>.
    /// </param>
    public DragVisualServiceMockBuilder CaptureStartSession(
        Action<DragVisualDescriptor, SpatialPoint<PhysicalScreenSpace>, SpatialPoint<ScreenSpace>> callback)
    {
        this.startSessionCallback = callback;
        return this;
    }

    /// <summary>
    ///     Builds the configured <see cref="Mock{IDragVisualService}"/> with strict behavior.
    /// </summary>
    public Mock<IDragVisualService> Build()
    {
        var mock = new Mock<IDragVisualService>(MockBehavior.Strict);

        var startSetup = mock.Setup(s => s.StartSession(
            It.IsAny<DragVisualDescriptor>(),
            It.IsAny<SpatialPoint<PhysicalScreenSpace>>(),
            It.IsAny<SpatialPoint<ScreenSpace>>()));
        if (this.startSessionCallback is not null)
        {
            _ = startSetup.Callback(this.startSessionCallback);
        }

        _ = startSetup.Returns(this.sessionToken);

        _ = mock.Setup(s => s.UpdatePosition(It.IsAny<DragSessionToken>(), It.IsAny<SpatialPoint<PhysicalScreenSpace>>()));
        _ = mock.Setup(s => s.EndSession(It.IsAny<DragSessionToken>()));

        // Setup GetDescriptor if a descriptor was provided
        if (this.descriptor is not null)
        {
            _ = mock.Setup(s => s.GetDescriptor(this.sessionToken)).Returns(this.descriptor);
        }

        return mock;
    }
}
