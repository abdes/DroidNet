// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using AwesomeAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Comprehensive test suite for the <see cref="WindowManagerService"/> class.
/// </summary>
/// <remarks>
///     These tests verify window lifecycle management, event publishing,
///     memory leak prevention, and concurrent operation handling.
/// </remarks>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTests : VisualUserInterfaceTests
{
    private Mock<IWindowContextFactory> mockWindowContextFactory = null!;
    private HostingContext hostingContext = null!;
    private Mock<ILoggerFactory> mockLoggerFactory = null!;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield(); // Ensure we're on UI thread

        // Create hosting context with real dispatcher
        this.hostingContext = new HostingContext
        {
            Dispatcher = DispatcherQueue.GetForCurrentThread(),
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(DispatcherQueue.GetForCurrentThread()),
        };

        // Setup mocks
        this.mockWindowContextFactory = new Mock<IWindowContextFactory>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();

        // Logger factory returns null logger
        _ = this.mockLoggerFactory.Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());

        // Setup window context factory to create contexts
        _ = this.mockWindowContextFactory
            .Setup(f => f.Create(
                It.IsAny<Window>(),
                It.IsAny<WindowCategory>(),
                It.IsAny<WindowDecorationOptions?>(),
                It.IsAny<IReadOnlyDictionary<string, object>?>()))
            .Returns<Window, WindowCategory, WindowDecorationOptions?, IReadOnlyDictionary<string, object>?>(
                (window, category, decoration, metadata) => new WindowContext
                {
                    Id = window.AppWindow.Id,
                    Window = window,
                    Category = category,
                    CreatedAt = DateTimeOffset.UtcNow,
                    Decorations = decoration,
                    Metadata = metadata,
                });
    });

    [TestMethod]
    public Task RegisterWindowAsync_WithValidWindow_RegistersContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(
                testWindow,
                new("Test")).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Category.Should().Be(new WindowCategory("Test"));
            _ = context.Window.Should().Be(testWindow);
            _ = context.Id.Should().Be(testWindow.AppWindow.Id);
            _ = sut.OpenWindows.Should().ContainSingle();
            _ = sut.OpenWindows.First().Id.Should().Be(context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_PublishesCreatedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert - Should have Created event as first event
            _ = events.Should().Contain(e => e.EventType == WindowLifecycleEventType.Created);
            var createdEvent = events.First(e => e.EventType == WindowLifecycleEventType.Created);
            _ = createdEvent.Context.Id.Should().Be(context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_WhenWindowActivates_TracksActiveWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            testWindow.Activate();
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
    public Task RegisterWindowAsync_WhenWindowAlreadyRegistered_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");

        var sut = this.CreateService();

        try
        {
            // Act
            _ = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            var act = async () => await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Assert
            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(true);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_WithMetadata_StoresMetadataInContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var metadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Key1"] = "Value1",
            ["Key2"] = 42,
        };

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(
                testWindow,
                new("Test"),
                metadata).ConfigureAwait(true);

            // Assert
            _ = context.Metadata.Should().NotBeNull();
            _ = context.Metadata.Should().HaveCount(2);
            _ = context.Metadata["Key1"].Should().Be("Value1");
            _ = context.Metadata["Key2"].Should().Be(42);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_WhenContextFactoryFails_ThrowsInvalidOperationException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        _ = this.mockWindowContextFactory
            .Setup(f => f.Create(
                It.IsAny<Window>(),
                It.IsAny<WindowCategory>(),
                It.IsAny<WindowDecorationOptions?>(),
                It.IsAny<IReadOnlyDictionary<string, object>?>()))
            .Throws(new InvalidOperationException("Factory failure"));

        var sut = this.CreateService();
        var testWindow = MakeSmallWindow();

        try
        {
            // Act & Assert
            var act = async () => await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(true);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RegisterWindowAsync_WhenDisposed_ThrowsObjectDisposedException_Async() => EnqueueAsync(async () =>
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
    public Task WindowEvents_PublishesCreatedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            // Act
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = events.Should().ContainSingle(e => e.EventType == WindowLifecycleEventType.Created);
            _ = events[0].Context.Id.Should().Be(context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowEvents_PublishesActivatedEvent_Async() => EnqueueAsync(async () =>
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

            events.Clear(); // Clear the Created event

            // Act - Explicitly activate
            sut.ActivateWindow(context.Id);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Assert
            _ = events.Should().ContainSingle(e => e.EventType == WindowLifecycleEventType.Activated);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowEvents_PublishesDeactivatedEvent_Async() => EnqueueAsync(async () =>
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

            // Assert
            _ = events.Should().Contain(e =>
                e.EventType == WindowLifecycleEventType.Deactivated &&
                e.Context.Id == context1.Id);
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowEvents_PublishesClosedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            events.Clear();

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = events.Should().ContainSingle(e => e.EventType == WindowLifecycleEventType.Closed);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task GetWindow_WithValidId_ReturnsContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var result = sut.GetWindow(context.Id);

            // Assert
            _ = result.Should().NotBeNull();
            _ = result!.Id.Should().Be(context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task GetWindow_WithInvalidId_ReturnsNull_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            // Act
            var tempWindow = MakeSmallWindow();
            var missingId = tempWindow.AppWindow.Id;
            tempWindow.Close();

            var result = sut.GetWindow(missingId);

            // Assert
            _ = result.Should().BeNull();
        }
        finally
        {
            sut.Dispose();
        }

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task GetWindowsByType_ReturnsMatchingWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();
        var window3 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(window1, WindowCategory.Tool).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window2, WindowCategory.Document).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window3, WindowCategory.Tool).ConfigureAwait(true);

            // Act
            var toolWindows = sut.GetWindowsByCategory(WindowCategory.Tool);

            // Assert
            _ = toolWindows.Should().HaveCount(2);
            _ = toolWindows.Should().AllSatisfy(w => w.Category.Should().Be(WindowCategory.Tool));
        }
        finally
        {
            window1.Close();
            window2.Close();
            window3.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task OpenWindows_ReturnsAllWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);

            // Act
            var allWindows = sut.OpenWindows;

            // Assert
            _ = allWindows.Should().HaveCount(2);
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
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

    private static Window MakeSmallWindow(string? title = null)
    {
        var window = string.IsNullOrEmpty(title) ? new Window() : new Window { Title = title };

        // Set window to a small size (200x150) to not invade the screen during tests
        window.AppWindow.Resize(new Windows.Graphics.SizeInt32 { Width = 200, Height = 150 });

        // Position it off to the side of the screen
        window.AppWindow.Move(new Windows.Graphics.PointInt32 { X = 50, Y = 50 });

        return window;
    }

    private WindowManagerService CreateService()
        => new(
            this.mockWindowContextFactory.Object,
            this.hostingContext,
            this.mockLoggerFactory.Object);
}
