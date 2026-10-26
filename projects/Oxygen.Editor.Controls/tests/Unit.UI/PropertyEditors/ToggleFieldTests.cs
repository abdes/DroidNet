// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Windows.Input;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Shapes;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
/// A toggle field shows a mixed value as Off with "Mixed" and sets On with the first click; its row shows the override bar
/// at full or half strength and the reset button only while the value is overridden.
/// </summary>
[TestClass]
internal sealed class ToggleFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task AMixedValueShowsOffWithMixedAndTheFirstClickSetsOn() => EnqueueAsync(async () =>
    {
        var field = new ToggleField { Label = "Visible", Value = null };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var toggle = field.FindDescendant<ToggleSwitch>()!;

        _ = toggle.IsOn.Should().BeFalse();
        _ = toggle.OffContent.Should().Be("Mixed");

        ((IToggleProvider)new ToggleSwitchAutomationPeer(toggle).GetPattern(PatternInterface.Toggle)).Toggle();

        _ = field.Value.Should().BeTrue();
        _ = toggle.OffContent.Should().Be(string.Empty);
    });

    [TestMethod]
    public Task ASetValueMovesTheSwitchWithoutReportingAClick() => EnqueueAsync(async () =>
    {
        var field = new ToggleField { Label = "Visible", Value = true };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var toggle = field.FindDescendant<ToggleSwitch>()!;
        _ = toggle.IsOn.Should().BeTrue();

        field.Value = false;

        _ = toggle.IsOn.Should().BeFalse();
        _ = field.Value.Should().BeFalse();
    });

    [TestMethod]
    [DataRow(OverrideState.None, 0d, false)]
    [DataRow(OverrideState.Partial, 0.5d, true)]
    [DataRow(OverrideState.Full, 1d, true)]
    public Task TheRowShowsTheOverrideBarAndResetOnlyWhileOverridden(OverrideState state, double opacity, bool resets) => EnqueueAsync(async () =>
    {
        var reset = new CountingCommand();
        var field = new ToggleField
        {
            Label = "Cast shadows",
            Value = false,
            OverrideState = state,
            ResetCommand = reset,
            ResetToolTip = "Reset to the asset value (On)",
        };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var bar = field.FindDescendant<Rectangle>(rectangle => string.Equals(rectangle.Name, "OverrideBar", StringComparison.Ordinal))!;
        var button = field.FindDescendant<Button>(candidate => string.Equals(candidate.Name, "ResetButton", StringComparison.Ordinal))!;

        _ = bar.Visibility.Should().Be(state == OverrideState.None ? Visibility.Collapsed : Visibility.Visible);
        _ = bar.Opacity.Should().Be(opacity);
        _ = button.Visibility.Should().Be(resets ? Visibility.Visible : Visibility.Collapsed);
        _ = ToolTipService.GetToolTip(button).Should().Be("Reset to the asset value (On)");
        if (resets)
        {
            ((IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke)).Invoke();
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = reset.Count.Should().Be(1);
        }
    });

    [TestMethod]
    public Task TheResetButtonTakesItsWidthFromTheRowNotFromBeyondIt() => EnqueueAsync(async () =>
    {
        var field = new ToggleField { Label = "Visible", Value = true, OverrideState = OverrideState.Full, ResetCommand = new CountingCommand() };
        var host = new Grid { Width = 300, Children = { field } };
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var button = field.FindDescendant<Button>(candidate => string.Equals(candidate.Name, "ResetButton", StringComparison.Ordinal))!;
        var right = button.TransformToVisual(host).TransformPoint(default).X + button.ActualWidth;

        _ = right.Should().BeLessThanOrEqualTo(300 + 0.5, "the reset button stays inside the row");
    });

    [TestMethod]
    public Task RowsWithAndWithoutResetStartTheirEditorsAtTheSameEdge() => EnqueueAsync(async () =>
    {
        var reset = new ToggleField { Label = "Visible", Value = true, OverrideState = OverrideState.Full, ResetCommand = new CountingCommand() };
        var plain = new ToggleField { Label = "Cast shadows", Value = true };
        var host = new StackPanel { Width = 360, Children = { reset, plain } };
        await LoadTestContentAsync(host).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);

        double EditorLeft(ToggleField field) => field.FindDescendant<ToggleSwitch>()!.TransformToVisual(host).TransformPoint(default).X;

        _ = EditorLeft(reset).Should().BeApproximately(EditorLeft(plain), 0.5, "the reset button narrows the editor, not the label column");
    });

    private sealed class CountingCommand : ICommand
    {
        public event EventHandler? CanExecuteChanged
        {
            add { }
            remove { }
        }

        public int Count { get; private set; }

        public bool CanExecute(object? parameter) => true;

        public void Execute(object? parameter) => this.Count++;
    }
}
