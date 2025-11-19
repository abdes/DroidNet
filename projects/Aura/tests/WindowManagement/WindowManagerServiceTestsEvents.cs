// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for the WindowEvents observable stream in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsEvents : WindowManagerServiceTestsBase
{
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
}
