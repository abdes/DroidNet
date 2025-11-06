// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Settings;
using DroidNet.Aura.Windowing;
using DroidNet.Config;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests.WindowManagement;

/// <summary>
/// Integration tests for <see cref="WindowManagerService"/> decoration resolution.
/// </summary>
/// <remarks>
/// <para>
/// These tests validate the 3-tier decoration resolution strategy:
/// </para>
/// <list type="number">
/// <item><description>Explicit decoration parameter (highest priority)</description></item>
/// <item><description>Settings registry lookup by category</description></item>
/// <item><description>No decoration (when no settings service)</description></item>
/// </list>
/// <para>
/// All tests run on the UI thread via <see cref="VisualUserInterfaceTests"/> base class
/// to properly handle WinUI window creation and cleanup.
/// </para>
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public class WindowManagerServiceDecorationTests : VisualUserInterfaceTests
{
    private Mock<ILoggerFactory> mockLoggerFactory = null!;
    private IWindowContextFactory windowContextFactory = null!;
    private HostingContext hostingContext = null!;

    /// <summary>
    /// Initializes the test fixtures before each test.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous test initialization.</returns>
    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield(); // Ensure we're on UI thread

        // Setup logger factory
        this.mockLoggerFactory = new Mock<ILoggerFactory>();
        _ = this.mockLoggerFactory
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());

        // Create real context factory (no mocking, we need real menu provider resolution)
        this.windowContextFactory = new WindowContextFactory(
            Mock.Of<ILogger<WindowContextFactory>>(),
            this.mockLoggerFactory.Object,
            []);

        // Create hosting context with real dispatcher
        var dispatcher = DispatcherQueue.GetForCurrentThread();
        this.hostingContext = new HostingContext
        {
            Dispatcher = dispatcher,
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(dispatcher),
        };
    });

    /// <summary>
    /// Tests that settings registry provides decoration when no explicit parameter.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_NoExplicit_UsesSettingsRegistry_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var settingsService = CreateMockSettingsService(WindowCategory.Tool);

        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var window = MakeSmallWindow();
        WindowContext? context = null;

        try
        {
            // Act - No explicit decoration
            context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Tool).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decorations.Should().NotBeNull();
            _ = context.Decorations.Category.Should().Be(WindowCategory.Tool);
            _ = context.Decorations.Buttons.ShowMaximize.Should().BeFalse(); // Tool default
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }
            else
            {
                window.Close();
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Tests that null is returned when no settings service and no explicit parameter.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_NoSettingsService_NoDecoration_Async() => EnqueueAsync(async () =>
    {
        // Arrange - No decoration settings service
        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: null); // No service

        var window = MakeSmallWindow();
        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Main).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decorations.Should().BeNull(); // No decoration applied
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }
            else
            {
                window.Close();
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Tests concurrent window creation with decoration resolution is thread-safe.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_ConcurrentCreation_IsThreadSafe_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int ConcurrentCallCount = 20;
        var settingsService = CreateMockSettingsService(WindowCategory.Tool);

        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var createdContexts = new ConcurrentBag<WindowContext>();
        var createdWindows = new List<Window>();

        try
        {
            // Act - Register multiple windows concurrently
            var tasks = Enumerable.Range(0, ConcurrentCallCount)
                .Select(async i =>
                {
                    var win = MakeSmallWindow(string.Create(CultureInfo.InvariantCulture, $"Window {i}"));
                    lock (createdWindows)
                    {
                        createdWindows.Add(win);
                    }

                    var ctx = await windowManager.RegisterDecoratedWindowAsync(
                        win,
                        WindowCategory.Tool).ConfigureAwait(true);
                    createdContexts.Add(ctx);
                })
                .ToArray();

            await Task.WhenAll(tasks).ConfigureAwait(true);

            // Assert
            _ = createdContexts.Should().HaveCount(ConcurrentCallCount);
            foreach (var context in createdContexts)
            {
                _ = context.Decorations.Should().NotBeNull();
                _ = context.Decorations.Category.Should().Be(WindowCategory.Tool);
                _ = context.Decorations.Buttons.ShowMaximize.Should().BeFalse();
            }
        }
        finally
        {
            // Cleanup all windows
            foreach (var context in createdContexts)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Tests that Main category windows get Main-specific default decoration.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_MainCategory_GetsMainDefault_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var settingsService = CreateMockSettingsService(WindowCategory.Main);

        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var window = MakeSmallWindow();
        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Main).ConfigureAwait(true);

            // Assert
            _ = context.Decorations.Should().NotBeNull();
            _ = context.Decorations.Category.Should().Be(WindowCategory.Main);
            _ = context.Decorations.TitleBar?.Height.Should().Be(40.0); // Main characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }
            else
            {
                window.Close();
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Tests that Tool category windows get Tool-specific default decoration.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_ToolCategory_GetsToolDefault_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var settingsService = CreateMockSettingsService(WindowCategory.Tool);

        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var window = MakeSmallWindow();
        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Tool).ConfigureAwait(true);

            // Assert
            _ = context.Decorations.Should().NotBeNull();
            _ = context.Decorations.Category.Should().Be(WindowCategory.Tool);
            _ = context.Decorations.Buttons.ShowMaximize.Should().BeFalse(); // Tool characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }
            else
            {
                window.Close();
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Tests that Document category windows get Document-specific default decoration.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_DocumentCategory_GetsDocumentDefault_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var settingsService = CreateMockSettingsService(WindowCategory.Document);

        var windowManager = new WindowManagerService(
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var window = MakeSmallWindow();
        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.RegisterDecoratedWindowAsync(window, WindowCategory.Document).ConfigureAwait(true);

            // Assert
            _ = context.Decorations.Should().NotBeNull();
            _ = context.Decorations.Category.Should().Be(WindowCategory.Document);
            _ = context.Decorations.Backdrop.Should().Be(BackdropKind.Mica); // Document characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }
            else
            {
                window.Close();
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Helper to create a mock settings service that returns a preset decoration for a category.
    /// </summary>
    private static Mock<ISettingsService<IWindowDecorationSettings>> CreateMockSettingsService(
        WindowCategory category)
    {
        var mockService = new Mock<ISettingsService<IWindowDecorationSettings>>();
        var mockSettings = new Mock<IWindowDecorationSettings>();

        // Setup GetEffectiveDecoration to return appropriate preset
        var decoration = category switch
        {
            var c when c == WindowCategory.Main => WindowDecorationBuilder.ForMainWindow().Build(),
            var c when c == WindowCategory.Tool => WindowDecorationBuilder.ForToolWindow().Build(),
            var c when c == WindowCategory.Document => WindowDecorationBuilder.ForDocumentWindow().Build(),
            var c when c == WindowCategory.Secondary => WindowDecorationBuilder.ForSecondaryWindow().Build(),
            _ => WindowDecorationBuilder.ForSecondaryWindow().Build(),
        };

        _ = mockSettings
            .Setup(s => s.GetEffectiveDecoration(It.IsAny<WindowCategory>()))
            .Returns(decoration);

        _ = mockService
            .Setup(s => s.Settings)
            .Returns(mockSettings.Object);

        return mockService;
    }

    /// <summary>
    /// Helper to create a small window for testing.
    /// </summary>
    private static Window MakeSmallWindow(string? title = null)
    {
        var window = string.IsNullOrEmpty(title) ? new Window() : new Window { Title = title };

        // Set window to a small size (200x150) to not invade the screen during tests
        window.AppWindow.Resize(new Windows.Graphics.SizeInt32 { Width = 200, Height = 150 });

        // Position it off to the side of the screen
        window.AppWindow.Move(new Windows.Graphics.PointInt32 { X = 50, Y = 50 });

        return window;
    }
}
