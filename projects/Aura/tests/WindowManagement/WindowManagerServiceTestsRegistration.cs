// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Reactive.Linq;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window registration functionality in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsRegistration : WindowManagerServiceTestsBase
{
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
            var toolWindows = sut.OpenWindows.Where(w => w.Category == WindowCategory.Tool).ToList();

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
}
