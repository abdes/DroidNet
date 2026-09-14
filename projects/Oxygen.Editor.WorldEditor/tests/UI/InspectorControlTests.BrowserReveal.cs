// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Controls;
using DroidNet.Routing;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using DryIoc;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Assets.Import;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises inspection navigation and virtual mounts through the complete browser router and real folder tree.</summary>
public sealed partial class InspectorControlTests
{
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
        var view = new ProjectLayoutView { ViewModel = fixture.Explorer };
        var root = new Grid { Width = 420, Height = 540, RequestedTheme = ElementTheme.Dark, Background = new Microsoft.UI.Xaml.Media.SolidColorBrush(Microsoft.UI.ColorHelper.FromArgb(255, 32, 32, 32)) };
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
        await this.CaptureQueryLayoutAsync(root, "cooked-mount-menu-" + persisted + ".png").ConfigureAwait(true);
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
        fixture.SetCookedOutputs(outputs);
        await fixture.OpenAsync().ConfigureAwait(true);
        fixture.Browser.Query.SearchText = "hidden";
        _ = (await fixture.Browser.ShowAssetsAsync(outputs.Select(static item => item.IdentityUri).ToArray()).ConfigureAwait(true)).Should().BeTrue(fixture.Diagnostics);
        _ = fixture.MountChanges.Should().Be(cookedAlias is null ? 1 : 0);
        _ = fixture.Layout.Assets.Select(static row => row.Item.IdentityUri).Should().BeEquivalentTo(outputs.Select(static item => item.IdentityUri));
        _ = fixture.Browser.Query.SearchText.Should().BeEmpty();
        _ = fixture.Layout.SelectedAsset!.IdentityUri.Should().Be(outputs[0].IdentityUri);
        fixture.Provider.Verify(value => value.ResolveAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>()), Times.Never);
    });

    private sealed partial class BrowserRevealFixture : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-BrowserReveal-");
        private readonly Container container = new();
        private readonly BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>> items;
        private readonly Mock<ILogger> logger = new();

        public BrowserRevealFixture(bool persisted = false, string cookedAlias = "Cooked")
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content", "Materials"));
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, ".cooked", "Content", "Materials"));
            this.Material = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
            this.items = new([this.Material]);
            var provider = CreateQueryProvider(this.items);
            this.Provider = provider;
            _ = provider.Setup(value => value.ResolveAsync(this.Material.IdentityUri, It.IsAny<CancellationToken>())).ReturnsAsync(this.Material);
            var info = new ProjectInfo("Browser", Category.Games, this.directory.FullName) { AuthoringMounts = [new("Content", "Content")] };
            if (persisted)
            {
                info.AuthoringMounts.Add(new(cookedAlias, ".cooked"));
            }

            this.Projects.Activate(ProjectContext.FromProjectInfo(info));
            var messenger = new StrongReferenceMessenger();
            messenger.Register<ChangeContentMountsRequestMessage>(this, (_, message) =>
            {
                this.MountChanges++;
                this.Projects.Activate(ProjectContext.FromProjectInfo(message.Candidate));
                message.Reply(Task.FromResult(true));
            });
            this.container.RegisterInstance<IProjectContextService>(this.Projects);
            this.container.RegisterInstance<IStorageProvider>(new NativeStorageProvider(new RealFileSystem()));
            this.container.RegisterInstance<IMessenger>(messenger);
            this.container.RegisterInstance(provider.Object);
            this.container.RegisterInstance(CreateStatusHosting());
            _ = this.logger.Setup(value => value.IsEnabled(It.IsAny<LogLevel>())).Returns(value: true);
            var logging = new Mock<ILoggerFactory>();
            _ = logging.Setup(value => value.CreateLogger(It.IsAny<string>())).Returns(this.logger.Object);
            this.container.RegisterInstance<ILoggerFactory>(logging.Object);
            this.container.RegisterInstance<IBuiltinCatalogDiscovery>(new Oxygen.Testing.BuiltinCatalogDiscoveryFixture());
            this.container.RegisterInstance(Mock.Of<IDialogService>());
            this.container.RegisterInstance(Mock.Of<IProjectAssetCatalog>());
            this.container.RegisterInstance(Mock.Of<IAssetCatalog>());
            this.container.RegisterInstance(Mock.Of<ICookRunService>());
            this.container.RegisterInstance(Mock.Of<IProjectManagerService>());
            this.container.RegisterInstance(Mock.Of<IAuthoringTargetResolver>());
            this.container.RegisterInstance(Mock.Of<IContentPipelineService>());
            this.container.RegisterInstance(Mock.Of<IOperationResultPublisher>());
            this.container.RegisterInstance(Mock.Of<IStatusReducer>());
            this.container.RegisterInstance(Mock.Of<IWindowManagerService>());
            this.Browser = new(this.container, Mock.Of<IRouter>(), this.Projects, Mock.Of<IProjectUsageService>(), Mock.Of<IOperationResultPublisher>(), Mock.Of<IStatusReducer>(), logging.Object);
        }

        public ProjectContextService Projects { get; } = new();

        public ContentBrowserAssetItem Material { get; }

        public ContentBrowserViewModel Browser { get; }

        public Mock<IContentBrowserAssetProvider> Provider { get; }

        public string Diagnostics => string.Join(Environment.NewLine, this.logger.Invocations.Where(static call => string.Equals(call.Method.Name, "Log", StringComparison.Ordinal))
            .Select(static call => call.Arguments[2]?.ToString() + " " + call.Arguments[3]?.ToString()));

        public ProjectLayoutViewModel Explorer => (ProjectLayoutViewModel)this.Browser.LeftPaneViewModel!;

        public AssetsLayoutViewModel Layout => (AssetsLayoutViewModel)((AssetsViewModel)this.Browser.RightPaneViewModel!).LayoutViewModel!;

        public int MountChanges { get; private set; }

        public void SetCookedOutputs(IReadOnlyList<ContentBrowserAssetItem> outputs)
        {
            foreach (var item in outputs)
            {
                var relative = Uri.UnescapeDataString(item.IdentityUri.AbsolutePath).TrimStart('/');
                _ = Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(this.directory.FullName, ".cooked", relative))!);
            }

            this.items.OnNext(outputs);
        }

        public async Task OpenAsync()
        {
            await this.Browser.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), Mock.Of<INavigationContext>(value => value.NavigationTarget == new object())).ConfigureAwait(true);
            _ = this.Browser.LeftPaneViewModel.Should().NotBeNull(this.Diagnostics);
        }

        public void Dispose()
        {
            this.Browser.Dispose();
            this.container.Dispose();
            this.items.Dispose();
            this.directory.Delete(recursive: true);
        }
    }
}
