// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.WorldEditor.TestSupport;
using Windows.Foundation;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

/// <summary>Verifies bounded inspector field discovery during scrolling and layout changes.</summary>
[TestClass]
internal sealed class InspectorFieldDiscoveryTests : VisualUserInterfaceTests
{
    /// <summary>Gets or sets the context for the current test execution.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Finds a hit-testable caption after a pending scroll and a content-height change.</summary>
    /// <param name="initialOffset">The requested offset before field discovery.</param>
    /// <param name="part">The caption template part to locate.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(0d, "PartValueTextBlock")]
    [DataRow(800d, "PartValueTextBlock")]
    [DataRow(1400d, "PartValueTextBlock")]
    [DataRow(0d, "PartLabelTextBlock")]
    [DataRow(800d, "PartLabelTextBlock")]
    [DataRow(1400d, "PartLabelTextBlock")]
    public Task FindFieldPendingScrollAndLayoutChangeSettlesOnVisibleCaption(double initialOffset, string part) => EnqueueAsync(async () =>
    {
        var spacer = new Border { Height = 1000 };
        var number = new NumberBox { Label = "Near plane", NumberValue = 1 };
        var content = new StackPanel();
        content.Children.Add(spacer);
        content.Children.Add(number);
        content.Children.Add(new Border { Height = 600 });
        var scroller = new ScrollViewer { Width = 320, Height = 160, Content = content };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        _ = scroller.ChangeView(horizontalOffset: null, initialOffset, zoomFactor: null, disableAnimation: true);
        spacer.Height += 100;
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(5));

        var found = await InspectorControls.FindInspectorControlAsync(
            scroller, () => number, "Near plane", timeout.Token, part).ConfigureAwait(true);

        _ = found.Should().BeSameAs(number);
        var caption = number.FindDescendant<TextBlock>(element => string.Equals(element.Name, part, StringComparison.Ordinal))!;
        var center = caption.TransformToVisual(scroller).TransformPoint(new Point(caption.ActualWidth / 2, caption.ActualHeight / 2));
        _ = center.Y.Should().BePositive().And.BeLessThan(scroller.ViewportHeight);
        _ = caption.ActualHeight.Should().BePositive();
    });

    /// <summary>Propagates cancellation rather than returning an unresolved field.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public Task FindFieldCancelledRequestPropagatesCancellation() => EnqueueAsync(async () =>
    {
        var scroller = new ScrollViewer { Width = 320, Height = 160, Content = new Border { Height = 1000 } };
        await LoadTestContentAsync(scroller).ConfigureAwait(true);
        using var cancelled = new CancellationTokenSource();
        await cancelled.CancelAsync().ConfigureAwait(true);

        Func<Task> find = () => InspectorControls.FindInspectorControlAsync(scroller, () => null, "Missing", cancelled.Token);

        _ = await find.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(true);
    });
}
