// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Aura.Decoration;
using DroidNet.Aura.Windowing;
using DroidNet.Hosting.WinUI;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Dispatching;
using Microsoft.UI.Xaml;
using Moq;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Base class for WindowManagerService test suites, providing shared infrastructure.
/// </summary>
[ExcludeFromCodeCoverage]
public abstract class WindowManagerServiceTestsBase : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    protected HostingContext HostingContext { get; private set; } = null!;

    protected Mock<ILoggerFactory> MockLoggerFactory { get; private set; } = null!;

    [TestInitialize]
    public Task InitializeAsync() => EnqueueAsync(async () =>
    {
        await Task.Yield(); // Ensure we're on UI thread

        // Create hosting context with real dispatcher
        this.HostingContext = new HostingContext
        {
            Dispatcher = DispatcherQueue.GetForCurrentThread(),
            Application = Application.Current,
            DispatcherScheduler = new System.Reactive.Concurrency.DispatcherQueueScheduler(DispatcherQueue.GetForCurrentThread()),
        };

        // Setup mocks
        this.MockLoggerFactory = new Mock<ILoggerFactory>();

        // Logger factory returns null logger
        _ = this.MockLoggerFactory.Setup(f => f.CreateLogger(It.IsAny<string>()))
            .Returns(Mock.Of<ILogger>());
    });

    /// <summary>
    ///     Creates a small test window positioned off to the side of the screen.
    /// </summary>
    /// <param name="title">Optional window title.</param>
    /// <returns>A configured test window.</returns>
    protected static Window MakeSmallWindow(string? title = null)
    {
        var window = string.IsNullOrEmpty(title) ? new Window() : new Window { Title = title };

        // Set window to a small size (200x150) to not invade the screen during tests
        window.AppWindow.Resize(new Windows.Graphics.SizeInt32 { Width = 200, Height = 150 });

        // Position it off to the side of the screen
        window.AppWindow.Move(new Windows.Graphics.PointInt32 { X = 50, Y = 50 });

        return window;
    }

    /// <summary>
    ///     Creates a WindowManagerService instance with minimal dependencies.
    /// </summary>
    /// <returns>A configured WindowManagerService.</returns>
    protected WindowManagerService CreateService()
        => new(
            this.HostingContext,
            [],
            this.MockLoggerFactory.Object);
}
