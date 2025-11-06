// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using System.Reactive.Subjects;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;
using DroidNet.Tests;
using FluentAssertions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests.Decoration;

/// <summary>
///     Comprehensive test suite for the <see cref="WindowBackdropService"/> class.
/// </summary>
/// <remarks>
///     These tests verify backdrop application based on WindowDecorationOptions,
///     window lifecycle event handling, and graceful error handling.
/// </remarks>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("UITest")]
public partial class WindowBackdropServiceTests : VisualUserInterfaceTests, IDisposable
{
    private Mock<IWindowManagerService> mockWindowManager = null!;
    private Subject<WindowLifecycleEvent> windowEventsSubject = null!;
    private Mock<ILoggerFactory> mockLoggerFactory = null!;
    private bool disposed;

    public TestContext TestContext { get; set; }

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield(); // Ensure we're on UI thread

        // Setup mocks
        this.mockWindowManager = new Mock<IWindowManagerService>();
        this.windowEventsSubject = new Subject<WindowLifecycleEvent>();
        this.mockLoggerFactory = new Mock<ILoggerFactory>();

        // Setup window manager to expose event stream
        _ = this.mockWindowManager
            .Setup(m => m.WindowEvents)
            .Returns(this.windowEventsSubject.AsObservable());

        // Logger factory returns null logger
        _ = this.mockLoggerFactory
            .Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());
    });

    [TestCleanup]
    public Task CleanupAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield();
        this.windowEventsSubject?.Dispose();
    });

    [TestMethod]
    public Task ApplyBackdrop_WithNoneBackdrop_SkipsApplication_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Backdrop = BackdropKind.None,
        };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().BeNull("BackdropKind.None should result in null SystemBackdrop");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithMicaBackdrop_AppliesMicaBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Backdrop = BackdropKind.Mica,
        };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().NotBeNull("BackdropKind.Mica should apply a backdrop");
            _ = window.SystemBackdrop.Should().BeOfType<Microsoft.UI.Xaml.Media.MicaBackdrop>();
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithMicaAltBackdrop_AppliesMicaAltBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Backdrop = BackdropKind.MicaAlt,
        };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().NotBeNull("BackdropKind.MicaAlt should apply a backdrop");
            _ = window.SystemBackdrop.Should().BeOfType<Microsoft.UI.Xaml.Media.MicaBackdrop>();
            var micaBackdrop = (Microsoft.UI.Xaml.Media.MicaBackdrop)window.SystemBackdrop;
            _ = micaBackdrop.Kind.Should().Be(Microsoft.UI.Composition.SystemBackdrops.MicaKind.BaseAlt);
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithAcrylicBackdrop_AppliesAcrylicBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
            Backdrop = BackdropKind.Acrylic,
        };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().NotBeNull("BackdropKind.Acrylic should apply a backdrop");
            _ = window.SystemBackdrop.Should().BeOfType<Microsoft.UI.Xaml.Media.DesktopAcrylicBackdrop>();
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithNullDecoration_DoesNotApplyBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var context = CreateWindowContext(window, decoration: null);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().BeNull("Null decoration should not apply any backdrop");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithNullBackdropInDecoration_DoesNotApplyBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");

        // Test with default backdrop value (None)
        var decoration = new WindowDecorationOptions
        {
            Category = new WindowCategory("Test"),
        };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop(context);

            // Assert
            _ = window.SystemBackdrop.Should().BeNull("Null backdrop value should not apply any backdrop");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_ToAllWindows_AppliesBackdropToMatchingWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow("Window 1");
        var window2 = MakeSmallWindow("Window 2");
        var window3 = MakeSmallWindow("Window 3");

        var decoration1 = new WindowDecorationOptions { Category = new WindowCategory("Test"), Backdrop = BackdropKind.Mica };
        var decoration2 = new WindowDecorationOptions { Category = new WindowCategory("Test"), Backdrop = BackdropKind.Acrylic };

        // Window 3 with default backdrop (None)
        var decoration3 = new WindowDecorationOptions { Category = new WindowCategory("Test") };

        var context1 = CreateWindowContext(window1, decoration1);
        var context2 = CreateWindowContext(window2, decoration2);
        var context3 = CreateWindowContext(window3, decoration3);

        var openWindows = new List<WindowContext> { context1, context2, context3 };
        _ = this.mockWindowManager.Setup(m => m.OpenWindows).Returns(openWindows.AsReadOnly());

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act
            sut.ApplyBackdrop();

            // Assert
            _ = window1.SystemBackdrop.Should().NotBeNull("Window 1 should have Mica backdrop");
            _ = window1.SystemBackdrop.Should().BeOfType<Microsoft.UI.Xaml.Media.MicaBackdrop>();
            _ = window2.SystemBackdrop.Should().NotBeNull("Window 2 should have Acrylic backdrop");
            _ = window2.SystemBackdrop.Should().BeOfType<Microsoft.UI.Xaml.Media.DesktopAcrylicBackdrop>();
            _ = window3.SystemBackdrop.Should().BeNull("Window 3 has default backdrop (None), should not apply any backdrop");
        }
        finally
        {
            window1.Close();
            window2.Close();
            window3.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task ApplyBackdrop_WithPredicate_AppliesOnlyToMatchingWindows_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow("Window 1");
        var window2 = MakeSmallWindow("Window 2");

        var decoration1 = new WindowDecorationOptions { Category = WindowCategory.Main, Backdrop = BackdropKind.Mica };
        var decoration2 = new WindowDecorationOptions { Category = WindowCategory.Tool, Backdrop = BackdropKind.Acrylic };

        var context1 = new WindowContext
        {
            Id = new Microsoft.UI.WindowId(1),
            Window = window1,
            Category = WindowCategory.Main,
            CreatedAt = DateTimeOffset.UtcNow,
            Decorations = decoration1,
        };
        var context2 = new WindowContext
        {
            Id = new Microsoft.UI.WindowId(2),
            Window = window2,
            Category = WindowCategory.Tool,
            CreatedAt = DateTimeOffset.UtcNow,
            Decorations = decoration2,
        };

        var openWindows = new List<WindowContext> { context1, context2 };
        _ = this.mockWindowManager.Setup(m => m.OpenWindows).Returns(openWindows.AsReadOnly());

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act - Only apply to Main category windows
            sut.ApplyBackdrop(ctx => ctx.Decorations?.Category.Equals(WindowCategory.Main) == true);

            // Assert
            _ = window1.SystemBackdrop.Should().NotBeNull("Main window should have backdrop applied");
            _ = window2.SystemBackdrop.Should().BeNull("Tool window should not have backdrop applied due to predicate");
        }
        finally
        {
            window1.Close();
            window2.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task OnWindowEvent_WithCreatedEvent_AutomaticallyAppliesBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions { Category = new WindowCategory("Test"), Backdrop = BackdropKind.Mica };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act - Simulate window created event
            this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Created, context));

            // Give a moment for async processing
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = window.SystemBackdrop.Should().NotBeNull("Backdrop should be automatically applied on Created event");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task OnWindowEvent_WithNonCreatedEvent_DoesNotApplyBackdrop_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions { Category = new WindowCategory("Test"), Backdrop = BackdropKind.Mica };
        var context = CreateWindowContext(window, decoration);

        using var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act - Simulate activated event (not created)
            this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Activated, context));

            // Give a moment for async processing
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = window.SystemBackdrop.Should().BeNull("Backdrop should not be applied for non-Created events");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Dispose_UnsubscribesFromWindowEvents_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window = MakeSmallWindow("Test Window");
        var decoration = new WindowDecorationOptions { Category = new WindowCategory("Test"), Backdrop = BackdropKind.Mica };
        var context = CreateWindowContext(window, decoration);

        var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        try
        {
            // Act - Dispose the service
            sut.Dispose();

            // Try to trigger event after disposal
            this.windowEventsSubject.OnNext(WindowLifecycleEvent.Create(WindowLifecycleEventType.Created, context));

            // Give a moment for async processing
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - No backdrop should be applied since service is disposed
            _ = window.SystemBackdrop.Should().BeNull("Backdrop should not be applied after service disposal");
        }
        finally
        {
            window.Close();
            await Task.CompletedTask.ConfigureAwait(true);
        }
    });

    [TestMethod]
    public Task Dispose_MultipleTimesDoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = new WindowBackdropService(this.mockWindowManager.Object, this.mockLoggerFactory.Object);

        // Act
        var act = () =>
        {
            sut.Dispose();
            sut.Dispose();
            sut.Dispose();
        };

        // Assert
        _ = act.Should().NotThrow("Multiple dispose calls should be safe");

        await Task.CompletedTask.ConfigureAwait(true);
    });

    /// <summary>
    /// Disposes managed resources.
    /// </summary>
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    /// <summary>
    /// Disposes managed resources.
    /// </summary>
    /// <param name="disposing">True if disposing managed resources.</param>
    protected virtual void Dispose(bool disposing)
    {
        if (this.disposed)
        {
            return;
        }

        if (disposing)
        {
            this.windowEventsSubject?.Dispose();
        }

        this.disposed = true;
    }

    private static WindowContext CreateWindowContext(Window window, WindowDecorationOptions? decoration)
        => new()
        {
            Id = new Microsoft.UI.WindowId((ulong)window.GetHashCode()),
            Window = window,
            Category = new WindowCategory("Test"),
            CreatedAt = DateTimeOffset.UtcNow,
            Decorations = decoration,
        };

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
