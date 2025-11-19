// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for WindowClosing event and cancellation scenarios in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsClosingEvent : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task WindowClosing_IsFiredBeforeClose_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var closingFired = false;

        sut.WindowClosing += (sender, args) =>
        {
            closingFired = true;
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = closingFired.Should().BeTrue();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosing_CanBeCancelled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        sut.WindowClosing += (sender, args) =>
        {
            args.Cancel = true; // Cancel the close operation
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var result = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = result.Should().BeFalse(); // Close was cancelled
            _ = sut.OpenWindows.Should().ContainSingle(); // Window is still open
            _ = sut.GetWindow(context.Id).Should().NotBeNull();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosing_WhenNotCancelled_WindowCloses_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var closingEventFired = false;

        sut.WindowClosing += (sender, args) =>
        {
            closingEventFired = true;
            args.Cancel = false; // Explicitly allow close
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var result = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = closingEventFired.Should().BeTrue();
            _ = result.Should().BeTrue(); // Close succeeded
            _ = sut.OpenWindows.Should().BeEmpty();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosing_MultipleHandlers_AllInvoked_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var handler1Called = false;
        var handler2Called = false;
        var handler3Called = false;

        sut.WindowClosing += (sender, args) =>
        {
            handler1Called = true;
            return Task.CompletedTask;
        };

        sut.WindowClosing += (sender, args) =>
        {
            handler2Called = true;
            return Task.CompletedTask;
        };

        sut.WindowClosing += (sender, args) =>
        {
            handler3Called = true;
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = handler1Called.Should().BeTrue();
            _ = handler2Called.Should().BeTrue();
            _ = handler3Called.Should().BeTrue();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosing_FirstHandlerCancels_SubsequentHandlersStillCalled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var handler2Called = false;

        sut.WindowClosing += (sender, args) =>
        {
            args.Cancel = true; // First handler cancels
            return Task.CompletedTask;
        };

        sut.WindowClosing += (sender, args) =>
        {
            handler2Called = true;
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            var result = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);

            // Assert
            _ = result.Should().BeFalse();
            _ = handler2Called.Should().BeTrue(); // Second handler should still be called
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosed_IsFiredAfterClose_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var closedFired = false;
        WindowId? closedWindowId = null;

        sut.WindowClosed += (sender, args) =>
        {
            closedFired = true;
            closedWindowId = args.WindowId;
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = closedFired.Should().BeTrue();
            _ = closedWindowId.Should().Be(context.Id);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosed_NotFiredWhenClosingCancelled_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var closedFired = false;
        WindowId? closedWindowId = null;

        sut.WindowClosing += (sender, args) =>
        {
            args.Cancel = true;
            return Task.CompletedTask;
        };

        sut.WindowClosed += (sender, args) =>
        {
            closedFired = true;
            closedWindowId = args.WindowId;
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = closedFired.Should().BeFalse(); // WindowClosed should not fire when close is cancelled
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task WindowClosing_AsyncHandler_AwaitsCompletion_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var asyncWorkCompleted = false;

        sut.WindowClosing += async (sender, args) =>
        {
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(false);
            asyncWorkCompleted = true;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);

            // Act
            _ = await sut.CloseWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(200, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = asyncWorkCompleted.Should().BeTrue();
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseAllWindows_RaisesClosingForEachWindow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();
        var window3 = MakeSmallWindow();

        var sut = this.CreateService();
        var closingCount = 0;

        sut.WindowClosing += (sender, args) =>
        {
            closingCount++;
            return Task.CompletedTask;
        };

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window3, new("Test3")).ConfigureAwait(true);

            // Act
            await sut.CloseAllWindowsAsync().ConfigureAwait(true);
            await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = closingCount.Should().Be(3);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task CloseAllWindows_WhenOneCloseIsCancelled_OthersStillClose_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var window1 = MakeSmallWindow();
        var window2 = MakeSmallWindow();
        var window3 = MakeSmallWindow();

        var sut = this.CreateService();
        IManagedWindow? window2Context = null;
        var window2Id = window2.AppWindow.Id;

        sut.WindowClosing += (sender, args) =>
        {
            // Cancel closing for window2 only
            if (args.WindowId == window2Id)
            {
                args.Cancel = true;
            }

            return Task.CompletedTask;
        };

        try
        {
            _ = await sut.RegisterDecoratedWindowAsync(window1, new("Test1")).ConfigureAwait(true);
            window2Context = await sut.RegisterDecoratedWindowAsync(window2, new("Test2")).ConfigureAwait(true);
            _ = await sut.RegisterDecoratedWindowAsync(window3, new("Test3")).ConfigureAwait(true);

            // Act
            await sut.CloseAllWindowsAsync().ConfigureAwait(true);
            await Task.Delay(300, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - window2 should still be open, others closed
            _ = sut.OpenWindows.Should().ContainSingle();
            _ = sut.OpenWindows.First().Id.Should().Be(window2Context.Id);
        }
        finally
        {
            window2.Close();
            sut.Dispose();
        }
    });
}
