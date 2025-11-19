// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window lifecycle operations (close, activate, disposal) in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsLifecycle : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task CloseWindowAsync_WithValidId_ClosesWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var result = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);

            // Assert
            _ = result.Should().BeTrue();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseWindowAsync_RemovesFromCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().ContainSingle();

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow async close to complete

            // Assert
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseWindowAsync_PublishesClosedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            events.Clear(); // Clear the Created event

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = events.Should().Contain(e => e.EventType == WindowLifecycleEventType.Closed);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseWindowAsync_WithInvalidId_ReturnsFalse_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Act
            var otherWindow = MakeSmallWindow();
            var missingId = otherWindow.AppWindow.Id;
            otherWindow.Close();

            var result = await sut.CloseWindowAsync(missingId).ConfigureAwait(true);

            // Assert
            _ = result.Should().BeFalse();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseAllWindowsAsync_ClosesAllWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();
        var window3 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window3, new("Test3")).ConfigureAwait(true);

            _ = sut.OpenWindows.Should().HaveCount(3);

            // Act
            await sut.CloseAllWindowsAsync().ConfigureAwait(true);
            await Task.Delay(200, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow async closes to complete

            // Assert
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseAllWindowsAsync_WithConcurrentModification_CompletesSuccessfully_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var windows = Enumerable.Range(0, 5).Select(_ => MakeSmallWindow()).ToList();

        var sut = this.CreateService();

        try
        {
            // Create multiple windows
            for (var i = 0; i < windows.Count; i++)
            {
                _ = await sut.RegisterDecoratedWindowAsync(windows[i], new("Test")).ConfigureAwait(true);
            }

            _ = sut.OpenWindows.Should().HaveCount(5);

            // Act - This tests the snapshot logic from TASK-007
            await sut.CloseAllWindowsAsync().ConfigureAwait(true);
            await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - All windows should be closed despite concurrent modification
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActivateWindow_UpdatesActiveWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            sut.ActivateWindow(context.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = sut.ActiveWindow.Should().NotBeNull();
            _ = sut.ActiveWindow!.Id.Should().Be(context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActivateWindow_PublishesActivatedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            await WaitForRenderAsync().ConfigureAwait(true);
            events.Clear(); // Clear the initial activation event

            // Act - Activate explicitly
            sut.ActivateWindow(context.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = events.Should().Contain(e => e.EventType == WindowLifecycleEventType.Activated);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActivateWindow_DeactivatesPreviousWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context1 = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            sut.ActivateWindow(context1.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            var context2 = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            events.Clear();

            // Act
            sut.ActivateWindow(context2.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert - Should have both Deactivated and Activated events
            _ = events.Should().Contain(e =>
                e.EventType == WindowLifecycleEventType.Deactivated &&
                e.Context.Id == context1.Id);
            _ = events.Should().Contain(e =>
                e.EventType == WindowLifecycleEventType.Activated &&
                e.Context.Id == context2.Id);
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActivateWindow_OnDifferentWindows_YieldsDistinctContexts_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context1 = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            sut.ActivateWindow(context1.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            var context2 = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act - Activate the second window
            sut.ActivateWindow(context2.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert - Verify that different windows maintain distinct state
            var updatedContext1 = sut.GetWindow(context1.Id);
            var updatedContext2 = sut.GetWindow(context2.Id);

            _ = updatedContext1.Should().NotBeNull();
            _ = updatedContext2.Should().NotBeNull();

            // Window 1 should be deactivated (state mutated in-place)
            _ = updatedContext1!.IsActive.Should().BeFalse();
            _ = updatedContext1.Should().BeSameAs(context1); // Same instance, state mutated

            // Window 2 should be activated with timestamp (state mutated in-place)
            _ = updatedContext2!.IsActive.Should().BeTrue();
            _ = updatedContext2.LastActivatedAt.Should().NotBeNull();
            _ = updatedContext2.Should().BeSameAs(context2); // Same instance, state mutated
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ActivateWindow_WithInvalidId_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Act & Assert - Should not throw
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            var act = () => sut.ActivateWindow(missingId);
            _ = act.Should().NotThrow();
        }
        finally
        {
            sut.Dispose();
        }

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Dispose_CompletesEventStream_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();
        var completed = false;
        _ = sut.WindowEvents.Subscribe(
            _ => { },
            () => completed = true);

        // Act
        sut.Dispose();
        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert
        _ = completed.Should().BeTrue();
    });

    [TestMethod]
    public Task Dispose_ClearsWindowCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            _ = sut.OpenWindows.Should().ContainSingle();

            // Act
            sut.Dispose();

            // Assert - Note: Disposal clears internal collection but doesn't close windows
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            testWindow.Close();
        }
    });
}
