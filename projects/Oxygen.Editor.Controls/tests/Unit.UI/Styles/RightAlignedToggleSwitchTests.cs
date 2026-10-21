// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Shapes;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.Styles;

/// <summary>A switch at the end of a settings row reads like Windows Settings: its On or Off text, then the rail at
/// the row's right edge.</summary>
[TestClass]
internal sealed class RightAlignedToggleSwitchTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task TheRailEndsTheRowAndTheTextComesBeforeIt() => EnqueueAsync(async () =>
    {
        var toggle = new ToggleSwitch
        {
            Style = (Style)Application.Current.Resources["RightAlignedToggleSwitchStyle"],
            IsOn = true,
        };
        var row = new Grid { Width = 300 };
        row.Children.Add(new TextBlock { Text = "VSync" });
        row.Children.Add(toggle);
        await LoadTestContentAsync(row).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var rail = toggle.FindDescendant<Rectangle>(part => string.Equals(part.Name, "OuterBorder", StringComparison.Ordinal))!;
        var text = toggle.FindDescendant<ContentPresenter>(part => string.Equals(part.Name, "OnContentPresenter", StringComparison.Ordinal))!;
        var railLeft = rail.TransformToVisual(row).TransformPoint(default).X;
        var textRight = text.TransformToVisual(row).TransformPoint(default).X + text.ActualWidth;

        _ = (railLeft + rail.ActualWidth).Should().BeApproximately(row.ActualWidth, 0.5, "the rail ends the row");
        _ = text.ActualWidth.Should().BeGreaterThan(0, "the switch says On");
        _ = textRight.Should().BeLessThanOrEqualTo(railLeft, "the text comes before the rail");
    });
}
