// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Mvvm.Converters;
using DroidNet.Mvvm;
using DroidNet.Routing;
using DroidNet.Storage;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentBrowser.TestSupport;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.UI.Tests.Navigation;

[TestClass]
public sealed partial class BrowserNavigationTests : DroidNet.Tests.VisualUserInterfaceTests
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
        projects.Activate(new() { ProjectId = Guid.NewGuid(), Name = "Navigation", Category = Category.Games, ProjectRoot = "C:/Navigation", AuthoringMounts = [], LocalFolderMounts = [], Scenes = [], });
        var state = new ContentBrowserState(projects);
        var geometry = CreateNavigationAsset("/Content/Geometry/Cube.ogeo.json", AssetKind.Geometry);
        var material = CreateNavigationAsset("/Content/Materials/Blue.omat.json", AssetKind.Material);
        using var items = new BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>>([geometry, material]);
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(items);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        var hosting = CreateStatusHosting();
        var builtins = new Oxygen.Testing.BuiltinCatalogDiscoveryFixture();
        using AssetsLayoutViewModel layout = tiles ? new TilesLayoutViewModel(provider.Object, projects, state, hosting, builtins) : new ListLayoutViewModel(provider.Object, projects, state, hosting, builtins);
        RouteStateMapping.ApplyNavigationScope(state, "/(left:project//right:assets/list)?selected=%2FContent%2FGeometry");
        await layout.OnNavigatedToAsync(null!, null!).ConfigureAwait(true);
        UserControl view = tiles ? new TilesLayoutView
        {
            ViewModel = (TilesLayoutViewModel)layout
        }

        : new ListLayoutView
        {
            ViewModel = (ListLayoutViewModel)layout
        };
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
        projects.Activate(new() { ProjectId = Guid.NewGuid(), Name = "Navigation", Category = Category.Games, ProjectRoot = root.Location, AuthoringMounts = [new("Content", "Content") { IsExpanded = false }], LocalFolderMounts = [], Scenes = [], });
        var state = new ContentBrowserState(projects);
        using var model = new ProjectLayoutViewModel(projects, storage.Object, state, Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), new StrongReferenceMessenger(), CreateNavigationPublication(projects, storage.Object), NullLoggerFactory.Instance);
        var selected = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        model.PropertyChanged += OnSelectionChanged;
        try
        {
            await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
            state.SetSelectedFolders(["/Content/Slow/Leaf"]);
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            state.SetSelectedFolders(["/Content/Fast"]);
            release.SetResult();
            await selected.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
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
            model.PropertyChanged -= OnSelectionChanged;
            _ = release.TrySetResult();
        }

        void OnSelectionChanged(object? sender, PropertyChangedEventArgs args)
        {
            if (args.PropertyName == nameof(ProjectLayoutViewModel.SelectedItem)
                && model.SelectedItem?.Label == "Fast")
            {
                _ = selected.TrySetResult();
            }
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
        using var directory = new NavigationDirectory();
        var root = CreateNavigationFolder("Navigation", directory.Path, []);
        var content = CreateNavigationFolder("Content", directory.Path + "/Content", []);
        var cooked = CreateNavigationFolder("Cooked", directory.Path + "/.cooked", []);
        var library = CreateNavigationFolder("Library", "D:/Library", []);
        var storage = new Mock<IStorageProvider>();
        _ = storage.Setup(value => value.NormalizeRelativeTo(It.IsAny<string>(), It.IsAny<string>())).Returns((string parent, string child) => parent.TrimEnd('/') + "/" + child);
        foreach (var folder in new[]
        {
            root,
            content,
            cooked,
            library
        }

        )
        {
            _ = storage.Setup(value => value.GetFolderFromPathAsync(folder.Location, It.IsAny<CancellationToken>())).ReturnsAsync(folder);
        }

        var projects = new ProjectContextService();
        var project = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "Navigation",
            Category = Category.Games,
            ProjectRoot = root.Location,
            AuthoringMounts = [new("Content", "Content"), new("Cooked", ".cooked")],
            LocalFolderMounts = [new("Library", "D:/Library")],
            Scenes = [],
            DefaultSceneId = Guid.NewGuid(),
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
        using var model = new ProjectLayoutViewModel(projects, storage.Object, state, Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), messenger, CreateNavigationPublication(projects, storage.Object), NullLoggerFactory.Instance);
        await model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!).ConfigureAwait(true);
        var tree = model.ShownItems.OfType<ProjectRootTreeItemAdapter>().Single();
        var renamed = tree.VirtualFolderMounts.Single(mount => string.Equals(mount.MountPointName, "Library", StringComparison.Ordinal));
        state.SetSelectedFolders(["/Library"]);
        renamed.Label = "LibraryRenamed";
        await model.PendingMountChange.ConfigureAwait(true);
        _ = saved.Should().NotBeNull();
        _ = saved!.AuthoringMounts.Should().Contain(mount => mount.Name == "Content" && mount.RelativePath == "Content");
        _ = saved.DefaultSceneId.Should().Be(project.DefaultSceneId);
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

    /// <summary>Publication notifications during preparation cannot install a closed or superseded project's tree.</summary>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(true, true)]
    public Task TreePreparationRejectsOldProjectAfterPublicationNotification(bool close, bool failObsoleteRead) => EnqueueAsync(async () =>
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
        var first = new ProjectContext
        {
            ProjectId = Guid.NewGuid(),
            Name = "First",
            Category = Category.Games,
            ProjectRoot = firstDirectory.Path,
            AuthoringMounts = [new("Cooked", ".cooked")],
            LocalFolderMounts = [],
            Scenes = [],
        };
        projects.Activate(first);
        var messenger = new StrongReferenceMessenger();
        using var model = new ProjectLayoutViewModel(projects, storage.Object, new ContentBrowserState(projects), Mock.Of<IDialogService>(), new ViewModelToView(Mock.Of<IViewLocator>()), messenger, CreateNavigationPublication(projects, storage.Object), NullLoggerFactory.Instance);
        var load = model.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), null!);
        try
        {
            await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(true);
            messenger.Send(new AssetsChangedMessage());
            if (close)
            {
                projects.Close();
                model.Dispose();
            }
            else
            {
                projects.Activate(first with { ProjectId = Guid.NewGuid(), Name = "Next", ProjectRoot = nextDirectory.Path });
                messenger.Send(new AssetsChangedMessage());
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
        public string Path => this.directory.FullName.Replace('\\', '/');

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
