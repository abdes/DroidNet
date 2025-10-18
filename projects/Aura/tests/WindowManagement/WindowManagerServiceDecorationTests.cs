// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using System.Globalization;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.WindowManagement;
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
public class WindowManagerServiceDecorationTests : VisualUserInterfaceTests
{
    private Mock<ILoggerFactory> mockLoggerFactory = null!;
    private Mock<IWindowFactory> mockWindowFactory = null!;
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

        // Setup window factory
        this.mockWindowFactory = new Mock<IWindowFactory>();
        _ = this.mockWindowFactory
            .Setup(f => f.CreateWindow<Window>())
            .Returns(() => MakeSmallWindow());

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
    /// Tests that explicit decoration parameter takes precedence over settings registry.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindow_ExplicitDecoration_TakesPrecedence_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var explicitDecoration = WindowDecorationBuilder.ForToolWindow().Build();
        var settingsService = CreateMockSettingsService(WindowCategory.Main);

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        WindowContext? context = null;

        try
        {
            // Act - Category says "Main" but explicit says "Tool"
            context = await windowManager.CreateWindowAsync<Window>(
                WindowCategory.Main,
                decoration: explicitDecoration).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration.Should().Be(explicitDecoration);
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Tool); // Tool preset
            _ = context.Decoration.Buttons.ShowMaximize.Should().BeFalse(); // Tool characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
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
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        WindowContext? context = null;

        try
        {
            // Act - No explicit decoration
            context = await windowManager.CreateWindowAsync<Window>(WindowCategory.Tool).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Tool);
            _ = context.Decoration.Buttons.ShowMaximize.Should().BeFalse(); // Tool default
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
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
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: null); // No service

        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.CreateWindowAsync<Window>(WindowCategory.Main).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decoration.Should().BeNull(); // No decoration applied
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
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
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        var createdContexts = new ConcurrentBag<WindowContext>();

        try
        {
            // Act - Create multiple windows concurrently
            var tasks = Enumerable.Range(0, ConcurrentCallCount)
                .Select(async i =>
                {
                    var ctx = await windowManager.CreateWindowAsync<Window>(
                        WindowCategory.Tool,
                        title: string.Create(CultureInfo.InvariantCulture, $"Window {i}")).ConfigureAwait(true);
                    createdContexts.Add(ctx);
                })
                .ToArray();

            await Task.WhenAll(tasks).ConfigureAwait(true);

            // Assert
            _ = createdContexts.Should().HaveCount(ConcurrentCallCount);
            foreach (var context in createdContexts)
            {
                _ = context.Decoration.Should().NotBeNull();
                _ = context.Decoration!.Category.Should().Be(WindowCategory.Tool);
                _ = context.Decoration.Buttons.ShowMaximize.Should().BeFalse();
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
    /// Tests RegisterWindowAsync respects decoration resolution.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task RegisterWindow_RespectsDecorationResolution_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Registered Window");
        var explicitDecoration = WindowDecorationBuilder.ForMainWindow().Build();
        var settingsService = CreateMockSettingsService(WindowCategory.Tool);

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        WindowContext? context = null;

        try
        {
            // Act - Category says "Tool" but explicit says "Main"
            context = await windowManager.RegisterWindowAsync(
                window,
                WindowCategory.Tool,
                decoration: explicitDecoration).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration.Should().Be(explicitDecoration);
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Main); // Main preset
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
    /// Tests CreateWindowAsync with string typename respects decoration resolution.
    /// </summary>
    /// <returns>A <see cref="Task"/> representing the asynchronous unit test.</returns>
    [TestMethod]
    public Task CreateWindowByTypeName_RespectsDecorationResolution_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        _ = this.mockWindowFactory
            .Setup(f => f.CreateWindow(It.IsAny<string>()))
            .Returns(() => MakeSmallWindow());

        var settingsService = CreateMockSettingsService(WindowCategory.Document);

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: settingsService.Object);

        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.CreateWindowAsync(
                "Microsoft.UI.Xaml.Window",
                WindowCategory.Document).ConfigureAwait(true);

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Document);
            _ = context.Decoration.Backdrop.Should().Be(BackdropKind.Mica); // Document default
        }
        finally
        {
            if (context != null)
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
        var decorationSettings = new Mock<IWindowDecorationSettingsService>();
        _ = decorationSettings.As<ISettingsService<WindowDecorationSettings>>();
        _ = decorationSettings
            .Setup(s => s.GetEffectiveDecoration(WindowCategory.Main))
            .Returns(WindowDecorationBuilder.ForMainWindow().Build());

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: decorationSettings.Object);

        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.CreateWindowAsync<Window>(WindowCategory.Main).ConfigureAwait(true);

            // Assert
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Main);
            _ = context.Decoration.TitleBar.Height.Should().Be(40.0); // Main characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
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
        var decorationSettings = new Mock<IWindowDecorationSettingsService>();
        _ = decorationSettings.As<ISettingsService<WindowDecorationSettings>>();
        _ = decorationSettings
            .Setup(s => s.GetEffectiveDecoration(WindowCategory.Tool))
            .Returns(WindowDecorationBuilder.ForToolWindow().Build());

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: decorationSettings.Object);

        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.CreateWindowAsync<Window>(WindowCategory.Tool).ConfigureAwait(true);

            // Assert
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Tool);
            _ = context.Decoration.Buttons.ShowMaximize.Should().BeFalse(); // Tool characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
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
        var decorationSettings = new Mock<IWindowDecorationSettingsService>();
        _ = decorationSettings.As<ISettingsService<WindowDecorationSettings>>();
        _ = decorationSettings
            .Setup(s => s.GetEffectiveDecoration(WindowCategory.Document))
            .Returns(WindowDecorationBuilder.ForDocumentWindow().Build());

        var windowManager = new WindowManagerService(
            this.mockWindowFactory.Object,
            this.windowContextFactory,
            this.hostingContext,
            this.mockLoggerFactory.Object,
            decorationSettingsService: decorationSettings.Object);

        WindowContext? context = null;

        try
        {
            // Act
            context = await windowManager.CreateWindowAsync<Window>(WindowCategory.Document).ConfigureAwait(true);

            // Assert
            _ = context.Decoration.Should().NotBeNull();
            _ = context.Decoration!.Category.Should().Be(WindowCategory.Document);
            _ = context.Decoration.Backdrop.Should().Be(BackdropKind.Mica); // Document characteristic
        }
        finally
        {
            if (context != null)
            {
                _ = await windowManager.CloseWindowAsync(context).ConfigureAwait(true);
            }

            windowManager.Dispose();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    /// <summary>
    /// Helper to create a mock settings service that returns a preset decoration for a category.
    /// </summary>
    private static Mock<ISettingsService<WindowDecorationSettings>> CreateMockSettingsService(
        WindowCategory category)
    {
        var mockService = new Mock<ISettingsService<WindowDecorationSettings>>();
        var mockDecoration = new Mock<IWindowDecorationSettingsService>();

        // Make the mock implement both interfaces
        _ = mockDecoration.As<ISettingsService<WindowDecorationSettings>>();

        // Setup GetEffectiveDecoration to return appropriate preset
        var decoration = category switch
        {
            var c when c == WindowCategory.Main => WindowDecorationBuilder.ForMainWindow().Build(),
            var c when c == WindowCategory.Tool => WindowDecorationBuilder.ForToolWindow().Build(),
            var c when c == WindowCategory.Document => WindowDecorationBuilder.ForDocumentWindow().Build(),
            var c when c == WindowCategory.Secondary => WindowDecorationBuilder.ForSecondaryWindow().Build(),
            _ => WindowDecorationBuilder.ForSecondaryWindow().Build(),
        };

        _ = mockDecoration
            .Setup(s => s.GetEffectiveDecoration(It.IsAny<WindowCategory>()))
            .Returns(decoration);

        return mockDecoration.As<ISettingsService<WindowDecorationSettings>>();
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
