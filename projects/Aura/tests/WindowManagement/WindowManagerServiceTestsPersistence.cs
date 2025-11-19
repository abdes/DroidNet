// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Aura.Windowing;
using Microsoft.UI.Windowing;

namespace DroidNet.Aura.Tests;

/// <summary>
///     Tests for window placement persistence (serialize/restore) in <see cref="WindowManagerService"/>.
/// </summary>
[TestClass]
[TestCategory("UITest")]
[ExcludeFromCodeCoverage]
public class WindowManagerServiceTestsPersistence : WindowManagerServiceTestsBase
{
    [TestMethod]
    public Task GetWindowPlacementString_ReturnsValidJson_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act
            var placement = sut.GetWindowPlacementString(context.Id);

            // Assert
            _ = placement.Should().NotBeNull();

            using var doc = JsonDocument.Parse(placement!);
            var root = doc.RootElement;
            _ = root.TryGetProperty("Bounds", out _).Should().BeTrue();
            _ = root.TryGetProperty("MonitorWorkArea", out _).Should().BeTrue();
            _ = root.TryGetProperty("SavedAt", out _).Should().BeTrue();
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });

    [TestMethod]
    public Task RestoreWindowPlacementAsync_RestoresBounds_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var testWindow = MakeSmallWindow();

        var sut = this.CreateService();

        try
        {
            var context = await sut.RegisterDecoratedWindowAsync(testWindow, new("Test")).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            var originalPlacement = sut.GetWindowPlacementString(context.Id);
            using var doc = JsonDocument.Parse(originalPlacement!);
            var boundsEl = doc.RootElement.GetProperty("Bounds");
            var expected = new Windows.Graphics.RectInt32
            {
                X = boundsEl.GetProperty("X").GetInt32(),
                Y = boundsEl.GetProperty("Y").GetInt32(),
                Width = boundsEl.GetProperty("Width").GetInt32(),
                Height = boundsEl.GetProperty("Height").GetInt32(),
            };

            // Move/resize the window to different bounds
            var altered = new Windows.Graphics.RectInt32 { X = expected.X + 200, Y = expected.Y + 150, Width = expected.Width + 50, Height = expected.Height + 30 };
            await sut.SetWindowBoundsAsync(context.Id, altered).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);

            // Act - restore using the saved placement
            await sut.RestoreWindowPlacementAsync(context.Id, originalPlacement!).ConfigureAwait(true);
            await Task.Delay(250, this.TestContext.CancellationToken).ConfigureAwait(true);

            // Assert - managed context should reflect original bounds (or clamped variant)
            var actual = context.CurrentBounds;

            _ = actual.Width.Should().Be(expected.Width);
            _ = actual.Height.Should().Be(expected.Height);
            _ = actual.X.Should().Be(expected.X);
            _ = actual.Y.Should().Be(expected.Y);
        }
        finally
        {
            testWindow.Close();
            sut.Dispose();
        }
    });
}
