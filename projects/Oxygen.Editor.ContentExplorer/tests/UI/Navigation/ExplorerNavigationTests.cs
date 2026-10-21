// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DroidNet.Storage;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions;
using Testably.Abstractions.Testing;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.UI.Tests.Navigation;

[TestClass]
public sealed partial class ExplorerNavigationTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A reused list/tiles model follows URL scope immediately and rejects delayed selection/invocation from the old folder.</summary>
    /// <param name="tiles">Whether to exercise the tile projection.</param>
    /// <returns>The asynchronous scope regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ReusedLayoutDropsOldFolderRowsAndActionTargets(bool tiles) => EnqueueAsync(async () =>
    {
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem(), "C:/Navigation").Context with { Name = "Navigation", AuthoringMounts = [] });
        var state = new ContentExplorerState();
        var geometry = CreateNavigationAsset("/Content/Geometry/Cube.ogeo", AssetKind.Geometry);
        var material = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        using var items = new BehaviorSubject<IReadOnlyList<AssetEntry>>([geometry, material]);
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(items);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var hosting = CreateStatusHosting();
        using AssetsLayoutViewModel layout = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, hosting) : new ListLayoutViewModel(provider.Object, projects, state, hosting);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:folders//right:assets/list)?selected=%2FContent%2FGeometry");
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)layout,
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)layout,
        };
        await LoadTestContentAsync(view).ConfigureAwait(true);
        await WaitForRenderAsync().ConfigureAwait(true);
        var selector = view.FindDescendant<ListViewBase>()!;
        selector.SelectedIndex = 0;
        _ = layout.SelectedAsset!.Identity.Should().Be(geometry.Identity);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:folders//right:assets/list)?selected=%2FContent%2FMaterials");
        _ = layout.Assets.Should().ContainSingle().Which.Item.Identity.Should().Be(material.Identity);
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
        _ = layout.Assets.Should().ContainSingle().Which.Item.Identity.Should().Be(material.Identity);
    });

    /// <summary>A delayed folder lookup cannot replace a newer folder selection when it completes.</summary>
    /// <returns>The asynchronous real-tree navigation regression.</returns>
    [TestMethod]
    public Task SlowFolderLookupCannotRestoreObsoleteTreeSelection() => EnqueueAsync(async () =>
    {
        using var directory = new NavigationDirectory();
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var leaf = CreateNavigationFolder("Leaf", directory.Path + "/Content/Slow/Leaf", []);
        var slow = CreateNavigationFolder("Slow", directory.Path + "/Content/Slow", [leaf], release.Task, entered);
        var fast = CreateNavigationFolder("Fast", directory.Path + "/Content/Fast", []);
        var content = CreateNavigationFolder("Content", directory.Path + "/Content", [fast, slow]);
        var root = CreateNavigationFolder("Navigation", directory.Path, [content]);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(root.Location, It.IsAny<CancellationToken>())).ReturnsAsync(root);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(content.Location, It.IsAny<CancellationToken>())).ReturnsAsync(content);
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new RealFileSystem(), root.Location).Context with { Name = "Navigation", AuthoringMounts = [new("Content", "Content") { IsExpanded = false }] });
        var state = new ContentExplorerState();
        using var model = new ContentFoldersViewModel(projects, storage.Object, state, Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), Mock.Of<IContentMountChanges>(), new ContentFoldersPane(), CreateNavigationPublication(projects, storage.Object), CreateNavigationCatalog(), CreateNavigationFolders(), CreateStatusHosting(), new Oxygen.Editor.ContentPipeline.Testing.EngineContentPakFixture(), NullLoggerFactory.Instance, relocationService: Mock.Of<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>());
        var selected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        model.PropertyChanged += OnSelectionChanged;
        try
        {
            await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
            state.SetSelectedFolders([AssetPath.Parse("/Content/Slow/Leaf")]);
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            state.SetSelectedFolders([AssetPath.Parse("/Content/Fast")]);
            release.SetResult();
            await selected.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            _ = state.SelectedFolders.Should().Equal(AssetPath.Parse("/Content/Fast"));
            var tree = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
            var mount = (await tree.Children.ConfigureAwait(true)).OfType<AuthoringMountPointTreeItemAdapter>().Single();
            var folders = (await mount.Children.ConfigureAwait(true)).OfType<FolderTreeItemAdapter>().ToArray();
            _ = folders.Single(folder => string.Equals(folder.Label, "Fast", StringComparison.Ordinal)).IsSelected.Should().BeTrue();
            var slowAdapter = folders.Single(folder => string.Equals(folder.Label, "Slow", StringComparison.Ordinal));
            _ = (await slowAdapter.Children.ConfigureAwait(true)).Should().OnlyContain(item => !item.IsSelected);
        }
        finally
        {
            model.PropertyChanged -= OnSelectionChanged;
            _ = release.TrySetResult();
        }

        void OnSelectionChanged(object? sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == nameof(ContentFoldersViewModel.SelectedItem)
                && model.SelectedItem?.Label == "Fast")
            {
                _ = selected.TrySetResult();
            }
        }
    });

    /// <summary>
    /// A folder that no longer exists, deleted since it was selected or reached again through Back or Forward, is
    /// replaced by its nearest folder that still exists.
    /// </summary>
    /// <returns>The asynchronous missing-folder regression.</returns>
    [TestMethod]
    public Task MissingSelectedFolderShowsItsNearestExistingFolder() => EnqueueAsync(async () =>
    {
        using var directory = new NavigationDirectory();
        var props = CreateNavigationFolder("Props", directory.Path + "/Content/Props", []);
        var content = CreateNavigationFolder("Content", directory.Path + "/Content", [props]);
        var root = CreateNavigationFolder("Navigation", directory.Path, [content]);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(root.Location, It.IsAny<CancellationToken>())).ReturnsAsync(root);
        _ = storage.Setup(value => value.GetFolderFromPathAsync(content.Location, It.IsAny<CancellationToken>())).ReturnsAsync(content);
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new RealFileSystem(), root.Location).Context with { Name = "Navigation", AuthoringMounts = [new("Content", "Content") { IsExpanded = false }] });
        var state = new ContentExplorerState();
        using var model = new ContentFoldersViewModel(projects, storage.Object, state, Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), Mock.Of<IContentMountChanges>(), new ContentFoldersPane(), CreateNavigationPublication(projects, storage.Object), CreateNavigationCatalog(), CreateNavigationFolders(), CreateStatusHosting(), new Oxygen.Editor.ContentPipeline.Testing.EngineContentPakFixture(), NullLoggerFactory.Instance, relocationService: Mock.Of<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>());
        var shown = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        model.PropertyChanged += OnSelectionChanged;
        try
        {
            await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
            state.SetSelectedFolders([AssetPath.Parse("/Content/Props/Deleted folder")]);
            await shown.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);

            _ = state.SelectedFolders.Should().Equal(AssetPath.Parse("/Content/Props"));
        }
        finally
        {
            model.PropertyChanged -= OnSelectionChanged;
        }

        void OnSelectionChanged(object? sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == nameof(ContentFoldersViewModel.SelectedItem) && model.SelectedItem?.Label == "Props")
            {
                _ = shown.TrySetResult();
            }
        }
    });

    /// <summary>A confirmed rename applies immediately; a failed change restores the accepted tree.</summary>
    /// <param name="succeeds">Whether project persistence accepts the save.</param>
    /// <returns>The asynchronous mount-persistence regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task ExplorerMountSavePreservesOriginsAndFailureState(bool succeeds) => EnqueueAsync(async () =>
    {
        using var directory = new NavigationDirectory();
        var root = CreateNavigationFolder("Navigation", directory.Path, []);
        var content = CreateNavigationFolder("Content", directory.Path + "/Content", []);
        var cooked = CreateNavigationFolder("Cooked", directory.Path + "/.cooked", []);
        var libraryRoot = FileSystemPath.From("D:/Library");
        var library = CreateNavigationFolder("Library", libraryRoot.FullPath, []);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        foreach (var folder in new[]
        {
            root,
            content,
            cooked,
            library,
        }

        )
        {
            _ = storage.Setup(value => value.GetFolderFromPathAsync(folder.Location, It.IsAny<CancellationToken>())).ReturnsAsync(folder);
        }

        var projects = new ProjectContextService();
        var fileSystem = new RealFileSystem();
        var project = TestProject.Create(fileSystem, root.Location).Context with
        {
            Name = "Navigation",
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
            LocalFolderMounts = [new("Library", libraryRoot)],
            DefaultSceneId = SceneId.New(),
        };
        projects.Activate(project);
        ProjectInfo? saved = null;
        var mountChanges = new Mock<IContentMountChanges>();
        _ = mountChanges.Setup(value => value.ChangeMountsAsync(It.IsAny<ProjectContext>(), It.IsAny<ProjectInfo>(), It.IsAny<CancellationToken>()))
            .Callback((ProjectContext _, ProjectInfo candidate, CancellationToken _) =>
            {
                saved = candidate;
                if (succeeds)
                {
                    projects.Activate(ProjectContext.FromProjectInfo(candidate, fileSystem));
                }
            })
            .ReturnsAsync(succeeds);
        var state = new ContentExplorerState();
        using var model = new ContentFoldersViewModel(projects, storage.Object, state, Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), mountChanges.Object, new ContentFoldersPane(), CreateNavigationPublication(projects, storage.Object), CreateNavigationCatalog(), CreateNavigationFolders(), CreateStatusHosting(), new Oxygen.Editor.ContentPipeline.Testing.EngineContentPakFixture(), NullLoggerFactory.Instance, relocationService: Mock.Of<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>());
        await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
        var tree = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        var renamed = tree.VirtualFolderMounts.Single(mount => string.Equals(mount.MountPointName, "Library", StringComparison.Ordinal));
        state.SetSelectedFolders([AssetPath.Parse("/Library")]);
        renamed.Label = "LibraryRenamed";
        await model.PendingMountChange.ConfigureAwait(true);
        _ = saved.Should().NotBeNull();
        _ = saved!.AuthoringMounts.Should().Contain(mount => mount.Name == "Content" && mount.RelativePath == "Content");
        _ = saved.DefaultSceneId.Should().Be(project.DefaultSceneId);
        _ = saved.AuthoringMounts.Should().Contain(mount => mount.Name == "Cooked" && mount.RelativePath == ".cooked");
        _ = saved.LocalFolderMounts.Should().Contain(mount => mount.Name == "LibraryRenamed" && mount.Root == libraryRoot);
        _ = model.HasUnsavedChanges.Should().BeFalse();
        _ = state.SelectedFolders.Should().Equal(AssetPath.Parse(succeeds ? "/LibraryRenamed" : "/Library"));
        var restored = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        _ = restored.VirtualFolderMounts.Should().Contain(mount => mount.MountPointName == (succeeds ? "LibraryRenamed" : "Library"));
        if (!succeeds)
        {
            _ = projects.ActiveProject.Should().BeSameAs(project);
        }
    });

    /// <summary>Catalog changes during preparation cannot install a closed or superseded project's tree.</summary>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(true, true)]
    public Task TreePreparationRejectsOldProjectAfterCatalogChange(bool close, bool failObsoleteRead) => EnqueueAsync(async () =>
    {
        using var firstDirectory = new NavigationDirectory();
        using var nextDirectory = new NavigationDirectory();
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var release = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var storage = new Mock<IStorageProvider>();
        storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string root, string path) => Path.Combine(root, path));
        storage.Setup(value => value.GetFolderFromPathAsync(It.IsAny<string>(), It.IsAny<CancellationToken>())).Returns(async (string path, CancellationToken token) =>
        {
            if (string.Equals(path, firstDirectory.Path, StringComparison.Ordinal))
            {
                entered.TrySetResult();
                await release.Task.WaitAsync(token).ConfigureAwait(true);
                if (failObsoleteRead)
                {
                    throw new IOException("The obsolete project folder is unavailable.");
                }
            }

            return CreateNavigationFolder(Path.GetFileName(path), path, []);
        });
        var projects = new ProjectContextService();
        var first = TestProject.Create(new RealFileSystem(), firstDirectory.Path).Context with
        {
            Name = "First",
            AuthoringMounts = [new("Cooked", ".cooked")],
        };
        projects.Activate(first);
        using var changes = new System.Reactive.Subjects.Subject<Oxygen.Managed.Assets.Catalog.AssetChange>();
        using var model = new ContentFoldersViewModel(projects, storage.Object, new ContentExplorerState(), Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), Mock.Of<IContentMountChanges>(), new ContentFoldersPane(), CreateNavigationPublication(projects, storage.Object), CreateNavigationCatalog(changes), CreateNavigationFolders(), CreateStatusHosting(), new Oxygen.Editor.ContentPipeline.Testing.EngineContentPakFixture(), NullLoggerFactory.Instance, relocationService: Mock.Of<Oxygen.Editor.ContentPipeline.Relocation.IAssetRelocationService>());
        var load = model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            changes.OnNext(new(Oxygen.Managed.Assets.Catalog.AssetChangeKind.Added, AssetPath.Parse("/Cooked/Published.omat")));
            if (close)
            {
                projects.Close();
                model.Dispose();
            }
            else
            {
                projects.Activate(first with { ProjectId = Guid.NewGuid(), Name = "Next", ProjectRoot = FileSystemPath.From(nextDirectory.Path) });
                changes.OnNext(new(Oxygen.Managed.Assets.Catalog.AssetChangeKind.Added, AssetPath.Parse("/Cooked/Published.omat")));
            }
        }
        finally
        {
            release.TrySetResult();
        }

        await load.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
        if (close)
        {
            _ = model.ShownItems.Should().BeEmpty();
        }
        else
        {
            _ = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Should().ContainSingle().Which.ProjectRootFolder.Location.Should().Be(nextDirectory.Path);
        }
    });

    private sealed class NavigationDirectory : IDisposable
    {
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-Navigation-");

        public string Path => this.directory.FullName;

        public void Dispose() => this.directory.Delete(recursive: true);
    }

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
}
