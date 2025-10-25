// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Aura.WindowManagement;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Comprehensive test suite for the <see cref="WindowManagerService"/> class.
/// </summary>
/// <remarks>
///     These tests verify window lifecycle management, theme synchronization, event publishing,
///     memory leak prevention, and concurrent operation handling.
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTests : VisualUserInterfaceTests
{
    private Mock<IWindowFactory> mockFactory = null!;
    private Mock<IWindowContextFactory> mockWindowContextFactory = null!;
    private Mock<IAppThemeModeService> mockThemeService = null!;
    private Mock<IAppearanceSettings> mockAppearanceSettings = null!;
    private Mock<ISettingsService<IAppearanceSettings>> mockSettingsService = null!;
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
        this.mockFactory = new Mock<IWindowFactory>();
        this.mockWindowContextFactory = new Mock<IWindowContextFactory>();
        this.mockThemeService = new Mock<IAppThemeModeService>();
        this.mockAppearanceSettings = new Mock<IAppearanceSettings>();
        this.mockSettingsService = new Mock<ISettingsService<IAppearanceSettings>>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();

        // Default appearance settings
        _ = this.mockAppearanceSettings.Setup(s => s.AppThemeMode).Returns(ElementTheme.Dark);
        _ = this.mockSettingsService.Setup(s => s.Settings).Returns(this.mockAppearanceSettings.Object);

        // Logger factory returns null logger
        _ = this.mockLoggerFactory.Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());

        // Setup window context factory to create contexts
        _ = this.mockWindowContextFactory
            .Setup(f => f.Create(
                It.IsAny<Window>(),
                It.IsAny<WindowCategory>(),
                It.IsAny<string>(),
                It.IsAny<WindowDecorationOptions>(),
                It.IsAny<IReadOnlyDictionary<string, object>>()))
            .Returns<Window, WindowCategory, string?, WindowDecorationOptions?, IReadOnlyDictionary<string, object>?>(
                (window, category, title, decoration, metadata) => new WindowContext
                {
                    Id = Guid.NewGuid(),
                    Window = window,
                    Category = category,
                    Title = title ?? window.Title ?? $"Untitled {category} Window",
                    CreatedAt = DateTimeOffset.UtcNow,
                    Decorations = decoration,
                    Metadata = metadata,
                });
    });

    [TestMethod]
    public Task CreateWindowAsync_WithValidType_CreatesAndRegistersWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                title: "Test Window").ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Category.Should().Be(new WindowCategory("Test"));
            _ = context.Title.Should().Be("Test Window");
            _ = context.Window.Should().Be(testWindow);
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
    public Task CreateWindowAsync_AppliesThemeToNewWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            // Act
            _ = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);

            // Assert - Theme service should be called
            this.mockThemeService.Verify(
                s => s.ApplyThemeMode(testWindow, ElementTheme.Dark),
                Times.Once);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CreateWindowAsync_PublishesCreatedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            // Act
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow event to propagate

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
    public Task CreateWindowAsync_WhenActivateTrue_ActivatesWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                activateWindow: true).ConfigureAwait(true);

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
    public Task CreateWindowAsync_WhenActivateFalse_DoesNotActivateWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow("Test Window");
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            // Act
            _ = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                activateWindow: false).ConfigureAwait(true);

            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow any async operations

            // Assert - Window should NOT be activated when activateWindow=false
            _ = sut.ActiveWindow.Should().BeNull();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CreateWindowAsync_WithMetadata_StoresMetadataInContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var metadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Key1"] = "Value1",
            ["Key2"] = 42,
        };

        var sut = this.CreateService();

        try
        {
            // Act
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                metadata: metadata).ConfigureAwait(true);

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
    public Task CreateWindowAsync_WhenFactoryFails_ThrowsInvalidOperationException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>())
            .Returns((Window)null!);

        var sut = this.CreateService();

        try
        {
            // Act & Assert
            var act = async () => await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
            _ = await act.Should().ThrowAsync<InvalidOperationException>().ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CreateWindowAsync_WhenDisposed_ThrowsObjectDisposedException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();
        sut.Dispose();

        // Act & Assert
        var act = async () => await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
        _ = await act.Should().ThrowAsync<ObjectDisposedException>().ConfigureAwait(true);
    });

    [TestMethod]
    public Task CloseWindowAsync_WithValidId_ClosesWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);

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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
            var result = await sut.CloseWindowAsync(Guid.NewGuid()).ConfigureAwait(true);

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2)
            .Returns(window3);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: new("Test1")).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: new("Test2")).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: new("Test3")).ConfigureAwait(true);

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
        var index = 0;

        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>())
            .Returns(() => windows[index++]);

        var sut = this.CreateService();

        try
        {
            // Create multiple windows
            for (var i = 0; i < windows.Count; i++)
            {
                _ = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                activateWindow: false).ConfigureAwait(true);

            // Act
            sut.ActivateWindow(context.Id);
            await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                activateWindow: false).ConfigureAwait(true);

            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true); // Wait for initial window.Activate() event
            events.Clear(); // Clear the initial activation event

            // Act - Activate explicitly
            sut.ActivateWindow(context.Id);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context1 = await sut.CreateWindowAsync<Window>(
                category: new("Test1"),
                activateWindow: true).ConfigureAwait(true);
            var context2 = await sut.CreateWindowAsync<Window>(
                category: new("Test2"),
                activateWindow: false).ConfigureAwait(true);

            events.Clear();

            // Act
            sut.ActivateWindow(context2.Id);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2);

        var sut = this.CreateService();

        try
        {
            var context1 = await sut.CreateWindowAsync<Window>(
                category: new("Test1"),
                activateWindow: true).ConfigureAwait(true);

            await Task.Delay(150, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow activation to complete

            var context2 = await sut.CreateWindowAsync<Window>(
                category: new("Test2"),
                activateWindow: false).ConfigureAwait(true);

            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true); // Ensure measurable time difference

            // Act - Activate the second window
            sut.ActivateWindow(context2.Id);
            await Task.Delay(150, this.TestContext.CancellationToken).ConfigureAwait(true);

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
            var act = () => sut.ActivateWindow(Guid.NewGuid());
            _ = act.Should().NotThrow();
        }
        finally
        {
            sut.Dispose();
        }

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task ThemeChange_ReappliesThemeToAllWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: new("Test1")).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: new("Test2")).ConfigureAwait(true);

            this.mockThemeService.Invocations.Clear();

            // Act - Change theme via PropertyChanged
            _ = this.mockAppearanceSettings.Setup(s => s.AppThemeMode).Returns(ElementTheme.Light);
            this.mockSettingsService.Raise(
                s => s.PropertyChanged += null,
                new PropertyChangedEventArgs(nameof(IAppearanceSettings.AppThemeMode)));

            await Task.Delay(200, this.TestContext.CancellationToken).ConfigureAwait(true); // Allow theme application

            // Assert - TEST-001: Theme should be reapplied to all windows
            this.mockThemeService.Verify(
                s => s.ApplyThemeMode(It.IsAny<Window>(), ElementTheme.Light),
                Times.AtLeast(2));
        }
        finally
        {
            window1.Close();
            window2.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task ThemeChange_OnlyReappliesWhenThemeModeChanges_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
            this.mockThemeService.Invocations.Clear();

            // Act - Change a different property
            this.mockSettingsService.Raise(
                s => s.PropertyChanged += null,
                new PropertyChangedEventArgs(nameof(IAppearanceSettings.AppThemeBackgroundColor)));

            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - Theme should NOT be reapplied
            this.mockThemeService.Verify(
                s => s.ApplyThemeMode(It.IsAny<Window>(), It.IsAny<ElementTheme>()),
                Times.Never);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CreateWindow_WithoutThemeService_DoesNotFail_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = new WindowManagerService(
            this.mockFactory.Object,
            this.mockWindowContextFactory.Object,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            themeModeService: null, // No theme service
            this.mockSettingsService.Object,
            router: null);

        try
        {
            // Act & Assert - Should not throw
            var act = async () => await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
            _ = await act.Should().NotThrowAsync().ConfigureAwait(true);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowEvents_PublishesCreatedEvent_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            // Act
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.CreateWindowAsync<Window>(
                category: new("Test"),
                activateWindow: false).ConfigureAwait(true);

            events.Clear(); // Clear the Created event

            // Act - Explicitly activate
            sut.ActivateWindow(context.Id);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context1 = await sut.CreateWindowAsync<Window>(
                category: new("Test1"),
                activateWindow: true).ConfigureAwait(true);
            var context2 = await sut.CreateWindowAsync<Window>(
                category: new("Test2"),
                activateWindow: false).ConfigureAwait(true);
            events.Clear();

            // Act
            sut.ActivateWindow(context2.Id);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();
        var events = new List<WindowLifecycleEvent>();
        _ = sut.WindowEvents.Subscribe(events.Add);

        try
        {
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            var context = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);

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
            var result = sut.GetWindow(Guid.NewGuid());

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2)
            .Returns(window3);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: WindowCategory.Tool).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: WindowCategory.Document).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: WindowCategory.Tool).ConfigureAwait(true);

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

        _ = this.mockFactory.SetupSequence(f => f.CreateWindow<Window>())
            .Returns(window1)
            .Returns(window2);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: new("Test1")).ConfigureAwait(true);
            _ = await sut.CreateWindowAsync<Window>(category: new("Test2")).ConfigureAwait(true);

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
    public Task Dispose_UnsubscribesFromAppearanceSettings_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        // Act
        sut.Dispose();

        // Trigger PropertyChanged after disposal
        this.mockSettingsService.Raise(
            s => s.PropertyChanged += null,
            new PropertyChangedEventArgs(nameof(IAppearanceSettings.AppThemeMode)));

        await Task.Delay(50, this.TestContext.CancellationToken).ConfigureAwait(true);

        // Assert - No exception should be thrown, and theme service should not be called
        this.mockThemeService.Verify(
            s => s.ApplyThemeMode(It.IsAny<Window>(), It.IsAny<ElementTheme>()),
            Times.Never);
    });

    [TestMethod]
    public Task Dispose_ClearsWindowCollection_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();
        _ = this.mockFactory.Setup(f => f.CreateWindow<Window>()).Returns(testWindow);

        var sut = this.CreateService();

        try
        {
            _ = await sut.CreateWindowAsync<Window>(category: new("Test")).ConfigureAwait(true);
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
            this.mockFactory.Object,
            this.mockWindowContextFactory.Object,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            this.mockThemeService.Object,
            this.mockSettingsService.Object,
            router: null);
}
