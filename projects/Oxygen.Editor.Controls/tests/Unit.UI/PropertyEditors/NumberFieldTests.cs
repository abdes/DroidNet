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
using Oxygen.Editor.Controls.PropertyEditors;

namespace Oxygen.Editor.Controls.Unit.UI.Tests.PropertyEditors;

/// <summary>
/// A number field's row shows the override bar at full or half strength and the reset button only while the value is
/// overridden, as the toggle field's row does.
/// </summary>
[TestClass]
internal sealed class NumberFieldTests : VisualUserInterfaceTests
{
    [TestMethod]
    [DataRow(OverrideState.None, 0d, false)]
    [DataRow(OverrideState.Partial, 0.5d, true)]
    [DataRow(OverrideState.Full, 1d, true)]
    public Task TheRowShowsTheOverrideBarAndResetOnlyWhileOverridden(OverrideState state, double opacity, bool resets) => EnqueueAsync(async () =>
    {
        var reset = new CountingCommand();
        var field = new NumberField
        {
            Label = "Intensity",
            NumberValue = 2d,
            OverrideState = state,
            ResetCommand = reset,
            ResetToolTip = "Reset to the asset value (1)",
        };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var bar = field.FindDescendant<Rectangle>(rectangle => string.Equals(rectangle.Name, "OverrideBar", StringComparison.Ordinal))!;
        var button = field.FindDescendant<Button>(candidate => string.Equals(candidate.Name, "ResetButton", StringComparison.Ordinal))!;

        _ = bar.Visibility.Should().Be(state == OverrideState.None ? Visibility.Collapsed : Visibility.Visible);
        _ = bar.Opacity.Should().Be(opacity);
        _ = button.Visibility.Should().Be(resets ? Visibility.Visible : Visibility.Collapsed);
        _ = ToolTipService.GetToolTip(button).Should().Be("Reset to the asset value (1)");
        if (resets)
        {
            ((IInvokeProvider)new ButtonAutomationPeer(button).GetPattern(PatternInterface.Invoke)).Invoke();
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = reset.Count.Should().Be(1);
        }
    });

    [TestMethod]
    public Task TheFieldAndItsValueBoxStayEqualAfterTheBoxIsEdited() => EnqueueAsync(async () =>
    {
        // Contract: an edit in the box reaches the field, and a later value of the field, such as a view model's
        // refusal of the edit, still reaches the box.
        var field = new NumberField { Label = "Intensity", NumberValue = 80d };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var box = field.FindDescendant<DroidNet.Controls.NumberBox>()!;
        _ = box.NumberValue.Should().Be(80);

        box.NumberValue = 100;
        _ = field.NumberValue.Should().Be(100);

        field.NumberValue = 80;
        _ = box.NumberValue.Should().Be(80);
    });

    [TestMethod]
    public Task ACollapsedFieldsCardIsFoundOnceItsTemplateIsApplied() => EnqueueAsync(async () =>
    {
        var field = new NumberField { Label = "Hidden", Visibility = Visibility.Collapsed };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);

        _ = Oxygen.Editor.Controls.Testing.PropertyEditorProbes.CardOf(field).PropertyName.Should().Be("Hidden");
    });

    [TestMethod]
    public Task TheValueBoxTakesThePropertyNumberBoxStyleWithoutAConsumerStyle() => EnqueueAsync(async () =>
    {
        // Contract: a field's NumberBox uses PropertyNumberBoxStyle (right-aligned value, trailing zeros trimmed),
        // resolved by the library; NumberBox's own defaults are centered and untrimmed.
        var field = new NumberField { Label = "Intensity", NumberValue = 2d, Tag = "Intensity" };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { field } }).ConfigureAwait(true);
        var number = field.FindDescendant<DroidNet.Controls.NumberBox>(box => string.Equals(box.Name, "PART_NumberBox", StringComparison.Ordinal))!;

        _ = number.Should().NotBeNull("the template names its NumberBox part");
        _ = number.TrimTrailingZeros.Should().BeTrue();
        _ = number.HorizontalValueAlignment.Should().Be(TextAlignment.Right);
        _ = number.Tag.Should().Be("Intensity", "the field's tag names the edited field on its NumberBox too");
        _ = field.IsTabStop.Should().BeFalse("the NumberBox, not the field, takes focus");
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
