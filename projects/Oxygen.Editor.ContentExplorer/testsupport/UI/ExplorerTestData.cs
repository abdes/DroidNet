// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using DroidNet.Storage;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ContentExplorer.TestSupport;

internal static class ExplorerTestData
{
    internal static Oxygen.Editor.ContentPipeline.Publication.CookPublicationService CreateNavigationPublication(ProjectContextService projects, IStorageProvider storage)
    {
        var files = new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
        return new(Mock.Of<Oxygen.Editor.ContentPipeline.IContentCookCoordinator>(), projects, files, new ProjectManagerService(storage, atomicFiles: files), new Oxygen.Editor.ContentPipeline.Testing.EngineContentPakFixture());
    }

    /// <summary>Creates an asset catalog whose only behavior is the record changes a test announces.</summary>
    /// <param name="changes">The catalog's changes; never emits when omitted.</param>
    /// <returns>The catalog.</returns>
    internal static Oxygen.Managed.Assets.Catalog.IAssetCatalog CreateNavigationCatalog(IObservable<Oxygen.Managed.Assets.Catalog.AssetChange>? changes = null)
    {
        var catalog = new Mock<Oxygen.Managed.Assets.Catalog.IAssetCatalog>();
        _ = catalog.SetupGet(value => value.Changes).Returns(changes ?? System.Reactive.Linq.Observable.Never<Oxygen.Managed.Assets.Catalog.AssetChange>());
        return catalog.Object;
    }

    /// <summary>Creates a folder source that never reports a folder change.</summary>
    /// <returns>The folder source.</returns>
    internal static Oxygen.Managed.Assets.Catalog.IAssetFolderSource CreateNavigationFolders()
        => Mock.Of<Oxygen.Managed.Assets.Catalog.IAssetFolderSource>(folders => folders.FolderChanges == System.Reactive.Linq.Observable.Never<AssetPath>());

    /// <summary>Creates a descriptor-backed asset row.</summary>
    /// <param name="path">The asset's identity, its virtual path without the authoring suffix.</param>
    /// <param name="kind">The asset kind.</param>
    /// <returns>The row.</returns>
    internal static AssetEntry CreateNavigationAsset(string path, AssetKind kind)
    {
        var identity = AssetPath.Parse(path);
        var file = FileSystemPath.From("C:/Query" + identity.VirtualPath);
        return new(identity, identity.Name, kind, AssetState.Descriptor, DerivedState: null, AssetRuntimeAvailability.Unknown, identity.VirtualPath, file, file, CookedPath: null, AssetGuid: null, [], IsSelectable: true);
    }

    internal static Mock<IAssetFeed> CreateQueryProvider(BehaviorSubject<IReadOnlyList<AssetEntry>> updates)
    {
        var provider = new Mock<IAssetFeed>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider;
    }

    internal static ProjectContextService CreateQueryProject(bool cookedMount = false)
    {
        var projects = new ProjectContextService();
        projects.Activate(TestProject.Create(new MockFileSystem(), "C:/Query").Context with
        {
            Name = "Query",
            AuthoringMounts = cookedMount ? [new("Content", "Content"), new("Cooked", ".cooked")] : [new("Content", "Content")],
        });
        return projects;
    }

    internal static AssetEntry CreateQueryAsset(string name, AssetKind kind, AssetCookFreshness freshness)
    {
        var folder = kind == AssetKind.Material ? "Materials" : "Geometry";
        var asset = CreateNavigationAsset("/Content/" + folder + "/" + name, kind);
        var current = freshness == AssetCookFreshness.Current;
        return asset with
        {
            DisplayName = name,
            CookStatus = new(asset.Identity, freshness, HasPublishedOutput: current, OutputAvailability: current ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [], []),
        };
    }
}
