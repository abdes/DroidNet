// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using CommunityToolkit.WinUI.Controls;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.Inspector;

/// <summary>
/// A source-mode field lays out as every property row does, shows its modes in a <see cref="Segmented"/> that a mixed
/// index leaves unselected, and shows its caption line and source button only when they have something to say.
/// </summary>
[TestClass]
internal sealed class InspectorSourceModeFieldTests : VisualUserInterfaceTests
{
    private static readonly string[] Modes = ["Inherit", "Shown", "Hidden"];

    [TestMethod]
    [DataRow(760d, PropertyLayout.Inline)]
    [DataRow(260d, PropertyLayout.Stacked)]
    public Task TheRowStacksByWidthAndTheModesStayInside(double width, PropertyLayout expected) => EnqueueAsync(async () =>
    {
        var field = new InspectorSourceModeField { Label = "Scene visibility", ModeNames = Modes, SelectedIndex = 0 };
        var host = new Grid { Width = width };
        host.Children.Add(field);
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        var card = field.FindDescendant<PropertyCard>()!;
        var modes = field.FindDescendant<Segmented>()!;
        var modesRight = modes.TransformToVisual(host).TransformPoint(default).X + modes.ActualWidth;

        _ = card.ActualLayout.Should().Be(expected);
        _ = modes.Items.Count.Should().Be(Modes.Length);
        _ = modesRight.Should().BeLessThanOrEqualTo(width + 0.5, "the modes never overflow the row");
    });

    [TestMethod]
    public Task AMixedIndexSelectsNoModeAndAChosenModeIsTheFieldsIndex() => EnqueueAsync(async () =>
    {
        var field = new InspectorSourceModeField { Label = "Cast shadows", ModeNames = Modes, SelectedIndex = -1 };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var modes = field.FindDescendant<Segmented>()!;

        _ = modes.SelectedIndex.Should().Be(-1);
        _ = modes.SelectedItem.Should().BeNull();

        modes.SelectedIndex = 2;

        _ = field.SelectedIndex.Should().Be(2);
    });

    [TestMethod]
    public Task TheCaptionLineAndSourceButtonShowOnlyWithContent() => EnqueueAsync(async () =>
    {
        var field = new InspectorSourceModeField { Label = "Scene visibility", ModeNames = Modes, SelectedIndex = 0 };
        var requests = 0;
        field.SourceRequested += (_, _) => requests++;
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var captionRow = field.FindDescendant<Grid>(grid => string.Equals(grid.Name, "CaptionRow", StringComparison.Ordinal))!;
        var source = field.FindDescendant<Button>(button => string.Equals(button.Name, "SourceButton", StringComparison.Ordinal))!;
        _ = captionRow.Visibility.Should().Be(Visibility.Collapsed);

        field.Caption = "Hidden, from Lamp";
        _ = captionRow.Visibility.Should().Be(Visibility.Visible);
        _ = source.Visibility.Should().Be(Visibility.Collapsed);

        field.SourceToolTip = "Select Lamp";
        _ = source.Visibility.Should().Be(Visibility.Visible);
        ((IInvokeProvider)new ButtonAutomationPeer(source).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = requests.Should().Be(1);
    });
}
