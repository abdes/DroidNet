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
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Testing;

namespace Oxygen.Editor.Pickers.UI.Tests;

/// <summary>
/// The asset picker button shows its label, raises <see cref="AssetPickerButton.PickerOpening"/> when its flyout opens,
/// and hides the flyout before it reports the picked choice.
/// </summary>
[TestClass]
internal sealed class AssetPickerButtonTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task TheButtonShowsTheLabelAsContentTooltipAndAutomationName() => EnqueueAsync(async () =>
    {
        var picker = new AssetPickerButton { Label = "Material 00", AutomationName = "Material", Model = CreateModel() };
        await LoadTestContentAsync(new Grid { Width = 400, Children = { picker } }).ConfigureAwait(true);
        var button = picker.FindDescendant<SplitButton>()!;

        _ = button.Content.Should().Be("Material 00");
        _ = ToolTipService.GetToolTip(button).Should().Be("Material 00");
        _ = AutomationProperties.GetName(button).Should().Be("Material");

        picker.Label = "Material 01";

        _ = button.Content.Should().Be("Material 01");
        _ = ToolTipService.GetToolTip(button).Should().Be("Material 01");
    });

    [TestMethod]
    public Task OpeningTheFlyoutCreatesThePickerAndRaisesPickerOpeningOnce() => EnqueueAsync(async () =>
    {
        var picker = new AssetPickerButton { Label = "Material 00", Model = CreateModel() };
        var opening = 0;
        picker.PickerOpening += (_, _) => opening++;
        await LoadTestContentAsync(new Grid { Width = 400, Children = { picker } }).ConfigureAwait(true);
        var button = picker.FindDescendant<SplitButton>()!;
        var flyout = (Flyout)button.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        _ = opening.Should().Be(0);
        _ = flyout.Content.Should().BeNull("the picker is created when the flyout first opens");

        flyout.ShowAt(button);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);

            _ = opening.Should().Be(1);
            _ = flyout.Content.Should().BeOfType<AssetPicker>().Which.Model.Should().BeSameAs(picker.Model);
        }
        finally
        {
            flyout.Hide();
        }
    });

    [TestMethod]
    public Task PickingAChoiceClosesTheFlyoutAndRaisesChoiceInvoked() => EnqueueAsync(async () =>
    {
        var material = TestAssetEntries.CookedMaterial(0);
        var model = CreateModel();
        model.Update([material]);
        var picker = new AssetPickerButton { Label = "Material 00", Model = model };
        var invoked = new List<AssetChoice>();
        picker.ChoiceInvoked += (_, choice) => invoked.Add(choice);
        await LoadTestContentAsync(new Grid { Width = 400, Children = { picker } }).ConfigureAwait(true);
        var button = picker.FindDescendant<SplitButton>()!;
        var flyout = (Flyout)button.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        var closed = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        flyout.Closed += (_, _) => closed.TrySetResult();

        flyout.ShowAt(button);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var choice = ((FrameworkElement)flyout.Content).FindDescendant<Button>(candidate => candidate.DataContext is AssetChoiceRow row && row.Choice.Asset == material.Identity)!;

            ((IInvokeProvider)new ButtonAutomationPeer(choice).GetPattern(PatternInterface.Invoke)).Invoke();
            await closed.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);

            _ = invoked.Should().ContainSingle().Which.Asset.Should().Be(material.Identity);
            _ = flyout.IsOpen.Should().BeFalse();
        }
        finally
        {
            flyout.Hide();
        }
    });

    private static AssetPickerModel CreateModel() => new(static asset => AssetChoice.For(asset, "Material", string.Empty));
}
