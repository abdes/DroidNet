// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Infrastructure.Assets;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Verifies the real native import, indexed project catalog and shared browser projection together.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>A source becomes actual named, source-linked outputs through the production catalog and pipeline.</summary>
    /// <returns>The asynchronous native-backed browsing regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeImportPublishesNamedSourceLinkedBrowserAndPickerRows()
    {
        using var workspace = new TempWorkspace();
        var sourcePath = workspace.SourcePath("Content/SourceMedia/DCC/model.gltf");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(sourcePath)!);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_scalar_triangle.gltf"), sourcePath);
        var source = new Uri("asset:///Content/SourceMedia/DCC/model.gltf");
        var projects = CreateProjectContextService(workspace);
        var documents = new CookDocumentRegistry();
        using var runs = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var scopes = new TestProjectCookScopeProvider(workspace);
        var pipeline = new ContentPipelineService(projects, runs, scopes, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, documents, compatibility);
        using var catalog = new ProjectAssetCatalog(projects, new NativeStorageProvider(new RealFileSystem()), CreateEmptyImportBuiltins());
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(catalog, projects, scopes, new AssetIdentityReducer(), pipeline, documents, runs, runtime);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<MaterialPickerResult> choices = [];
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single(row => row.IdentityUri == source).PrimaryBadge.Should().Be("Not imported");
        _ = choices.Should().BeEmpty();
        var result = await pipeline.ImportSourceAsync(new(projects.ActiveProject!, sourcePath, "Crate", new("asset:///Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single(row => row.IdentityUri == source).CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        foreach (var output in result.CookedAssets)
        {
            var row = rows.Should().ContainSingle(row => row.IdentityUri == output.CookedAssetUri).Subject;
            _ = row.ImportSourceUri.Should().Be(source);
            _ = row.CookedMetadata.Should().NotBeNull("the production index, not a guessed file path, must supply the output representation");
            _ = row.Kind.ToString().Should().Be(output.Kind.ToString());
            _ = row.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
            _ = row.CanCook.Should().BeTrue();
        }

        _ = choices.Select(static choice => choice.MaterialUri).Should().BeEquivalentTo(result.CookedAssets.Where(static output => output.Kind == ContentCookAssetKind.Material).Select(static output => output.CookedAssetUri));
        _ = rows.Count(row => row.IdentityUri == source).Should().Be(1);
    }

    private static IBuiltinCatalogDiscovery CreateEmptyImportBuiltins()
    {
        var empty = new BuiltinCatalogSnapshot(Catalog: null, IsLastKnown: false, Notice: null);
        var builtins = new Mock<IBuiltinCatalogDiscovery>();
        _ = builtins.SetupGet(value => value.Snapshot).Returns(empty);
        _ = builtins.Setup(value => value.GetAsync(It.IsAny<CancellationToken>())).ReturnsAsync(empty);
        return builtins.Object;
    }
}
