// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for edge cases and error conditions in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsEdgeCases : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task RegisterWindowAsync_WhenDisposed_ThrowsObjectDisposedException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();
        sut.Dispose();

        var testWindow = MakeSmallWindow();

        // Act & Assert
        var act = async () => await sut.RegisterWindowAsync(testWindow).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);

        testWindow.Close();
    });

    [TestMethod]
    public Task RegisterDecoratedWindowAsync_WhenDisposed_ThrowsObjectDisposedException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();
        sut.Dispose();

        var testWindow = MakeSmallWindow();

        // Act & Assert
        var act = async () => await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);

        testWindow.Close();
    });

    [TestMethod]
    public Task CloseWindowAsync_WhenDisposed_ThrowsObjectDisposedException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

        sut.Dispose();

        try
        {
            // Act & Assert
            var act = async () => await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            _ = await act.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);
        }
        finally
        {
            testWindow.Close();
        }
    });

    [TestMethod]
    public Task MinimizeWindowAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.MinimizeWindowAsync(missingId).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task MaximizeWindowAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.MaximizeWindowAsync(missingId).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RestoreWindowAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.RestoreWindowAsync(missingId).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task MoveWindowAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            var position = new Windows.Graphics.PointInt32 { X = 100, Y = 100 };

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.MoveWindowAsync(missingId, position).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ResizeWindowAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            var size = new Windows.Graphics.SizeInt32 { Width = 400, Height = 300 };

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.ResizeWindowAsync(missingId, size).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowBoundsAsync_WithInvalidWindowId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            var bounds = new Windows.Graphics.RectInt32 { X = 100, Y = 100, Width = 400, Height = 300 };

            // Act & Assert - Should not throw, operation is silently skipped
            await sut.SetWindowBoundsAsync(missingId, bounds).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_WithNullWindow_ThrowsArgumentNullException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Act & Assert
            var act = async () => await sut.RegisterWindowAsync(null!).ConfigureAwait(true);
            _ = await act.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterDecoratedWindowAsync_WithNullWindow_ThrowsArgumentNullException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Act & Assert
            var act = async () => await sut.RegisterDecoratedWindowAsync(null!, new("Test")).ConfigureAwait(true);
            _ = await act.Should().ThrowAsync<ArgumentNullException>().ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task OpenWindows_IsReadOnly_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Assert
            _ = sut.OpenWindows.Should().BeAssignableTo<IReadOnlyCollection<IManagedWindow>>();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActiveWindow_UpdatesOnActivation_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context1 = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            var context2 = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);

            // Act
            sut.ActivateWindow(context1.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = sut.ActiveWindow.Should().NotBeNull();
            _ = sut.ActiveWindow!.Id.Should().Be(context1.Id);

            // Act - Activate second window
            sut.ActivateWindow(context2.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = sut.ActiveWindow!.Id.Should().Be(context2.Id);
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActiveWindow_IsNullWhenNoWindowActive_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Assert - No windows registered yet
            _ = sut.ActiveWindow.Should().BeNull();
        }
        finally
        {
            sut.Dispose();
        }

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterWindowAsync_WithSystemCategory_Succeeds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            // Act - RegisterWindowAsync uses WindowCategory.System by default
            var context = await sut.RegisterWindowAsync(testWindow).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Category.Should().Be(WindowCategory.System);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task Dispose_CanBeCalledMultipleTimes_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        // Act & Assert - Should not throw
        sut.Dispose();
        sut.Dispose();
        sut.Dispose();

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task RegisterDecoratedWindowAsync_WithEmptyMetadata_Succeeds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        var emptyMetadata = new Dictionary<string, object>(StringComparer.Ordinal);

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test"), emptyMetadata).ConfigureAwait(true);

            // Assert
            _ = context.Metadata.Should().NotBeNull();
            _ = context.Metadata.Should().BeEmpty();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task OpenWindows_ReflectsCurrentState_AfterRegistrationAndClosure_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            // Initially empty
            _ = sut.OpenWindows.Should().BeEmpty();

            // Register first window
            var context1 = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().ContainSingle();

            // Register second window
            var context2 = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().HaveCount(2);

            // Close first window
            _ = await sut.CloseWindowAsync(context1.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().ContainSingle();
            _ = sut.OpenWindows.First().Id.Should().Be(context2.Id);

            // Close second window
            _ = await sut.CloseWindowAsync(context2.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RestoredBounds_InitializedWithCurrentBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert - RestoredBounds should be initialized with current bounds
            _ = context.RestoredBounds.Should().NotBe(default);
            _ = context.RestoredBounds.X.Should().Be(context.CurrentBounds.X);
            _ = context.RestoredBounds.Y.Should().Be(context.CurrentBounds.Y);
            _ = context.RestoredBounds.Width.Should().Be(context.CurrentBounds.Width);
            _ = context.RestoredBounds.Height.Should().Be(context.CurrentBounds.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
