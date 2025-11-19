// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window state management operations (minimize, maximize, restore) in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsStateManagement : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task MinimizeWindowAsync_RaisesPresenterStateEvents_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changed = new List<PresenterStateChangeEventArgs>();
        sut.PresenterStateChanged += (s, e) =>
        {
            changed.Add(e);
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            changed.Clear();

            // Act
            await sut.MinimizeWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = changed.Should().Contain(e => e.State == OverlappedPresenterState.Minimized && e.WindowId == context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task MaximizeWindowAsync_RaisesPresenterStateEvents_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changed = new List<PresenterStateChangeEventArgs>();
        sut.PresenterStateChanged += (s, e) =>
        {
            changed.Add(e);
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            changed.Clear();

            // Act
            await sut.MaximizeWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert
            _ = changed.Should().Contain(e => e.State == OverlappedPresenterState.Maximized && e.WindowId == context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RestoreWindowAsync_RaisesPresenterStateEvents_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();
        var changed = new List<PresenterStateChangeEventArgs>();
        sut.PresenterStateChanged += (s, e) =>
        {
            changed.Add(e);
            return Task.CompletedTask;
        };

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            changed.Clear();

            // Act - minimize then restore
            await sut.MinimizeWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Verify window is actually minimized
            _ = context.IsMinimized().Should().BeTrue("window should be minimized before restore");

            changed.Clear(); // Clear minimize events before testing restore

            await sut.RestoreWindowAsync(context.Id).ConfigureAwait(true);
            await Task.Delay(500, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Verify window is restored
            _ = context.IsMinimized().Should().BeFalse("window should not be minimized after restore");

            // Assert - expect at least one restore entry
            _ = changed.Should().Contain(e => e.State == OverlappedPresenterState.Restored && e.WindowId == context.Id);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
