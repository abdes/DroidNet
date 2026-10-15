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
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.MaterialEditor;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorModels;

namespace Oxygen.Editor.WorldEditor.Unit.UI.Tests.Inspector;

[TestClass]
internal sealed class MaterialStatusTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A visible material document follows the shared cook without reloading its editable controls.</summary>
    /// <returns>The asynchronous rendered-status regression.</returns>
    [TestMethod]
    public Task MaterialDocumentDisplaysBackgroundCookStatus() => EnqueueAsync(async () =>
    {
        var item = CreateStatusAsset(0) with
        {
            IdentityUri = new("asset:///Content/Materials/UI.omat.json"),
        };
        using var updates = new BehaviorSubject<IReadOnlyList<AssetEntry>>([item]);
        using var model = CreateMaterialEditor(StatusProvider(updates));
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

    // The content browser's view of the document's asset, published by the test.
    internal static IAssetFeed StatusProvider(IObservable<IReadOnlyList<AssetEntry>> updates)
    {
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider.Object;
    }
}
