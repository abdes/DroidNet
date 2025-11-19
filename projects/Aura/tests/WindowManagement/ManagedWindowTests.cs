// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for <see cref="IManagedWindow"/> interface operations.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class ManagedWindowTests : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task ManagedWindow_Properties_ArePopulatedCorrectly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("TestCategory")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = context.Id.Should().Be(testWindow.AppWindow.Id);
            _ = context.Window.Should().BeSameAs(testWindow);
            _ = context.Category.Should().Be(new WindowCategory("TestCategory"));
            _ = context.DispatcherQueue.Should().NotBeNull();
            _ = context.CreatedAt.Should().BeCloseTo(DateTimeOffset.UtcNow, TimeSpan.FromSeconds(5));
            _ = context.CurrentBounds.Should().NotBe(default);
            _ = context.RestoredBounds.Should().NotBe(default);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_IsActive_UpdatesOnActivation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act
            sut.ActivateWindow(context.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = context.IsActive.Should().BeTrue();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_LastActivatedAt_IsSetOnActivation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var beforeActivation = DateTimeOffset.UtcNow;

            // Act
            sut.ActivateWindow(context.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = context.LastActivatedAt.Should().NotBeNull();
            _ = context.LastActivatedAt!.Value.Should().BeOnOrAfter(beforeActivation);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_IsMinimized_ReturnsCorrectState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act - minimize window
            await context.MinimizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMinimized().Should().BeTrue();
            _ = context.IsMaximized().Should().BeFalse();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_IsMaximized_ReturnsCorrectState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act - maximize window
            await context.MaximizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMaximized().Should().BeTrue();
            _ = context.IsMinimized().Should().BeFalse();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_MinimizeAsync_ChangesState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act
            await context.MinimizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMinimized().Should().BeTrue();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_MaximizeAsync_ChangesState_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act
            await context.MaximizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMaximized().Should().BeTrue();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_RestoreAsync_RestoresFromMinimized_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            await context.MinimizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Act
            await context.RestoreAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMinimized().Should().BeFalse();
            _ = context.IsMaximized().Should().BeFalse();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_RestoreAsync_RestoresFromMaximized_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            await context.MaximizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Act
            await context.RestoreAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.IsMaximized().Should().BeFalse();
            _ = context.IsMinimized().Should().BeFalse();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_MoveAsync_UpdatesPosition_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var newPosition = new Windows.Graphics.PointInt32 { X = 150, Y = 200 };

            // Act
            await context.MoveAsync(newPosition).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.CurrentBounds.X.Should().Be(newPosition.X);
            _ = context.CurrentBounds.Y.Should().Be(newPosition.Y);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_ResizeAsync_UpdatesSize_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var newSize = new Windows.Graphics.SizeInt32 { Width = 400, Height = 300 };

            // Act
            await context.ResizeAsync(newSize).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.CurrentBounds.Width.Should().Be(newSize.Width);
            _ = context.CurrentBounds.Height.Should().Be(newSize.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_SetBoundsAsync_UpdatesPositionAndSize_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var newBounds = new Windows.Graphics.RectInt32
            {
                X = 100,
                Y = 100,
                Width = 500,
                Height = 400,
            };

            // Act
            await context.SetBoundsAsync(newBounds).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = context.CurrentBounds.X.Should().Be(newBounds.X);
            _ = context.CurrentBounds.Y.Should().Be(newBounds.Y);
            _ = context.CurrentBounds.Width.Should().Be(newBounds.Width);
            _ = context.CurrentBounds.Height.Should().Be(newBounds.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_RestoredBounds_CapturedBeforeMinimize_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var originalBounds = context.CurrentBounds;

            // Act
            await context.MinimizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - RestoredBounds should match the original bounds before minimize
            _ = context.RestoredBounds.X.Should().Be(originalBounds.X);
            _ = context.RestoredBounds.Y.Should().Be(originalBounds.Y);
            _ = context.RestoredBounds.Width.Should().Be(originalBounds.Width);
            _ = context.RestoredBounds.Height.Should().Be(originalBounds.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_RestoredBounds_CapturedBeforeMaximize_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var originalBounds = context.CurrentBounds;

            // Act
            await context.MaximizeAsync().ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - RestoredBounds should match the original bounds before maximize
            _ = context.RestoredBounds.X.Should().Be(originalBounds.X);
            _ = context.RestoredBounds.Y.Should().Be(originalBounds.Y);
            _ = context.RestoredBounds.Width.Should().Be(originalBounds.Width);
            _ = context.RestoredBounds.Height.Should().Be(originalBounds.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_Decorations_CanBeUpdated_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var originalDecorations = context.Decorations;

            // Act
            var newDecorations = new WindowDecorationOptions
            {
                Category = new("Test"),
                ChromeEnabled = true,
                Backdrop = BackdropKind.Mica,
            };

            context.Decorations = newDecorations;

            // Assert
            _ = context.Decorations.Should().BeSameAs(newDecorations);
            _ = context.Decorations.Should().NotBeSameAs(originalDecorations);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ManagedWindow_CurrentBounds_ReflectsRealTimeBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act - Move window directly via AppWindow
            var newPosition = new Windows.Graphics.PointInt32 { X = 300, Y = 300 };
            testWindow.AppWindow.Move(newPosition);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - CurrentBounds should reflect the actual window position
            _ = context.CurrentBounds.X.Should().Be(newPosition.X);
            _ = context.CurrentBounds.Y.Should().Be(newPosition.Y);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
