// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Concurrent;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;
using DroidNet.Controls.Menus;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests.WindowManagement;

/// <summary>
/// Comprehensive test suite for the <see cref="WindowContextFactory"/> class.
/// </summary>
/// <remarks>
/// These tests verify decoration property assignment, menu provider resolution,
/// graceful degradation for missing providers, and thread-safe concurrent usage.
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
public class WindowContextFactoryTests : VisualUserInterfaceTests
{
    private Mock<ILogger<WindowContextFactory>> mockLogger = null!;
    private Mock<ILoggerFactory> mockLoggerFactory = null!;

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield(); // Ensure we're on UI thread

        // Setup mocks
        this.mockLogger = new Mock<ILogger<WindowContextFactory>>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();

        // Logger factory returns null logger
        _ = this.mockLoggerFactory
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());
    });

    [TestMethod]
    public Task Create_WithBasicParameters_CreatesWindowContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            []);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"));

            // Assert
            _ = context.Should().NotBeNull();
            _ = context.Id.Value.Should().BeGreaterThan(0);
            _ = context.Window.Should().Be(window);
            _ = context.Category.Should().Be(new WindowCategory("Test"));
            _ = context.CreatedAt.Should().BeCloseTo(DateTimeOffset.UtcNow, TimeSpan.FromSeconds(1));
            _ = context.Decorations.Should().BeNull();
            _ = context.Metadata.Should().BeNull();
            _ = context.MenuSource.Should().BeNull();
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithDecoration_StoresDecorationInContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Backdrop = BackdropKind.Mica,
            ChromeEnabled = true,
        };
        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            []);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert
            _ = context.Decorations.Should().Be(decoration);
            _ = context.Decorations.Backdrop.Should().Be(BackdropKind.Mica);
            _ = context.Decorations.ChromeEnabled.Should().BeTrue();
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithMetadata_StoresMetadataInContext_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var metadata = new Dictionary<string, object>(StringComparer.Ordinal)
        {
            ["Key1"] = "Value1",
            ["Key2"] = 42,
            ["Key3"] = true,
        };
        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            []);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                metadata: metadata);

            // Assert
            _ = context.Metadata.Should().NotBeNull();
            _ = context.Metadata.Should().BeEquivalentTo(metadata);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithMenuProvider_CreatesMenuSource_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var mockMenuSource = new Mock<IMenuSource>();
        var mockProvider = new Mock<IMenuProvider>();
        _ = mockProvider.Setup(p => p.ProviderId).Returns("TestProvider");
        _ = mockProvider.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource.Object);

        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = new MenuOptions { MenuProviderId = "TestProvider" },
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            [mockProvider.Object]);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert
            _ = context.MenuSource.Should().NotBeNull();
            _ = context.MenuSource.Should().Be(mockMenuSource.Object);
            mockProvider.Verify(p => p.CreateMenuSource(), Times.Once);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithMissingMenuProvider_LogsWarningWithoutThrowing_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = new MenuOptions { MenuProviderId = "NonExistentProvider" },
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            []);

        try
        {
            // Act
            var act = () => factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert - Should not throw (REQ-020)
            _ = act.Should().NotThrow();
            var context = act();
            _ = context.MenuSource.Should().BeNull();

            // Note: Cannot verify LoggerMessage source generator logging via Moq
            // The important assertion is that it doesn't throw
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithMultipleProviders_SelectsCorrectProvider_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var mockMenuSource1 = new Mock<IMenuSource>();
        var mockMenuSource2 = new Mock<IMenuSource>();
        var mockMenuSource3 = new Mock<IMenuSource>();

        var mockProvider1 = new Mock<IMenuProvider>();
        _ = mockProvider1.Setup(p => p.ProviderId).Returns("Provider1");
        _ = mockProvider1.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource1.Object);

        var mockProvider2 = new Mock<IMenuProvider>();
        _ = mockProvider2.Setup(p => p.ProviderId).Returns("Provider2");
        _ = mockProvider2.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource2.Object);

        var mockProvider3 = new Mock<IMenuProvider>();
        _ = mockProvider3.Setup(p => p.ProviderId).Returns("Provider3");
        _ = mockProvider3.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource3.Object);

        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = new MenuOptions { MenuProviderId = "Provider2" },
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            [mockProvider1.Object, mockProvider2.Object, mockProvider3.Object]);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert
            _ = context.MenuSource.Should().Be(mockMenuSource2.Object);
            mockProvider1.Verify(p => p.CreateMenuSource(), Times.Never);
            mockProvider2.Verify(p => p.CreateMenuSource(), Times.Once);
            mockProvider3.Verify(p => p.CreateMenuSource(), Times.Never);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithCaseSensitiveProviderId_UsesOrdinalComparison_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var mockMenuSource = new Mock<IMenuSource>();
        var mockProvider = new Mock<IMenuProvider>();
        _ = mockProvider.Setup(p => p.ProviderId).Returns("TestProvider");
        _ = mockProvider.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource.Object);

        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = new MenuOptions { MenuProviderId = "testprovider" }, // Different case
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            [mockProvider.Object]);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert - Should not match due to case sensitivity (StringComparison.Ordinal)
            _ = context.MenuSource.Should().BeNull();
            mockProvider.Verify(p => p.CreateMenuSource(), Times.Never);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_ConcurrentCalls_AreThreadSafe_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        const int ConcurrentCallCount = 50;
        var windows = Enumerable.Range(0, ConcurrentCallCount).Select(_ => MakeSmallWindow()).ToList();

        var mockMenuSource = new Mock<IMenuSource>();
        var mockProvider = new Mock<IMenuProvider>();
        _ = mockProvider.Setup(p => p.ProviderId).Returns("TestProvider");
        _ = mockProvider.Setup(p => p.CreateMenuSource()).Returns(mockMenuSource.Object);

        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = new MenuOptions { MenuProviderId = "TestProvider" },
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            [mockProvider.Object]);

        var contexts = new ConcurrentBag<WindowContext>();

        try
        {
            // Act - Create contexts concurrently (any exception will fail the test)
            var tasks = windows.Select(async window =>
            {
                await Task.Yield();
                var context = factory.Create(window, new WindowCategory("Test"), decoration: decoration);
                contexts.Add(context);
            });

            await Task.WhenAll(tasks).ConfigureAwait(true);

            // Assert
            _ = contexts.Should().HaveCount(ConcurrentCallCount);
            _ = contexts.Select(c => c.Id).Should().OnlyHaveUniqueItems("each context should have a unique ID");
            _ = contexts.Should().OnlyContain(c => c.MenuSource == mockMenuSource.Object);
        }
        finally
        {
            foreach (var window in windows)
            {
                window.Close();
            }

            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Create_WithNullWindow_ThrowsArgumentNullException_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            []);

        // Act & Assert
        var act = () => factory.Create(
            null!,
            new WindowCategory("Test"));

        _ = act.Should().Throw<ArgumentNullException>().WithParameterName("window");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    [TestMethod]
    public Task Create_WithNoDecorationMenu_DoesNotCreateMenuSource_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow();
        var mockProvider = new Mock<IMenuProvider>();
        _ = mockProvider.Setup(p => p.ProviderId).Returns("TestProvider");

        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Menu = null, // No menu
        };

        var factory = new WindowContextFactory(
            this.mockLogger.Object,
            this.mockLoggerFactory.Object,
            [mockProvider.Object]);

        try
        {
            // Act
            var context = factory.Create(
                window,
                new WindowCategory("Test"),
                decoration: decoration);

            // Assert
            _ = context.MenuSource.Should().BeNull();
            mockProvider.Verify(p => p.CreateMenuSource(), Times.Never);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
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
}
