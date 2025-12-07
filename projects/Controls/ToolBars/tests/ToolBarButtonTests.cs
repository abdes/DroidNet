// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls.ToolBars.Tests;

[ExcludeFromCodeCoverage]
[TestClass]
[TestCategory("UITest")]
public class ToolBarButtonTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task SetsTemplatePartsCorrectly() => EnqueueAsync(async () =>
    {
        var (button, _) = await SetUpontrolForTesting().ConfigureAwait(true);

        // Assert - Verify all required template parts are present
        _ = GetPartOrFail<FrameworkElement>(button, ToolBarButton.RootGridPartName);
        _ = GetPartOrFail<FrameworkElement>(button, ToolBarButton.ContentPresenterPartName);
        _ = GetPartOrFail<FrameworkElement>(button, ToolBarButton.IconPresenterPartName);
        _ = GetPartOrFail<FrameworkElement>(button, ToolBarButton.LabelTextPartName);
    });

    [TestMethod]
    public Task DisabledVisualState_ShouldEnterDisabledState() => EnqueueAsync(async () =>
    {
        // Arrange
        var (button, root) = await SetUpontrolForTesting().ConfigureAwait(true);

        // Ensure the VSM is attached exactly to the element that has the visual state groups defined.
        var vsm = new TestVisualStateManager();
        VisualStateManager.SetCustomVisualStateManager(root, vsm);

        // Act - toggle disabled
        button.IsEnabled = false;

        // Render and wait for the VisualStateManager (test shim) to record the state transition.
        await WaitForStateAsync(vsm, button, "Disabled", TimeSpan.FromSeconds(1)).ConfigureAwait(true);

        // Assert - Disabled visual state should be active on the control
        _ = vsm.GetCurrentStates(button).Should().Contain("Disabled");
    });

    [TestMethod]
    public Task DisabledVisualState_ShouldApplyDisabledBrush() => EnqueueAsync(async () =>
    {
        var (button, root) = await SetUpontrolForTesting().ConfigureAwait(true);
        var icon = GetPartOrFail<IconSourceElement>(button, ToolBarButton.IconPresenterPartName);
        var label = GetPartOrFail<TextBlock>(button, ToolBarButton.LabelTextPartName);
        var expected = Application.Current.Resources["TextFillColorDisabledBrush"] as SolidColorBrush;

        // Act - toggle disabled
        button.IsEnabled = false;

        // Render and wait for the VisualStateManager (test shim) to record the state transition.
        var vsm = new TestVisualStateManager();
        VisualStateManager.SetCustomVisualStateManager(root, vsm);
        await WaitForStateAsync(vsm, button, ToolBarButton.DisabledVisualState, TimeSpan.FromSeconds(1)).ConfigureAwait(true);

        // Check actual brush color (compare Color rather than object identity)
        _ = icon!.Foreground.Should().BeOfType<SolidColorBrush>();
        _ = label!.Foreground.Should().BeOfType<SolidColorBrush>();
        _ = ((SolidColorBrush)icon.Foreground).Color.Should().Be(expected!.Color);
        _ = ((SolidColorBrush)label.Foreground).Color.Should().Be(expected.Color);
    });

    private static async Task<(ToolBarButton button, Grid rootGrid)> SetUpontrolForTesting()
    {
        var button = new ToolBarButton
        {
            Icon = new SymbolIconSource { Symbol = Symbol.Save },
            Label = "Save",
            Width = 48,
            Height = 48,
        };

        await LoadTestContentAsync(button).ConfigureAwait(true);
        return (button, GetPartOrFail<Grid>(button, ToolBarButton.RootGridPartName));
    }

    private static T GetPartOrFail<T>(ToolBarButton button, string partName)
        where T : FrameworkElement
    {
        var part = TryGetPart<T>(button, partName);
        _ = part.Should().NotBeNull($"{partName} should be there");
        return part!;
    }

    private static T? TryGetPart<T>(ToolBarButton button, string partName)
        where T : FrameworkElement
        => button.FindDescendant<T>(e => string.Equals(e.Name, partName, StringComparison.Ordinal));
}
