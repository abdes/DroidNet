// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Globalization;
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
using static DroidNet.Tests.UiTestHosting;
using static Oxygen.Editor.ContentBrowser.TestSupport.BrowserTestData;

namespace Oxygen.Editor.ContentBrowser.TestSupport;

internal sealed partial class BrowserRevealFixture : IDisposable
{
    public Mock<IContentPipelineService> HistoryPipeline { get; } = new();

    public ContentBrowserAssetItem[] ConfigureHistoryJourney()
    {
        foreach (var folder in new[]
        {
            "Materials",
            "Geometry",
            "Scenes"
        }

        )
        {
            _ = Directory.CreateDirectory(Path.Combine(this.directory.FullName, "Content", folder));
        }

        var rows = Enumerable.Range(0, 1000).Select(index =>
        {
            var (folder, suffix, kind) = (index % 3) switch
            {
                0 => ("Materials", ".omat.json", AssetKind.Material),
                1 => ("Geometry", ".ogeo.json", AssetKind.Geometry),
                _ => ("Scenes", ".oscene.json", AssetKind.Scene),
            };
            return CreateNavigationAsset(string.Create(CultureInfo.InvariantCulture, $"/Content/{folder}/Item{index:D4}{suffix}"), kind);
        }).ToArray();
        this.items.OnNext(rows);
        _ = this.HistoryPipeline.Setup(value => value.CookAssetAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>(), It.IsAny<ProjectContext?>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Asset, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        _ = this.HistoryPipeline.Setup(value => value.CookFolderAsync(It.IsAny<Uri>(), It.IsAny<CancellationToken>())).ReturnsAsync(new ContentCookResult(Guid.NewGuid(), CookTargetKind.Folder, OperationStatus.Succeeded, [], [], Inspection: null, Validation: null));
        this.container.Unregister<IContentPipelineService>();
        this.container.RegisterInstance(this.HistoryPipeline.Object);
        return rows;
    }

    public void PublishHistoryRows(IReadOnlyList<ContentBrowserAssetItem> rows) => this.items.OnNext(rows);
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
                CookedUri = new("asset:///Content/Materials/Red.omat"),
                PrimaryState = AssetState.Cooked,
            };
            await this.SetCookedOutputsAsync([cooked], token).ConfigureAwait(true);
            return (Path.GetRelativePath(this.directory.FullName, Path.Combine(this.publishedOutputRoot!, "Materials")), "Published/Content/Materials");
        }

        this.Projects.Activate(this.Projects.ActiveProject! with { AuthoringMounts = [new("Game", "Content")] });
        return ("Content/Materials", "Game/Materials");
    }

    public async Task NavigateHistoryFolderAsync(string folder, CancellationToken cancellationToken, bool relativeToContent = true)
    {
        var path = relativeToContent ? Path.Combine(this.directory.FullName, "Content", folder) : Path.Combine(this.directory.FullName, folder);
        var location = await this.container.Resolve<DroidNet.Storage.IStorageProvider>().GetFolderFromPathAsync(path, cancellationToken).ConfigureAwait(true);
        await this.Explorer.NavigateToFolderAsync(location).ConfigureAwait(true);
    }

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
        var info = new ProjectInfo("Browser", Category.Games, this.directory.FullName)
        {
            AuthoringMounts = [new("Content", "Content")]
        };
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
        var storage = new NativeStorageProvider(new RealFileSystem());
        this.container.RegisterInstance<IStorageProvider>(storage);
        this.container.RegisterInstance(CreateNavigationPublication(this.Projects, storage));
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
    public string Diagnostics => string.Join(Environment.NewLine, this.logger.Invocations.Where(static call => string.Equals(call.Method.Name, "Log", StringComparison.Ordinal)).Select(static call => call.Arguments[2]?.ToString() + " " + call.Arguments[3]?.ToString()));
    public ProjectLayoutViewModel Explorer => (ProjectLayoutViewModel)this.Browser.LeftPaneViewModel!;
    public AssetsLayoutViewModel Layout => (AssetsLayoutViewModel)((AssetsViewModel)this.Browser.RightPaneViewModel!).LayoutViewModel!;
    public int MountChanges { get; private set; }

    private string? publishedOutputRoot;
    public async Task SetCookedOutputsAsync(IReadOnlyList<ContentBrowserAssetItem> outputs, CancellationToken token)
    {
        var project = this.Projects.ActiveProject!;
        var key = Guid.CreateVersion7();
        var root = CookPublicationPaths.Generation(project.ProjectRoot, key);
        Directory.CreateDirectory(root);
        await File.WriteAllBytesAsync(Path.Combine(root, CookedGeneration.MarkerFileName), [], token).ConfigureAwait(true);
        var digest = new string('0', 64);
        var products = outputs.Select(item =>
        {
            var path = Uri.UnescapeDataString(item.IdentityUri.AbsolutePath);
            var descriptor = path["/Content/".Length..];
            Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(root, descriptor))!);
            var kind = item.Kind == AssetKind.Geometry ? ContentCookAssetKind.Geometry : ContentCookAssetKind.Material;
            var sourceUri = new Uri(item.IdentityUri.AbsoluteUri + ".json");
            var sourceRelativePath = path.TrimStart('/') + ".json";
            var sourcePath = Path.Combine(project.ProjectRoot, sourceRelativePath);
            return new CookProvenance.Product(sourceUri, digest, [], [new(new(sourceUri, item.IdentityUri, kind, "Content", path) { DescriptorRelativePath = descriptor }, "Content")])
            {
                ReuseFingerprint = digest,
                SourceInput = new(sourceUri, kind, "Content", sourceRelativePath, sourcePath, path, ContentCookInputRole.Primary),
                SourceFiles = [new(sourceUri, sourcePath, sourceRelativePath, digest)],
                DeclaredOutputs = [new(path, kind == ContentCookAssetKind.Geometry ? "geometry" : "material", string.Empty, Required: true)],
            };
        }).ToImmutableArray();
        var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, Guid.NewGuid(), DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(project), [new(CookPublicationRootOwner.Project, "Content", key, digest, null)], products, new("logical-folder-fixture", digest, [], [], [], []));
        document.Validate(project);
        var files = new NativeAtomicFileStore(new RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(project.ProjectRoot, token).ConfigureAwait(true);
        var documentPath = CookPublicationPaths.Document(project.ProjectRoot, document.OperationId);
        Directory.CreateDirectory(Path.GetDirectoryName(documentPath)!);
        var version = await files.WriteAsync(documentPath, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), FileVersion.Missing, token).ConfigureAwait(true);
        var headPath = CookPublicationPaths.Head(project.ProjectRoot);
        var previous = await files.ReadAsync(headPath, token).ConfigureAwait(true);
        var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
        _ = await files.WriteAsync(headPath, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), previous.Version, token).ConfigureAwait(true);
        this.publishedOutputRoot = root;
        this.items.OnNext(outputs);
    }

    public ContentBrowserAssetItem SetLibraryOutput()
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
            CookedUri = new("asset:///Art/Shared.omat"),
            CookedMetadata = new(root, "Materials/Shared.omat", Guid.NewGuid(), new(1, 2), 1, 1, new string('0', 64))
            {
                VirtualPath = "/Art/Shared.omat"
            },
        };
        this.Projects.Activate(this.Projects.ActiveProject! with { LocalFolderMounts = [new("Library", root)] });
        _ = this.Provider.Setup(provider => provider.ResolveAsync(item.IdentityUri, It.IsAny<CancellationToken>())).ReturnsAsync(item);
        this.items.OnNext([item]);
        return item;
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
