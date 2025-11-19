// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window bounds and positioning operations in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsBounds : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task MoveWindowAsync_RaisesWindowBoundsChangedAndUpdatesCurrentBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var boundsEvents = new List<WindowBoundsChangedEventArgs>();
        sut.WindowBoundsChanged += (s, e) =>
        {
            boundsEvents.Add(e);
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            boundsEvents.Clear();

            var position = new Windows.Graphics.PointInt32 { X = 100, Y = 75 };

            // Act
            await sut.MoveWindowAsync(context.Id, position).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = boundsEvents.Should().Contain(e => e.WindowId == context.Id && e.Bounds.X == position.X && e.Bounds.Y == position.Y);
            _ = context.CurrentBounds.X.Should().Be(position.X);
            _ = context.CurrentBounds.Y.Should().Be(position.Y);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ResizeWindowAsync_RaisesWindowBoundsChangedAndUpdatesCurrentBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var boundsEvents = new List<WindowBoundsChangedEventArgs>();
        sut.WindowBoundsChanged += (s, e) =>
        {
            boundsEvents.Add(e);
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            boundsEvents.Clear();

            var size = new Windows.Graphics.SizeInt32 { Width = 300, Height = 200 };

            // Act
            await sut.ResizeWindowAsync(context.Id, size).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = boundsEvents.Should().Contain(e => e.WindowId == context.Id && e.Bounds.Width == size.Width && e.Bounds.Height == size.Height);
            _ = context.CurrentBounds.Width.Should().Be(size.Width);
            _ = context.CurrentBounds.Height.Should().Be(size.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowBoundsAsync_WhenNotRestored_UpdatesRestoredBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var newBounds = new Windows.Graphics.RectInt32 { X = 50, Y = 50, Width = 400, Height = 300 };

            // Act - Set bounds while in restored state
            await sut.SetWindowBoundsAsync(context.Id, newBounds).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Now maximize the window
            await sut.MaximizeWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - RestoredBounds should preserve the bounds we set before maximizing
            _ = context.RestoredBounds.Should().NotBeNull();
            _ = context.RestoredBounds.X.Should().Be(newBounds.X);
            _ = context.RestoredBounds.Y.Should().Be(newBounds.Y);
            _ = context.RestoredBounds.Width.Should().Be(newBounds.Width);
            _ = context.RestoredBounds.Height.Should().Be(newBounds.Height);

            // CurrentBounds will reflect the maximized state, not the newBounds
            _ = context.IsMaximized().Should().BeTrue();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
