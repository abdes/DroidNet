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
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.Assets.Testing;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Managed.Core;
using static Oxygen.Editor.MaterialEditor.TestSupport.MaterialEditorModels;

namespace Oxygen.Editor.MaterialEditor.UI.Tests;

/// <summary>The material editor's document: its controls, its header and its cook status.</summary>
[TestClass]
internal sealed class MaterialEditorViewTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A visible material document follows the shared cook without reloading its editable controls.</summary>
    /// <returns>The asynchronous rendered-status regression.</returns>
    [TestMethod]
    public Task MaterialDocumentDisplaysBackgroundCookStatus() => EnqueueAsync(async () =>
    {
        var item = TestAssetEntries.CookedMaterial(0) with
        {
            Identity = AssetPath.Parse("/Content/Materials/UI.omat"),
        };
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        using var material = CreateMaterialEditor(StatusProvider(updates));
        var model = material.Editor;
        var view = new MaterialEditorView { ViewModel = model, Width = 920, Height = 600 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var label = (TextBlock)view.FindName("CookStatus");
        var chip = (Border)view.FindName("CookStatusChip");
        _ = label.Text.Should().Be("Cooked");
        _ = ToolTipService.GetToolTip(chip).Should().Be(model.CookStatusDescription);
        _ = AutomationProperties.GetName(chip).Should().Be("Cooked");
        foreach (var (state, text) in new[]
        {
            (CookRunState.Queued, "Queued"),
            (CookRunState.Cooking, "Cooking"),
            (CookRunState.Failed, "Cook failed"),
            (CookRunState.Succeeded, "Cooked"),
        })
        {
            await Task.Run(() => updates.OnNext([item with { CookActivity = new(Guid.NewGuid(), state) }]), this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = label.Text.Should().Be(text);
            _ = VisualStateManager.GetVisualStateGroups((FrameworkElement)view.Content).Single().CurrentState.Name.Should().Be(model.CookStatusTone);
            _ = model.RoughnessFactor.Should().Be(0.5f);
            _ = model.IsDirty.Should().BeFalse();
        }

        foreach (var (availability, text) in new[]
        {
            (AssetRuntimeAvailability.Mounted, "Ready"),
            (AssetRuntimeAvailability.Failed, "Preview issue"),
            (AssetRuntimeAvailability.Unavailable, "Cooked"),
        })
        {
            await Task.Run(() => updates.OnNext([item with { RuntimeAvailability = availability, RuntimeReason = "Native availability detail" }]), this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = label.Text.Should().Be(text);
            _ = ToolTipService.GetToolTip(chip).Should().Be(model.CookStatusDescription);
            _ = model.CookStatusDescription.Should().Contain("Native availability detail");
        }
    });

    /// <summary>The material editor realizes its controls without a generated XAML connection-ID cast failure.</summary>
    /// <returns>The XAML realization task.</returns>
    [TestMethod]
    public Task MaterialEditorXamlRealizesItsNumericControls() => EnqueueAsync(async () =>
    {
        using var material = CreateMaterialEditor();
        var view = new MaterialEditorView { ViewModel = material.Editor };
        await LoadTestContentAsync(view).ConfigureAwait(true);

        var roughness = await RealizeNumberAsync(view, "RoughnessFactor").ConfigureAwait(true);

        _ = roughness.Should().NotBeNull("the roughness field realizes once scrolled into view");
        _ = view.KeyboardAccelerators.Should().OnlyContain(accelerator => ReferenceEquals(accelerator.ScopeOwner, view));
    });

    /// <summary>The material document keeps its cook status beside the title and every header action inside the document.</summary>
    /// <param name="width">The document width in logical pixels.</param>
    /// <param name="rasterizationScale">The effective XAML scale.</param>
    /// <returns>The asynchronous header layout regression.</returns>
    [TestMethod]
    [DataRow(920d, 1d)]
    [DataRow(920d, 2d)]
    [DataRow(560d, 1.5d)]
    [DataRow(360d, 1d)]
    [DataRow(360d, 2d)]
    public Task MaterialHeaderKeepsStatusBesideTheTitleAndActionsInside(double width, double rasterizationScale) => EnqueueAsync(async () =>
    {
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([TestAssetEntries.CookedMaterial(0) with { Identity = AssetPath.Parse("/Content/Materials/UI.omat") }]);
        using var material = CreateMaterialEditor(StatusProvider(updates));
        var model = material.Editor;
        var view = new MaterialEditorView { ViewModel = model, Width = width, Height = 600 };
        using var scaledHost = new ScaledXamlHost();
        await scaledHost.LoadAsync(view, rasterizationScale, this.TestContext.CancellationToken).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var chip = (Border)view.FindName("CookStatusChip");
        _ = chip.ActualHeight.Should().BeLessThanOrEqualTo(26, "routine status uses one compact line");
        var chipPoint = chip.TransformToVisual(view).TransformPoint(default);
        _ = chipPoint.Y.Should().BeLessThan(40, "status belongs beside the title");
        var buttons = view.FindDescendants().OfType<Button>().Where(button => button.Content is "Undo" or "Redo" or "Save" or "Cook").ToArray();
        _ = buttons.Should().HaveCount(4);
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
    });

    // The content explorer's view of the document's asset, published by the test.
    internal static IAssetFeed StatusProvider(IObservable<IReadOnlyList<AssetEntry>> updates)
    {
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider.Object;
    }

    // Scrolls the document until the numeric control of a field is loaded.
    private static async Task<DroidNet.Controls.NumberBox?> RealizeNumberAsync(FrameworkElement view, string field)
    {
        var scroller = view.FindDescendant<ScrollViewer>()!;
        var offset = 0d;
        while (true)
        {
            if (view.FindDescendant<DroidNet.Controls.NumberBox>(number => Equals(number.Tag, field)) is { IsLoaded: true } number)
            {
                return number;
            }

            if (offset >= scroller.ScrollableHeight)
            {
                return null;
            }

            offset = Math.Min(offset + Math.Max(scroller.ViewportHeight / 2, 1), scroller.ScrollableHeight);
            _ = scroller.ChangeView(horizontalOffset: null, verticalOffset: offset, zoomFactor: null, disableAnimation: true);
            await WaitForRenderAsync().ConfigureAwait(true);
        }
    }
}
