// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Storage;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.Projects;

namespace Oxygen.Editor.World.Tests;

/// <summary>Exercises browser scope changes on the real UI dispatcher.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>A reused list/tiles model follows URL scope immediately and rejects delayed selection/invocation from the old folder.</summary>
    /// <param name="tiles">Whether to exercise the tile projection.</param>
    /// <returns>The asynchronous scope regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ReusedLayoutDropsOldFolderRowsAndActionTargets(bool tiles) => EnqueueAsync(async () =>
    {
        var projects = new ProjectContextService();
        projects.Activate(new()
        {
            ProjectId = Guid.NewGuid(), Name = "Navigation", Category = Category.Games, ProjectRoot = "C:/Navigation",
            AuthoringMounts = [], LocalFolderMounts = [], Scenes = [],
        });
        var state = new ContentBrowserState(projects);
        var geometry = CreateNavigationAsset("/Content/Geometry/Cube.ogeo.json", AssetKind.Geometry);
        var material = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
        using var items = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([geometry, material]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(items);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var hosting = CreateStatusHosting();
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel layout = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, hosting, builtins)
            : new ListLayoutViewModel(provider.Object, projects, state, hosting, builtins);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)?selected=%2FContent%2FGeometry");
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView { ViewModel = (TilesLayoutViewModel)layout }
            : new ListLayoutView { ViewModel = (ListLayoutViewModel)layout };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var selector = view.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        _ = layout.SelectedAsset!.IdentityUri.Should().Be(geometry.IdentityUri);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)?selected=%2FContent%2FMaterials");
        _ = layout.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
        _ = layout.SelectedAsset.Should().BeNull();
        _ = selector.SelectedItem.Should().BeNull();
        layout.SelectedAsset = geometry;
        _ = layout.SelectedAsset.Should().BeNull();
        var invoked = false;
        layout.ItemInvoked += (_, _) => invoked = true;
        if (layout is TilesLayoutViewModel tileModel)
        {
            tileModel.InvokeItemCommand.Execute(geometry);
        }
        else
        {
            ((ListLayoutViewModel)layout).InvokeItemCommand.Execute(geometry);
        }

        _ = invoked.Should().BeFalse();
        items.OnNext([material, geometry]);
        await WaitForRenderAsync().ConfigureAwait(true);
        _ = layout.Assets.Should().ContainSingle().Which.Item.IdentityUri.Should().Be(material.IdentityUri);
    });

    /// <summary>A delayed folder lookup cannot replace a newer folder selection when it completes.</summary>
    /// <returns>The asynchronous real-tree navigation regression.</returns>
    [TestMethod]
    public Task SlowFolderLookupCannotRestoreObsoleteTreeSelection() => EnqueueAsync(async () =>
    {
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var leaf = CreateNavigationFolder("Leaf", "C:/Navigation/Content/Slow/Leaf", []);
        var slow = CreateNavigationFolder("Slow", "C:/Navigation/Content/Slow", [leaf], release.Task, entered);
        var fast = CreateNavigationFolder("Fast", "C:/Navigation/Content/Fast", []);
        var content = CreateNavigationFolder("Content", "C:/Navigation/Content", [fast, slow]);
        var root = CreateNavigationFolder("Navigation", "C:/Navigation", [content]);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(root.Location, It.IsAny<CancellationToken>())).ReturnsAsync(root);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(content.Location, It.IsAny<CancellationToken>())).ReturnsAsync(content);
        var projects = new ProjectContextService();
        projects.Activate(new()
        {
            ProjectId = Guid.NewGuid(), Name = "Navigation", Category = Category.Games, ProjectRoot = root.Location,
            AuthoringMounts = [new("Content", "Content") { IsExpanded = false }], LocalFolderMounts = [], Scenes = [],
        });
        var state = new ContentBrowserState(projects);
        using var model = new ProjectLayoutViewModel(
            projects,
            storage.Object,
            state,
            Mock.Of<IDialogService>(),
            new ViewModelToView(Mock.Of<IViewLocator>()),
            new StrongReferenceMessenger(),
            NullLoggerFactory.Instance);
        try
        {
            await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
            state.SetSelectedFolders(["/Content/Slow/Leaf"]);
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            state.SetSelectedFolders(["/Content/Fast"]);
            release.SetResult();
            await WaitForRenderAsync().ConfigureAwait(true);
            _ = state.SelectedFolders.Should().Equal("/Content/Fast");
            var tree = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
            var mount = (await tree.Children.ConfigureAwait(true)).OfType<AuthoringMountPointTreeItemAdapter>().Single();
            var folders = (await mount.Children.ConfigureAwait(true)).OfType<FolderTreeItemAdapter>().ToArray();
            _ = folders.Single(folder => string.Equals(folder.Label, "Fast", StringComparison.Ordinal)).IsSelected.Should().BeTrue();
            var slowAdapter = folders.Single(folder => string.Equals(folder.Label, "Slow", StringComparison.Ordinal));
            _ = (await slowAdapter.Children.ConfigureAwait(true)).Should().OnlyContain(item => !item.IsSelected);
        }
        finally
        {
            _ = release.TrySetResult();
        }
    });

    /// <summary>A confirmed rename applies immediately; a failed change restores the accepted tree.</summary>
    /// <param name="succeeds">Whether project persistence accepts the save.</param>
    /// <returns>The asynchronous mount-persistence regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task BrowserMountSavePreservesOriginsAndFailureState(bool succeeds) => EnqueueAsync(async () =>
    {
        var root = CreateNavigationFolder("Navigation", "C:/Navigation", []);
        var content = CreateNavigationFolder("Content", "C:/Navigation/Content", []);
        var cooked = CreateNavigationFolder("Cooked", "C:/Navigation/.cooked", []);
        var library = CreateNavigationFolder("Library", "D:/Library", []);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        foreach (var folder in new[] { root, content, cooked, library })
        {
            _ = storage.Setup(value => value.GetFolderFromPathAsync(folder.Location, It.IsAny<CancellationToken>())).ReturnsAsync(folder);
        }

        var projects = new ProjectContextService();
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(), Name = "Navigation", Category = Category.Games, ProjectRoot = root.Location,
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")], LocalFolderMounts = [new("Library", "D:/Library")], Scenes = [],
        };
        projects.Activate(project);
        var messenger = new StrongReferenceMessenger();
        ProjectInfo? saved = null;
        messenger.Register<ChangeContentMountsRequestMessage>(this, (_, message) =>
        {
            saved = message.Candidate;
            if (succeeds)
            {
                projects.Activate(ProjectContext.FromProjectInfo(message.Candidate));
            }

            message.Reply(Task.FromResult(succeeds));
        });
        var state = new ContentBrowserState(projects);
        using var model = new ProjectLayoutViewModel(
            projects,
            storage.Object,
            state,
            Mock.Of<IDialogService>(),
            new ViewModelToView(Mock.Of<IViewLocator>()),
            messenger,
            NullLoggerFactory.Instance);
        await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
        var tree = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        var renamed = tree.VirtualFolderMounts.Single(mount => string.Equals(mount.MountPointName, "Library", StringComparison.Ordinal));
        state.SetSelectedFolders(["/Library"]);
        renamed.Label = "LibraryRenamed";
        await model.PendingMountChange.ConfigureAwait(true);
        _ = saved.Should().NotBeNull();
        _ = saved!.AuthoringMounts.Should().Contain(mount => mount.Name == "Content" && mount.RelativePath == "Content");
        _ = saved.AuthoringMounts.Should().Contain(mount => mount.Name == "Cooked" && mount.RelativePath == ".cooked");
        _ = saved.LocalFolderMounts.Should().Contain(mount => mount.Name == "LibraryRenamed" && mount.AbsolutePath == "D:/Library");
        _ = model.HasUnsavedChanges.Should().BeFalse();
        _ = state.SelectedFolders.Should().Equal(succeeds ? "/LibraryRenamed" : "/Library");
        var restored = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        _ = restored.VirtualFolderMounts.Should().Contain(mount => mount.MountPointName == (succeeds ? "LibraryRenamed" : "Library"));
        if (!succeeds)
        {
            _ = projects.ActiveProject.Should().BeSameAs(project);
        }
    });

    private static IFolder CreateNavigationFolder(string name, string path, IReadOnlyList<IFolder> children, Task? wait = null, TaskCompletionSource? entered = null)
    {
        var folder = new Mock<IFolder>();
        _ = folder.SetupGet(value => value.Name).Returns(name);
        _ = folder.SetupGet(value => value.Location).Returns(path);
        _ = folder.Setup(value => value.ExistsAsync()).ReturnsAsync(value: true);
        _ = folder.Setup(value => value.GetFoldersAsync(It.IsAny<CancellationToken>())).Returns(() => EnumerateNavigationFolders(children, wait, entered));
        return folder.Object;
    }

    private static async IAsyncEnumerable<IFolder> EnumerateNavigationFolders(IReadOnlyList<IFolder> children, Task? wait, TaskCompletionSource? entered)
    {
        _ = entered?.TrySetResult();
        if (wait is not null)
        {
            await wait.ConfigureAwait(true);
        }

        foreach (var child in children)
        {
            yield return child;
        }
    }

    private static ContentBrowserAssetItem CreateNavigationAsset(string path, AssetKind kind)
        => new(
            new("asset://" + path),
            Path.GetFileName(path),
            kind,
            AssetState.Descriptor,
            DerivedState: null,
            AssetRuntimeAvailability.Unknown,
            path,
            path,
            path,
            CookedUri: null,
            CookedPath: null,
            AssetGuid: null,
            [],
            IsSelectable: true);
}
