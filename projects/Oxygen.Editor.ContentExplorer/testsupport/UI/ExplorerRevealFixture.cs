// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Subjects;
using AwesomeAssertions;
using DroidNet.Aura.Dialogs;
using DroidNet.Aura.Windowing;
using DroidNet.Composition;
using DroidNet.Routing;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentExplorer.Panes.Assets.Layouts;
using Oxygen.Editor.ContentExplorer.Panes.ContentFolders;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Shell.Desktop;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentExplorer.TestSupport.ExplorerTestData;

namespace Oxygen.Editor.ContentExplorer.TestSupport;

internal sealed partial class ExplorerRevealFixture : IDisposable
{
    public Mock<IContentPipelineService> HistoryPipeline { get; } = new();

    public AssetEntry[] ConfigureHistoryJourney()
    {
        foreach (var folder in new[]
        {
            "Materials",
            "Geometry",
            "Scenes",
        }

        )
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content", folder));
        }

        var rows = Enumerable.Range(0, 1000).Select(index =>
        {
            var (folder, suffix, kind) = (index % 3) switch
            {
                0 => ("Materials", ".omat", AssetKind.Material),
                1 => ("Geometry", ".ogeo", AssetKind.Geometry),
                _ => ("Scenes", ".oscene", AssetKind.Scene),
            };
            return CreateNavigationAsset(string.Create(CultureInfo.InvariantCulture, $"/Content/{folder}/Item{index:D4}{suffix}"), kind);
        }).ToArray();
        this.items.OnNext(rows);
        _ = this.HistoryPipeline.Setup(value => value.CookAssetAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>(), It.IsAny<ProjectContext?>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        _ = this.HistoryPipeline.Setup(value => value.CookFolderAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Folder, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        this.container.Unregister<IContentPipelineService>();
        this.container.RegisterInstance(this.HistoryPipeline.Object);
        return rows;
    }

    public void PublishHistoryRows(IReadOnlyList<AssetEntry> rows) => this.items.OnNext(rows);

    public async Task<(string physical, string expected)> ConfigurePhysicalNavigationAsync(string mount, CancellationToken token)
    {
        if (string.Equals(mount, "Library", StringComparison.Ordinal))
        {
            _ = this.SetLibraryOutput();
            return ("Library/Materials", "Library/Materials");
        }

        if (string.Equals(mount, "Cooked", StringComparison.Ordinal))
        {
            var cooked = CreateNavigationAsset("/Content/Materials/Red.omat", AssetKind.Material) with
            {
                SourcePath = null,
                DescriptorPath = null,
                PrimaryState = AssetState.Cooked,
            };
            await this.SetCookedOutputsAsync([cooked], token).ConfigureAwait(true);
            return (Path.GetRelativePath(this.directory.FullName, Path.Combine(this.publishedOutputRoot!, "Materials")), "Published/Content/Materials");
        }

        this.Session.ChangeContext(this.Session.Context with { AuthoringMounts = [new("Game", "Content")] });
        return ("Content/Materials", "Game/Materials");
    }

    public async Task NavigateHistoryFolderAsync(string folder, CancellationToken cancellationToken, bool relativeToContent = true)
    {
        var path = relativeToContent ? Path.Combine(this.directory.FullName, "Content", folder) : Path.Combine(this.directory.FullName, folder);
        var location = await this.container.Resolve<DroidNet.Storage.IStorageProvider>().GetFolderFromPathAsync(path, cancellationToken).ConfigureAwait(true);
        await this.Folders.NavigateToFolderAsync(location).ConfigureAwait(true);
    }

    private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("Oxygen-ExplorerReveal-");
    private readonly RealFileSystem fileSystem = new();
    private readonly IContainer container = HostContainer.Create();
    private readonly ScopeHandle scopeRoot;
    private readonly BehaviorSubject<IReadOnlyList<AssetEntry>> items;
    private readonly Mock<ILogger> logger = new();
    private readonly FakeCookPublicationReader publications;

    public ExplorerRevealFixture(bool persisted = false, string cookedAlias = "Cooked")
    {
        _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content", "Materials"));
        _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, ".cooked", "Content", "Materials"));
        this.Material = CreateNavigationAsset("/Content/Materials/Blue.omat", AssetKind.Material);
        this.items = new([this.Material]);
        var provider = CreateQueryProvider(this.items);
        this.Provider = provider;
        _ = provider.Setup(value => value.ResolveAsync(this.Material.Identity, It.IsAny<CancellationToken>())).ReturnsAsync(this.Material);
        var info = new ProjectInfo("Explorer", Category.Games, this.directory.FullName)
        {
            AuthoringMounts = [new("Content", "Content")],
        };
        if (persisted)
        {
            info.AuthoringMounts.Add(new(cookedAlias, ".cooked"));
        }

        this.Session = new(ProjectContext.FromProjectInfo(info, this.fileSystem));
        this.publications = new(this.Session.Context.ProjectId, this.Session.Context.ProjectRoot);
        var mountChanges = new Mock<IContentMountChanges>();
        _ = mountChanges.Setup(value => value.ChangeMountsAsync(It.IsAny<ProjectContext>(), It.IsAny<ProjectInfo>(), It.IsAny<CancellationToken>()))
            .Callback((ProjectContext _, ProjectInfo candidate, CancellationToken _) =>
            {
                this.MountChanges++;
                this.Session.ChangeContext(ProjectContext.FromProjectInfo(candidate, this.fileSystem));
            })
            .ReturnsAsync(true);
        this.container.RegisterInstance<IProjectSession>(this.Session);
        var storage = new NativeStorageProvider(new RealFileSystem());
        this.container.RegisterInstance<IStorageProvider>(storage);
        this.container.RegisterInstance<ICookPublicationReader>(this.publications);
        this.container.RegisterInstance(mountChanges.Object);
        this.container.RegisterInstance(provider.Object);
        var hosting = CreateStatusHosting();
        this.container.RegisterInstance(hosting);
        _ = this.logger.Setup(value => value.IsEnabled(It.IsAny<LogLevel>())).Returns(value: true);
        var logging = new Mock<ILoggerFactory>();
        _ = logging.Setup(value => value.CreateLogger(It.IsAny<string>())).Returns(this.logger.Object);
        this.container.RegisterInstance<ILoggerFactory>(logging.Object);
        this.container.RegisterInstance(this.Dialogs.Object);
        this.container.RegisterInstance(Mock.Of<IAssetCatalog>(catalog => catalog.Changes == System.Reactive.Linq.Observable.Never<AssetChange>()));
        this.container.RegisterInstance(Mock.Of<IAssetFolderSource>(folders => folders.FolderChanges == System.Reactive.Linq.Observable.Never<AssetPath>()));
        this.container.RegisterInstance(Mock.Of<IAutomaticCookService>());
        this.container.RegisterInstance(Mock.Of<IProjectManagerService>());
        this.container.RegisterInstance(Mock.Of<IContentPipelineService>());
        this.container.RegisterInstance<IEngineContentPak>(new EngineContentPakFixture());
        this.container.RegisterInstance(Mock.Of<IOperationResultPublisher>());
        this.container.RegisterInstance(Mock.Of<IStatusReducer>());
        this.container.RegisterInstance(Mock.Of<IWindowManagerService>());
        this.container.RegisterInstance(new Oxygen.Editor.Textures.TextureImport(new RealFileSystem()));
        this.container.RegisterInstance(Mock.Of<Oxygen.Editor.Models.IModelImportPrompt>());
        this.container.RegisterInstance(Mock.Of<IDesktopIntegration>());

        // The creation workflow the explorer builds over the window's services; without a relocation service the
        // explorer renames folders itself.
        this.container.RegisterInstance(Mock.Of<IAssetAuthoring>());
        this.container.RegisterInstance(Mock.Of<ICodeEditorService>());
        // The fixture's container stands in for the explorer's window.
        this.scopeRoot = ScopeFactory.CreateRoot(this.container);
        this.Explorer = new(
            new ScopeFactory(),
            this.scopeRoot,
            Mock.Of<IRouter>(),
            hosting,
            this.Session,
            provider.Object,
            new ContentExplorerAssetReveal(Mock.Of<IPlacementService>()),
            new ContentExplorerScriptCreation(),
            Mock.Of<IProjectUsageService>(), Mock.Of<IOperationResultPublisher>(), Mock.Of<IStatusReducer>(), logging.Object);
    }

    public TestSession Session { get; }

    public AssetEntry Material { get; }

    public ContentExplorerViewModel Explorer { get; }

    public Mock<IAssetFeed> Provider { get; }

    public Mock<IDialogService> Dialogs { get; } = new();

    public string Diagnostics => string.Join(Environment.NewLine, this.logger.Invocations.Where(static call => string.Equals(call.Method.Name, "Log", StringComparison.Ordinal)).Select(static call => call.Arguments[2]?.ToString() + " " + call.Arguments[3]?.ToString()));

    public ContentFoldersViewModel Folders => (ContentFoldersViewModel)this.Explorer.LeftPaneViewModel!;

    public AssetsLayoutViewModel Layout => (AssetsLayoutViewModel)((AssetsViewModel)this.Explorer.RightPaneViewModel!).LayoutViewModel!;

    public int MountChanges { get; private set; }

    private string? publishedOutputRoot;

    /// <summary>Publishes a project output that holds <paramref name="outputs"/>, in a generation folder of its own.</summary>
    /// <param name="outputs">The cooked products, all under <c>/Content</c>.</param>
    /// <param name="token">Cancels the publication.</param>
    /// <returns>A task that completes once the outputs are published and listed.</returns>
    public Task SetCookedOutputsAsync(IReadOnlyList<AssetEntry> outputs, CancellationToken token)
    {
        token.ThrowIfCancellationRequested();
        var root = Path.Combine(this.directory.FullName, ".cooked", "generations", Guid.CreateVersion7().ToString("N"));
        foreach (var item in outputs)
        {
            _ = Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(root, item.Identity.VirtualPath["/Content/".Length..]))!);
        }

        this.publications.Publish([], new Dictionary<string, FileSystemPath>(StringComparer.Ordinal) { ["Content"] = FileSystemPath.From(root) }, outputs.Select(static item => item.Identity.VirtualPath));
        this.publishedOutputRoot = root;
        this.items.OnNext(outputs);
        return Task.CompletedTask;
    }

    public AssetEntry SetLibraryOutput()
    {
        var root = Path.Combine(this.directory.FullName, "Library");
        _ = Directory.CreateDirectory(Path.Combine(root, "Materials"));
        File.WriteAllBytes(Path.Combine(root, "Materials/Shared.omat"), [1]);
        File.WriteAllBytes(Path.Combine(root, "container.index.bin"), [1]);
        var item = CreateNavigationAsset("/Art/Shared.omat", AssetKind.Material) with
        {
            SourcePath = null,
            DescriptorPath = null,
            PrimaryState = AssetState.Cooked,
            CookedMetadata = new(FileSystemPath.From(root), "Materials/Shared.omat", Guid.NewGuid(), new(1, 2), 1, 1, new string('0', 64))
            {
                VirtualPath = AssetPath.Parse("/Art/Shared.omat"),
            },
        };
        this.Session.ChangeContext(this.Session.Context with { LocalFolderMounts = [new("Library", FileSystemPath.From(root))] });
        _ = this.Provider.Setup(provider => provider.ResolveAsync(item.Identity, It.IsAny<CancellationToken>())).ReturnsAsync(item);
        this.items.OnNext([item]);
        return item;
    }

    public async Task OpenAsync()
    {
        await this.Explorer.OnNavigatedToAsync(Mock.Of<IActiveRoute>(), Mock.Of<INavigationContext>(value => value.NavigationTarget == new object())).ConfigureAwait(true);
        _ = this.Explorer.LeftPaneViewModel.Should().NotBeNull(this.Diagnostics);
    }

    public void Dispose()
    {
        this.Explorer.Dispose();
        this.scopeRoot.DisposeAsync().AsTask().GetAwaiter().GetResult();
        this.container.Dispose();
        this.Session.Dispose();
        this.items.Dispose();
        this.directory.Delete(recursive: true);
    }
}
