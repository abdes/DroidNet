// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Controls;
using DroidNet.Routing;
using DroidNet.Storage.Native;
using DroidNet.Storage;
using DryIoc;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentBrowser.TestSupport;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Navigation;

[TestClass]
public sealed partial class BrowserRevealTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source link selects its material after replacing a layout and after a mount configuration update.</summary>
    /// <returns>The asynchronous real-browser regression.</returns>
    [TestMethod]
    public Task InspectionSourceLinkRevealsMaterialAcrossBrowserNavigation() => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        _ = fixture.Browser.LeftPaneViewModel.Should().BeOfType<ProjectLayoutViewModel>();
        _ = fixture.Browser.RightPaneViewModel.Should().BeOfType<AssetsViewModel>();
        _ = ((AssetsViewModel)fixture.Browser.RightPaneViewModel!).LayoutViewModel.Should().BeOfType<TilesLayoutViewModel>();
        fixture.Browser.Query.SearchText = "no matches";
        _ = (await fixture.Browser.ShowAssetAsync(fixture.Material.IdentityUri).ConfigureAwait(true)).Should().BeTrue();
        _ = fixture.Layout.SelectedAsset!.IdentityUri.Should().Be(fixture.Material.IdentityUri);
        _ = fixture.Browser.Query.SearchText.Should().BeEmpty();
        await fixture.Explorer.MountKnownLocationCommand.ExecuteAsync(KnownVirtualFolderMount.Cooked).ConfigureAwait(true);
        _ = (await fixture.Browser.ShowAssetAsync(fixture.Material.IdentityUri).ConfigureAwait(true)).Should().BeTrue();
        _ = fixture.Layout.SelectedAsset!.IdentityUri.Should().Be(fixture.Material.IdentityUri);
    });

    /// <summary>The actual Cooked menu restores an existing mount or immediately applies a new one.</summary>
    /// <param name="persisted">Whether Cooked was saved before browser startup.</param>
    /// <returns>The asynchronous menu and rendered-tree regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task CookedMountMenuImmediatelyShowsAndSelectsTheFolder(bool persisted) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture(persisted);
        await fixture.OpenAsync().ConfigureAwait(true);
        _ = fixture.Browser.LeftPaneViewModel.Should().BeOfType<ProjectLayoutViewModel>();
        var view = new ProjectLayoutView
        {
            ViewModel = fixture.Explorer
        };
        var root = new Grid
        {
            Width = 420,
            Height = 540,
            RequestedTheme = ElementTheme.Dark,
            Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.ColorHelper.FromArgb(255, 32, 32, 32))
        };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var toolbar = view.FindDescendant<ToolBar>()!;
        _ = toolbar.Should().NotBeNull("the view must realize its toolbar");
        var mount = toolbar.SecondaryItems.OfType<ToolBarButton>().Single(button => string.Equals(button.Label, "Mount", StringComparison.Ordinal));
        var menu = (MenuFlyout)mount.Flyout;
        menu.ShowAt(view);
        await WaitForRenderAsync().ConfigureAwait(true);
        var cooked = menu.Items.OfType<MenuFlyoutItem>().Single(item => string.Equals(item.Text, "Cooked", StringComparison.Ordinal));
        _ = cooked.Command.Should().NotBeNull("the Cooked menu must bind its command");
        _ = cooked.CommandParameter.Should().Be(KnownVirtualFolderMount.Cooked);
        var peer = new MenuFlyoutItemAutomationPeer(cooked);
        ((IInvokeProvider)peer.GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Explorer.MountKnownLocationCommand.ExecutionTask.Should().NotBeNull("invoking Cooked must execute its command");
        await fixture.Explorer.MountKnownLocationCommand.ExecutionTask!.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Explorer.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Cooked");
        _ = fixture.Explorer.SelectedItem.Should().NotBeNull("the mounted folder must become the tree selection");
        _ = fixture.Explorer.SelectedItem!.Label.Should().Be("Cooked");
        _ = fixture.Projects.ActiveProject!.AuthoringMounts.Should().ContainSingle(mount => mount.RelativePath == ".cooked");
        _ = fixture.MountChanges.Should().Be(persisted ? 0 : 1);
    });

    /// <summary>A local folder appears in the routed explorer after mounting and remains visible after refresh.</summary>
    /// <param name="navigateFirst">Whether to replace the explorer outlet after folder navigation.</param>
    /// <returns>The asynchronous rendered-browser regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task LocalFolderMountImmediatelyUpdatesTheDisplayedExplorer(bool navigateFirst) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        var library = Path.Combine(fixture.Projects.ActiveProject!.ProjectRoot, ".cooked", "main");
        _ = Directory.CreateDirectory(library);
        await File.WriteAllBytesAsync(Path.Combine(library, "container.index.bin"), [], this.TestContext.CancellationToken).ConfigureAwait(true);
        _ = fixture.Dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()))
            .Returns((DialogSpec spec, CancellationToken _) =>
            {
                var dialog = (LocalFolderMountDialogView)spec.Content!;
                dialog.ViewModel!.MountPointName = "Examples";
                dialog.ViewModel.SelectedFolderPath = library;
                return Task.FromResult(DialogButton.Primary);
            });
        var browserView = new ContentBrowserView { ViewModel = fixture.Browser, Width = 960, Height = 540 };
        await LoadTestContentAsync(browserView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        if (navigateFirst)
        {
            var previous = fixture.Explorer;
            await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            fixture.Browser.LoadContent(null!, "left");
            await fixture.Browser.NavigateToBreadcrumbAsync(0).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = fixture.Explorer.Should().NotBeSameAs(previous);
        }

        var view = browserView.FindDescendant<ProjectLayoutView>()!;
        _ = view.Should().NotBeNull("the router must display the project explorer");
        _ = view.ViewModel.Should().BeSameAs(fixture.Explorer);
        await fixture.Explorer.MountLocalFolderCommand.ExecuteAsync(null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Projects.ActiveProject!.LocalFolderMounts.Should().ContainSingle(mount => mount.Name == "Examples");
        _ = fixture.Explorer.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Examples", fixture.Diagnostics);
        var displayed = view.FindDescendant<DynamicTree>()!.DisplayedItems.Should().BeAssignableTo<IEnumerable<ITreeItem>>().Subject;
        _ = displayed.Should().Contain(item => item.Label == "Examples");
        _ = view.FindDescendant<TextBlock>(text => text.Text == "Examples").Should().NotBeNull();
        _ = Directory.CreateDirectory(Path.Combine(fixture.Projects.ActiveProject.ProjectRoot, "Refreshed"));
        await fixture.Browser.RefreshCommand.ExecuteAsync(null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Explorer.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Examples", fixture.Diagnostics);
        _ = view.FindDescendant<TextBlock>(text => text.Text == "Examples").Should().NotBeNull();
        _ = fixture.Explorer.ShownItems.Should().Contain(item => item.Label == "Refreshed");
        _ = view.FindDescendant<TextBlock>(text => text.Text == "Refreshed").Should().NotBeNull();
    });

    /// <summary>Imported output navigation opens the cooked tree and reveals all participating folders without per-row discovery.</summary>
    /// <param name="cookedAlias">An already saved output mount name, or null to create the default mount.</param>
    /// <returns>The asynchronous complete-browser imported-output regression.</returns>
    [TestMethod]
    [DataRow(null)]
    [DataRow("Published")]
    public Task ImportedOutputsOpenTogetherWithoutManualMounting(string? cookedAlias) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture(persisted: cookedAlias is not null, cookedAlias ?? "Cooked");
        var outputs = new[]
        {
            CreateNavigationAsset("/Content/Models/Crate/Materials/Paint.omat", AssetKind.Material),
            CreateNavigationAsset("/Content/Models/Crate/Geometry/Mesh.ogeo", AssetKind.Geometry),
        }.Select(static item => item with { SourcePath = null, DescriptorPath = null, CookedUri = item.IdentityUri, PrimaryState = AssetState.Cooked }).ToArray();
        await fixture.SetCookedOutputsAsync(outputs, this.TestContext.CancellationToken).ConfigureAwait(true);
        await fixture.OpenAsync().ConfigureAwait(true);
        fixture.Browser.Query.SearchText = "hidden";
        _ = (await fixture.Browser.ShowAssetsAsync(outputs.Select(static item => item.IdentityUri).ToArray()).ConfigureAwait(true)).Should().BeTrue(fixture.Diagnostics);
        _ = fixture.MountChanges.Should().Be(cookedAlias is null ? 1 : 0);
        _ = fixture.Layout.Assets.Select(static row => row.Item.IdentityUri).Should().BeEquivalentTo(outputs.Select(static item => item.IdentityUri));
        _ = fixture.Browser.Query.SearchText.Should().BeEmpty();
        _ = fixture.Layout.SelectedAsset!.IdentityUri.Should().Be(outputs[0].IdentityUri);
        fixture.Provider.Verify(value => value.ResolveAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>()), Times.Never);
    });

    /// <summary>Inspection and multi-output reveal navigate to the supplying library without mounting project output.</summary>
    /// <param name="fromInspection">Whether to use the single-asset inspection navigation.</param>
    /// <returns>The asynchronous library-folder navigation regression.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task CookedLibraryRevealUsesItsMountedPhysicalFolder(bool fromInspection) => EnqueueAsync(async () =>
    {
        using var fixture = new BrowserRevealFixture();
        var item = fixture.SetLibraryOutput();
        await fixture.OpenAsync().ConfigureAwait(true);
        fixture.Browser.Query.SearchText = "hidden";
        var revealed = fromInspection ? await fixture.Browser.ShowAssetAsync(item.IdentityUri).ConfigureAwait(true) : await fixture.Browser.ShowAssetsAsync([item.IdentityUri]).ConfigureAwait(true);
        _ = revealed.Should().BeTrue(fixture.Diagnostics);
        _ = fixture.MountChanges.Should().Be(0);
        _ = fixture.Layout.SelectedAsset!.IdentityUri.Should().Be(item.IdentityUri);
        _ = fixture.Layout.SelectedAsset.DisplayPath.Should().Be("/Library/Materials/Shared.omat");
        _ = fixture.Browser.Query.SearchText.Should().BeEmpty();
        _ = fixture.Projects.ActiveProject!.AuthoringMounts.Should().NotContain(mount => mount.RelativePath == ".cooked");
    });
}
