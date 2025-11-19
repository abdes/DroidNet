// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window minimum size constraints in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsMinimumSize : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task SetWindowMinimumSizeAsync_UpdatesPresenterConstraints_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            const int minWidth = 400;
            const int minHeight = 300;

            // Act
            await sut.SetWindowMinimumSizeAsync(context.Id, minWidth, minHeight).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - verify properties on ManagedWindow
            _ = context.MinimumWidth.Should().Be(minWidth);
            _ = context.MinimumHeight.Should().Be(minHeight);

            // Assert - verify constraints on OverlappedPresenter
            if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                _ = presenter.PreferredMinimumWidth.Should().Be(minWidth);
                _ = presenter.PreferredMinimumHeight.Should().Be(minHeight);
            }
            else
            {
                Assert.Fail("Expected OverlappedPresenter");
            }
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_PreventsResizingSmallerThanMinimum_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            const int minWidth = 400;
            const int minHeight = 300;

            await sut.SetWindowMinimumSizeAsync(context.Id, minWidth, minHeight).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Act - Try to resize smaller than minimum
            var tooSmallSize = new Windows.Graphics.SizeInt32 { Width = 200, Height = 150 };
            await sut.ResizeWindowAsync(context.Id, tooSmallSize).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - window should be constrained to minimum size
            _ = context.CurrentBounds.Width.Should().BeGreaterThanOrEqualTo(minWidth);
            _ = context.CurrentBounds.Height.Should().BeGreaterThanOrEqualTo(minHeight);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_WithNullValues_RemovesConstraints_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Set constraints first
            await sut.SetWindowMinimumSizeAsync(context.Id, 400, 300).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Act - Remove constraints by passing null
            await sut.SetWindowMinimumSizeAsync(context.Id, minimumWidth: null, minimumHeight: null).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - verify properties are null
            _ = context.MinimumWidth.Should().BeNull();
            _ = context.MinimumHeight.Should().BeNull();

            // Assert - verify presenter constraints are null
            if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                _ = presenter.PreferredMinimumWidth.Should().BeNull();
                _ = presenter.PreferredMinimumHeight.Should().BeNull();
            }
            else
            {
                Assert.Fail("Expected OverlappedPresenter");
            }
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_WithPartialConstraints_OnlyAppliesSpecified_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            const int minWidth = 400;

            // Act - Set only width constraint
            await sut.SetWindowMinimumSizeAsync(context.Id, minWidth, minimumHeight: null).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - width is set, height is null
            _ = context.MinimumWidth.Should().Be(minWidth);
            _ = context.MinimumHeight.Should().BeNull();

            if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                _ = presenter.PreferredMinimumWidth.Should().Be(minWidth);
                _ = presenter.PreferredMinimumHeight.Should().BeNull();
            }
            else
            {
                Assert.Fail("Expected OverlappedPresenter");
            }
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_AllowsResizingLargerThanMinimum_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            const int minWidth = 300;
            const int minHeight = 200;

            await sut.SetWindowMinimumSizeAsync(context.Id, minWidth, minHeight).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Act - Resize larger than minimum
            var largerSize = new Windows.Graphics.SizeInt32 { Width = 600, Height = 500 };
            await sut.ResizeWindowAsync(context.Id, largerSize).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - window should accept the larger size
            _ = context.CurrentBounds.Width.Should().Be(largerSize.Width);
            _ = context.CurrentBounds.Height.Should().Be(largerSize.Height);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_NonExistentWindow_DoesNotThrow_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var sut = this.CreateService();

        try
        {
            var fakeWindowId = new WindowId { Value = 99999 };

            // Act & Assert - should not throw
            await sut.SetWindowMinimumSizeAsync(fakeWindowId, 400, 300).ConfigureAwait(true);
        }
        finally
        {
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task SetWindowMinimumSizeAsync_UpdatesExistingConstraints_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Set initial constraints
            await sut.SetWindowMinimumSizeAsync(context.Id, 300, 200).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            const int newMinWidth = 500;
            const int newMinHeight = 400;

            // Act - Update constraints
            await sut.SetWindowMinimumSizeAsync(context.Id, newMinWidth, newMinHeight).ConfigureAwait(true);
            await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - new constraints are applied
            _ = context.MinimumWidth.Should().Be(newMinWidth);
            _ = context.MinimumHeight.Should().Be(newMinHeight);

            if (context.Window.AppWindow.Presenter is OverlappedPresenter presenter)
            {
                _ = presenter.PreferredMinimumWidth.Should().Be(newMinWidth);
                _ = presenter.PreferredMinimumHeight.Should().Be(newMinHeight);
            }
            else
            {
                Assert.Fail("Expected OverlappedPresenter");
            }
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
