// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.Projects;
using static DroidNet.Tests.UiTestHosting;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Assets;

[TestClass]
public sealed partial class BuiltinCatalogTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The catalog notice stays scoped to engine browsing and clears when retry succeeds.</summary>
    /// <returns>The asynchronous browser-scope regression.</returns>
    [TestMethod]
    public Task BuiltinCatalogNoticeFollowsBrowserScopeAndRetry() => EnqueueAsync(async () =>
    {
        var discovery = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        var live = discovery.Snapshot;
        discovery.SetSnapshot(new(Catalog: null, IsLastKnown: false, "Engine catalog unavailable. No last-known catalog is available."));
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<ContentBrowserAssetItem>>([]));
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var projects = new ProjectContextService();
        var state = new ContentBrowserState(projects);
        state.SetSelectedFolders(["/Content/Materials"]);
        using var model = new ListLayoutViewModel(provider.Object, projects, state, CreateStatusHosting(), discovery);
        var view = new ListLayoutView
        {
            ViewModel = model,
            Width = 500,
            Height = 300,
        };
        await model.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        await LoadTestContentAsync(view).ConfigureAwait(true);
        var notice = view.FindDescendant<InfoBar>()!;
        _ = notice.IsOpen.Should().BeFalse();
        state.SetSelectedFolders(["/Engine"]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = notice.IsOpen.Should().BeTrue();
        _ = notice.Message.Should().Contain("No last-known catalog");
        discovery.RefreshResult = live;
        await model.RetryBuiltinCatalogCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = notice.IsOpen.Should().BeFalse();
    });
}
