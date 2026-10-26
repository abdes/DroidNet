// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.Feedback;

/// <summary>
/// A diagnostic card shows its message and the items it is about: a few inline, every one in place once expanded, never
/// only in a tooltip; its background, not an icon, says how serious it is.
/// </summary>
[TestClass]
internal sealed class DiagnosticCardTests : VisualUserInterfaceTests
{
    private static readonly string[] Materials = ["Bun", "Cheese", "Sauce", "Lettuce", "Patty"];

    [TestMethod]
    [DataRow(1, "Bun")]
    [DataRow(3, "Bun, Cheese and Sauce")]
    [DataRow(5, "Bun, Cheese, Sauce and 2 more")]
    public void PreviewOf_NamesFewItemsAndCountsTheRest(int count, string expected)
        => _ = DiagnosticCard.PreviewOf(Materials[..count], 3).Should().Be(expected);

    [TestMethod]
    public Task ACardWithManyItemsExpandsToShowEveryOne() => EnqueueAsync(async () =>
    {
        var card = new DiagnosticCard { Severity = DiagnosticCardSeverity.Warning, Message = "Sampling is not imported.", Details = Materials };
        await LoadTestContentAsync(new Grid { Width = 280, Children = { card } }).ConfigureAwait(true);
        var preview = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PART_Preview", StringComparison.Ordinal))!;
        var expand = card.FindDescendant<Button>(button => string.Equals(button.Name, "PART_Expand", StringComparison.Ordinal))!;

        _ = preview.Text.Should().Be("Bun, Cheese, Sauce and 2 more");
        _ = expand.Visibility.Should().Be(Visibility.Visible);

        ((IInvokeProvider)new ButtonAutomationPeer(expand).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);

        _ = card.IsExpanded.Should().BeTrue();
        _ = preview.Visibility.Should().Be(Visibility.Collapsed);
        _ = card.FindDescendants().OfType<TextBlock>().Where(static text => text.Visibility == Visibility.Visible).Select(static text => text.Text)
            .Should().ContainInOrder(Materials);
        _ = AutomationProperties.GetName(expand).Should().Be("Show fewer");
    });

    [TestMethod]
    public Task ACardWithFewItemsShowsThemWithoutAnExpander() => EnqueueAsync(async () =>
    {
        var card = new DiagnosticCard { Message = "Animations are not imported.", Details = ["Drive"] };
        await LoadTestContentAsync(new Grid { Width = 280, Children = { card } }).ConfigureAwait(true);

        _ = card.FindDescendant<Button>(button => string.Equals(button.Name, "PART_Expand", StringComparison.Ordinal))!.Visibility.Should().Be(Visibility.Collapsed);
        _ = card.FindDescendant<TextBlock>(text => string.Equals(text.Name, "PART_Preview", StringComparison.Ordinal))!.Text.Should().Be("Drive");
    });

    [TestMethod]
    public Task TheSeverityIsTheBackgroundAndTheAccessibleName() => EnqueueAsync(async () =>
    {
        var info = new DiagnosticCard { Message = "Noted." };
        var warning = new DiagnosticCard { Severity = DiagnosticCardSeverity.Warning, Message = "Dropped." };
        var error = new DiagnosticCard { Severity = DiagnosticCardSeverity.Error, Message = "Failed." };
        await LoadTestContentAsync(new StackPanel { Width = 280, Children = { info, warning, error } }).ConfigureAwait(true);

        var backgrounds = new[] { info, warning, error }
            .Select(static card => ((SolidColorBrush)card.FindDescendant<Border>(border => string.Equals(border.Name, "Root", StringComparison.Ordinal))!.Background).Color)
            .ToArray();
        _ = backgrounds.Should().OnlyHaveUniqueItems("each severity has its own background");
        _ = AutomationProperties.GetName(warning).Should().Be("Warning: Dropped.");
        _ = warning.FindDescendants().OfType<FontIcon>().Should().BeEmpty("the card carries no severity icon");
    });
}
