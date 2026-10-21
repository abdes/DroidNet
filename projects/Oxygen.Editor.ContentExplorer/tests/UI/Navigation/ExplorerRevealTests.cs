// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentExplorer.TestSupport;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

[TestClass]
public sealed partial class ExplorerRevealTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source link selects its material after replacing a layout and after a mount configuration update.</summary>
    /// <returns>The asynchronous real-browser regression.</returns>
    [TestMethod]
    public Task InspectionSourceLinkRevealsMaterialAcrossExplorerNavigation() => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        _ = fixture.Explorer.LeftPaneViewModel.Should().BeOfType<ContentFoldersViewModel>();
        _ = fixture.Explorer.RightPaneViewModel.Should().BeOfType<AssetsViewModel>();
        _ = ((AssetsViewModel)fixture.Explorer.RightPaneViewModel!).LayoutViewModel.Should().BeOfType<TilesLayoutViewModel>();
        fixture.Explorer.Query.SearchText = "no matches";
        _ = (await fixture.Explorer.ShowAssetAsync(fixture.Material.Identity).ConfigureAwait(true)).Should().BeTrue();
        _ = fixture.Layout.SelectedAsset!.Identity.Should().Be(fixture.Material.Identity);
        _ = fixture.Explorer.Query.SearchText.Should().BeEmpty();
        await fixture.Folders.MountKnownLocationCommand.ExecuteAsync(KnownVirtualFolderMount.Cooked).ConfigureAwait(true);
        _ = (await fixture.Explorer.ShowAssetAsync(fixture.Material.Identity).ConfigureAwait(true)).Should().BeTrue();
        _ = fixture.Layout.SelectedAsset!.Identity.Should().Be(fixture.Material.Identity);
    });

    /// <summary>The actual Cooked menu restores an existing mount or immediately applies a new one.</summary>
    /// <param name="persisted">Whether Cooked was saved before browser startup.</param>
    /// <returns>The asynchronous menu and rendered-tree regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task CookedMountDialogImmediatelyShowsAndSelectsTheFolder(bool persisted) => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture(persisted);
        await fixture.OpenAsync().ConfigureAwait(true);
        _ = fixture.Dialogs.Setup(value => value.ShowAsync(It.IsAny<DialogSpec>(), It.IsAny<CancellationToken>()))
            .Returns((DialogSpec spec, CancellationToken _) =>
            {
                var mounts = ((ContentMountsView)spec.Content!).ViewModel!;
                mounts.SelectedKind = "Cooked";
                mounts.AddMountCommand.Execute(parameter: null);
                return Task.FromResult(DialogButton.Primary);
            });
        _ = fixture.Explorer.LeftPaneViewModel.Should().BeOfType<ContentFoldersViewModel>();
        var view = new ContentFoldersView
        {
            ViewModel = fixture.Folders,
        };
        var root = new Grid
        {
            Width = 420,
            Height = 540,
            RequestedTheme = ElementTheme.Dark,
            Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.ColorHelper.FromArgb(255, 32, 32, 32)),
        };
        root.Children.Add(view);
        await LoadTestContentAsync(root).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var mountsButton = view.FindDescendant<Button>(button => string.Equals(AutomationProperties.GetName(button), "Mounts", StringComparison.Ordinal))!;
        _ = mountsButton.Should().NotBeNull("the sources footer must offer Mounts");
        _ = view.FindDescendant<Button>(button => string.Equals(AutomationProperties.GetName(button), "Content priority", StringComparison.Ordinal)).Should().NotBeNull();
        ((IInvokeProvider)new ButtonAutomationPeer(mountsButton).GetPattern(PatternInterface.Invoke)).Invoke();
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Folders.ManageMountsCommand.ExecutionTask.Should().NotBeNull("Mounts must open the Content mounts dialog");
        await fixture.Folders.ManageMountsCommand.ExecutionTask!.ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Folders.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Cooked");
        if (!persisted)
        {
            // A newly added mount becomes the location.
            _ = fixture.Folders.SelectedItem.Should().NotBeNull("the mounted folder must become the tree selection");
            _ = fixture.Folders.SelectedItem!.Label.Should().Be("Cooked");
        }
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
        using var fixture = new ExplorerRevealFixture();
        await fixture.OpenAsync().ConfigureAwait(true);
        var library = Path.Combine(fixture.Projects.ActiveProject!.ProjectRoot.FullPath, ".cooked", "main");
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
        var browserView = new ContentExplorerView { ViewModel = fixture.Explorer, Width = 960, Height = 540 };
        await LoadTestContentAsync(browserView).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        if (navigateFirst)
        {
            var previous = fixture.Folders;
            await fixture.NavigateHistoryFolderAsync("Materials", this.TestContext.CancellationToken).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            fixture.Explorer.LoadContent(null!, "left");

            // The Content breadcrumb is a URL the router does not hold yet, so the explorer outlet is activated again.
            await fixture.Explorer.NavigateToBreadcrumbAsync(1).ConfigureAwait(true);
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = fixture.Folders.Should().NotBeSameAs(previous);
        }

        var view = browserView.FindDescendant<ContentFoldersView>()!;
        _ = view.Should().NotBeNull("the router must display the project explorer");
        _ = view.ViewModel.Should().BeSameAs(fixture.Folders);
        await fixture.Folders.MountLocalFolderCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Projects.ActiveProject!.LocalFolderMounts.Should().ContainSingle(mount => mount.Name == "Examples");
        _ = fixture.Folders.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Examples", fixture.Diagnostics);
        var displayed = view.FindDescendant<DynamicTree>()!.DisplayedItems.Should().BeAssignableTo<IEnumerable<ITreeItem>>().Subject;
        _ = displayed.Should().Contain(item => item.Label == "Examples");
        _ = view.FindDescendant<TextBlock>(text => text.Text == "Examples").Should().NotBeNull();
        _ = Directory.CreateDirectory(Path.Combine(fixture.Projects.ActiveProject.ProjectRoot.FullPath, "Refreshed"));
        await fixture.Explorer.RefreshCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = fixture.Folders.ShownItems.Should().Contain(item => item is VirtualFolderMountTreeItemAdapter && item.Label == "Examples", fixture.Diagnostics);
        _ = view.FindDescendant<TextBlock>(text => text.Text == "Examples").Should().NotBeNull();
        _ = fixture.Folders.ShownItems.Should().Contain(item => item.Label == "Refreshed");
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
        using var fixture = new ExplorerRevealFixture(persisted: cookedAlias is not null, cookedAlias ?? "Cooked");
        var outputs = new[]
        {
            CreateNavigationAsset("/Content/Models/Crate/Materials/Paint.omat", AssetKind.Material),
            CreateNavigationAsset("/Content/Models/Crate/Geometry/Mesh.ogeo", AssetKind.Geometry),
        }.Select(static item => item with { SourcePath = null, DescriptorPath = null, PrimaryState = AssetState.Cooked }).ToArray();
        await fixture.SetCookedOutputsAsync(outputs, this.TestContext.CancellationToken).ConfigureAwait(true);
        await fixture.OpenAsync().ConfigureAwait(true);
        fixture.Explorer.Query.SearchText = "hidden";
        _ = (await fixture.Explorer.ShowAssetsAsync(outputs.Select(static item => item.Identity).ToArray()).ConfigureAwait(true)).Should().BeTrue(fixture.Diagnostics);
        _ = fixture.MountChanges.Should().Be(cookedAlias is null ? 1 : 0);
        _ = fixture.Layout.Assets.Select(static row => row.Item.Identity).Should().BeEquivalentTo(outputs.Select(static item => item.Identity));
        _ = fixture.Explorer.Query.SearchText.Should().BeEmpty();
        _ = fixture.Layout.SelectedAsset!.Identity.Should().Be(outputs[0].Identity);
        fixture.Provider.Verify(value => value.ResolveAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()), Times.Never);
    });

    /// <summary>Inspection and multi-output reveal navigate to the supplying library without mounting project output.</summary>
    /// <param name="fromInspection">Whether to use the single-asset inspection navigation.</param>
    /// <returns>The asynchronous library-folder navigation regression.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public Task CookedLibraryRevealUsesItsMountedPhysicalFolder(bool fromInspection) => EnqueueAsync(async () =>
    {
        using var fixture = new ExplorerRevealFixture();
        var item = fixture.SetLibraryOutput();
        await fixture.OpenAsync().ConfigureAwait(true);
        fixture.Explorer.Query.SearchText = "hidden";
        var revealed = fromInspection ? await fixture.Explorer.ShowAssetAsync(item.Identity).ConfigureAwait(true) : await fixture.Explorer.ShowAssetsAsync([item.Identity]).ConfigureAwait(true);
        _ = revealed.Should().BeTrue(fixture.Diagnostics);
        _ = fixture.MountChanges.Should().Be(0);
        _ = fixture.Layout.SelectedAsset!.Identity.Should().Be(item.Identity);
        _ = fixture.Layout.SelectedAsset.DisplayPath.Should().Be("/Library/Materials/Shared.omat");
        _ = fixture.Explorer.Query.SearchText.Should().BeEmpty();
        _ = fixture.Projects.ActiveProject!.AuthoringMounts.Should().NotContain(mount => mount.RelativePath == ".cooked");
    });
}
