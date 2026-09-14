// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Tests;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Markup;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.MaterialEditor;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks material status bindings with updates arriving from background cooking.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A visible material document follows the shared cook without reloading its editable controls.</summary>
    /// <param name="width">The document width in logical pixels.</param>
    /// <param name="theme">The rendered editor theme.</param>
    /// <param name="rasterizationScale">The effective XAML scale.</param>
    /// <returns>The asynchronous rendered-status regression.</returns>
    [TestMethod]
    [DataRow(920d, ElementTheme.Dark, 1d)]
    [DataRow(920d, ElementTheme.Dark, 1.5d)]
    [DataRow(920d, ElementTheme.Dark, 2d)]
    [DataRow(560d, ElementTheme.Light, 1d)]
    [DataRow(560d, ElementTheme.Light, 1.5d)]
    [DataRow(560d, ElementTheme.Light, 2d)]
    [DataRow(360d, ElementTheme.Dark, 1d)]
    [DataRow(360d, ElementTheme.Dark, 1.5d)]
    [DataRow(360d, ElementTheme.Dark, 2d)]
    public Task MaterialDocumentDisplaysBackgroundCookStatus(double width, ElementTheme theme, double rasterizationScale) => EnqueueAsync(async () =>
    {
        var item = CreateStatusAsset(0) with { IdentityUri = new("asset:///Content/Materials/UI.omat.json") };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([item]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = CreateMaterialEditor(provider.Object);
        var view = new MaterialEditorView { ViewModel = model, Width = width, Height = 600, RequestedTheme = theme };
        var host = (Border)XamlReader.Load("<Border xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' Background='{ThemeResource ApplicationPageBackgroundThemeBrush}' />");
        host.RequestedTheme = theme;
        host.Width = width;
        host.Height = view.Height;
        host.Child = view;
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(host, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var label = (TextBlock)view.FindName("CookStatus");
        var chip = (Border)view.FindName("CookStatusChip");
        _ = label.Text.Should().Be("Cooked");
        _ = chip.ActualHeight.Should().BeLessThanOrEqualTo(26, "routine status uses one compact line");
        _ = chip.TransformToVisual(view).TransformPoint(default).Y.Should().BeLessThan(40, "status belongs beside the title");
        _ = ToolTipService.GetToolTip(chip).Should().Be(model.CookStatusDescription);
        _ = AutomationProperties.GetName(chip).Should().Be("Cooked");
        AssertMaterialHeaderActions(view, chip);
        await this.CaptureComponentLayoutAsync(host, string.Create(System.Globalization.CultureInfo.InvariantCulture, $"material-status-{width}-{theme}-{rasterizationScale}.png")).ConfigureAwait(true);
        foreach (var (state, text) in new[] { (CookRunState.Queued, "Queued"), (CookRunState.Cooking, "Cooking"), (CookRunState.Failed, "Cook failed"), (CookRunState.Succeeded, "Cooked") })
        {
            await Task.Run(() => updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), state) }]), this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = label.Text.Should().Be(text);
            _ = VisualStateManager.GetVisualStateGroups((FrameworkElement)view.Content).Single().CurrentState.Name.Should().Be(model.CookStatusTone);
            _ = model.RoughnessFactor.Should().Be(0.5f);
            _ = model.IsDirty.Should().BeFalse();
        }

        foreach (var (availability, text) in new[] { (AssetRuntimeAvailability.Mounted, "Ready"), (AssetRuntimeAvailability.Failed, "Preview issue"), (AssetRuntimeAvailability.Unavailable, "Cooked") })
        {
            await Task.Run(() => updates.OnNext([item with { RuntimeAvailability = availability, RuntimeReason = "Native availability detail" }]), this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = label.Text.Should().Be(text);
            _ = ToolTipService.GetToolTip(chip).Should().Be(model.CookStatusDescription);
            _ = model.CookStatusDescription.Should().Contain("Native availability detail");
        }
    });

    private static void AssertMaterialHeaderActions(MaterialEditorView view, Border chip)
    {
        var buttons = view.FindDescendants().OfType<Button>().Where(button => button.Content is "Undo" or "Redo" or "Save" or "Cook").ToArray();
        _ = buttons.Should().HaveCount(4);
        var chipPoint = chip.TransformToVisual(view).TransformPoint(default);
        foreach (var button in buttons)
        {
            var point = button.TransformToVisual(view).TransformPoint(default);
            _ = point.X.Should().BeGreaterThanOrEqualTo(0);
            _ = (point.X + button.ActualWidth).Should().BeLessThanOrEqualTo(view.ActualWidth);
            if (Math.Abs(point.Y - chipPoint.Y) < 26)
            {
                _ = (chipPoint.X + chip.ActualWidth).Should().BeLessThanOrEqualTo(point.X);
            }
        }
    }
}
