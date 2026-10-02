// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Subjects;
using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using CommunityToolkit.WinUI;
using DroidNet.Aura.Dialogs;
using DroidNet.Controls;
using DroidNet.Hosting.WinUI;
using DroidNet.Mvvm.Converters;
using DroidNet.Mvvm;
using DroidNet.Routing;
using DroidNet.Storage;
using DroidNet.Tests;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Markup;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentBrowser.Messages;
using Oxygen.Editor.ContentBrowser.Panes.Assets.Layouts;
using Oxygen.Editor.ContentBrowser.Panes.Assets;
using Oxygen.Editor.ContentBrowser.ProjectExplorer;
using Oxygen.Editor.ContentBrowser.Shell;
using Oxygen.Editor.ContentBrowser.TestSupport;
using Oxygen.Editor.ContentBrowser;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentBrowser.TestSupport;

internal static class BrowserTestData
{
    internal static ContentBrowserAssetItem CreateStatusAsset(int index)
    {
        var name = string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Material {index:00}");
        var uri = new Uri("asset:///Content/" + Uri.EscapeDataString(name) + ".omat.json");
        return new(uri, name, AssetKind.Material, AssetState.Descriptor, AssetState.Cooked, AssetRuntimeAvailability.Unknown, uri.AbsolutePath, uri.AbsolutePath, uri.AbsolutePath, CookedUri: null, CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true)
        {
            CookStatus = new(uri, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [], [], []),
        };
    }

    internal static Oxygen.Editor.ContentPipeline.Publication.CookPublicationService CreateNavigationPublication(ProjectContextService projects, IStorageProvider storage)
    {
        var files = new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
        return new(Mock.Of<Oxygen.Editor.ContentPipeline.IContentCookCoordinator>(), projects, files, new ProjectManagerService(storage, atomicFiles: files));
    }

    internal static ContentBrowserAssetItem CreateNavigationAsset(string path, AssetKind kind) => new(new("asset://" + path), Path.GetFileName(path), kind, AssetState.Descriptor, DerivedState: null, AssetRuntimeAvailability.Unknown, path, path, path, CookedUri: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true);

    internal static Mock<IContentBrowserAssetProvider> CreateQueryProvider(BehaviorSubject<IReadOnlyList<ContentBrowserAssetItem>> updates)
    {
        var provider = new Mock<IContentBrowserAssetProvider>();
        _ = provider.SetupGet(value => value.Items).Returns(updates);
        _ = provider.Setup(value => value.RefreshAsync(It.IsAny<AssetBrowserFilter>(), It.IsAny<CancellationToken>())).Returns(Task.CompletedTask);
        return provider;
    }

    internal static ProjectContextService CreateQueryProject(bool cookedMount = false)
    {
        var projects = new ProjectContextService();
        projects.Activate(new() { ProjectId = Guid.NewGuid(), Name = "Query", Category = Category.Games, ProjectRoot = "C:/Query", AuthoringMounts = cookedMount ? [new("Content", "Content"), new("Cooked", ".cooked")] : [new("Content", "Content")], LocalFolderMounts = [], Scenes = [], });
        return projects;
    }

    internal static ContentBrowserAssetItem CreateQueryAsset(string name, AssetKind kind, AssetCookFreshness freshness)
    {
        var folder = kind == AssetKind.Material ? "Materials" : "Geometry";
        var asset = CreateNavigationAsset("/Content/" + folder + "/" + Uri.EscapeDataString(name), kind);
        var current = freshness == AssetCookFreshness.Current;
        return asset with
        {
            DisplayName = name,
            CookStatus = new(asset.IdentityUri, freshness, HasPublishedOutput: current, OutputAvailability: current ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [], [], [])
        };
    }
}
