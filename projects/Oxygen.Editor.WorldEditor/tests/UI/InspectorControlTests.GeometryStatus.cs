// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.World.Inspector.Geometry;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks geometry choices against shared status and proven built-in origins.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Status updates keep the focused geometry button and do not create duplicate built-in choices.</summary>
    /// <returns>The asynchronous rendered-picker regression.</returns>
    [TestMethod]
    public Task GeometryPickerSharesStatusWithoutReplacingItsFocusedChoice() => EnqueueAsync(async () =>
    {
        var asset = CreateStatusAsset(0) with { IdentityUri = new("asset:///Content/Geometry/Custom.ogeo.json"), DisplayName = "Custom", Kind = AssetKind.Geometry, CookedUri = new("asset:///Content/Geometry/Custom.ogeo") };
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var engine = builtins.Snapshot.Catalog!.CreateCatalogRecords()[0];
        var copy = asset with { IdentityUri = new("asset://" + engine.Generated!.CookedVirtualPath), Generated = engine.Generated, BuiltinOriginUri = engine.Uri, PrimaryState = AssetState.Generated };
        using var updates = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([asset, copy]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var materials = new Mock<IMaterialPickerService>();
        _ = materials.SetupGet(value => value.Results).Returns(Observable.Return<IReadOnlyList<MaterialPickerResult>>([]));
        _ = materials.Setup(value => value.RefreshAsync(It.IsAny<MaterialPickerFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        using var model = new GeometryViewModel(CreateStatusHosting(), provider.Object, materials.Object, builtins) { IsExpanded = true };
        var view = new GeometryView { ViewModel = model, Width = 440 };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var content = model.Groups.Single(group => string.Equals(group.Key, "Content", StringComparison.Ordinal));
        var row = content.Items.Should().ContainSingle().Subject;
        _ = row.Item.Uri.Should().Be(asset.IdentityUri);
        var owner = (SplitButton)view.FindName("AssetSplitButton");
        var flyout = (Flyout)owner.Flyout;
        flyout.AreOpenCloseAnimationsEnabled = false;
        flyout.ShowAt(owner);
        try
        {
            await WaitForRenderAsync().ConfigureAwait(true);
            var button = ((FrameworkElement)flyout.Content).FindDescendant<Button>(candidate => ReferenceEquals(candidate.DataContext, row))!;
            _ = button.Focus(FocusState.Programmatic);
            updates.OnNext([asset with { CookActivity = new(Guid.NewGuid(), CookRunState.Queued) }, copy]);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = content.Items.Should().ContainSingle().Which.Should().BeSameAs(row);
            _ = button.DataContext.Should().BeSameAs(row);
            _ = button.FindDescendant<TextBlock>(label => string.Equals(label.Text, "Geometry · Queued", StringComparison.Ordinal)).Should().NotBeNull();
            _ = Microsoft.UI.Xaml.Input.FocusManager.GetFocusedElement(view.XamlRoot).Should().BeSameAs(button);
            _ = model.Groups.Single(group => string.Equals(group.Key, "Engine", StringComparison.Ordinal)).Items.Should().HaveCount(11);
        }
        finally
        {
            flyout.Hide();
        }
    });
}
