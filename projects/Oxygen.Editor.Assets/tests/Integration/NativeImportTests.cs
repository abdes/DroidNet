// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Moq;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.ContentPipeline.Discovery;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Testing;
using Testably.Abstractions;

namespace Oxygen.Editor.Assets.Integration.Tests;

/// <summary>Verifies the real native import, the indexed project catalog and the asset feed together.</summary>
[TestClass]
public sealed class NativeImportTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A source becomes actual named, source-linked outputs through the production catalog and pipeline.</summary>
    /// <returns>The asynchronous native-backed browsing regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeImportPublishesNamedSourceLinkedBrowserAndPickerRows()
    {
        using var workspace = new AssetWorkspace();
        var sourcePath = workspace.SourcePath("Content/SourceMedia/DCC/model.gltf");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(sourcePath)!);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures/static_scalar_triangle.gltf"), sourcePath);
        var source = AssetPath.Parse("/Content/SourceMedia/DCC/model.gltf");
        var projects = workspace.ActivateProject();
        var documents = new CookDocumentRegistry();
        using var runs = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var scopes = new WorkspaceCookScopeProvider(workspace);
        var storage = new NativeStorageProvider(new RealFileSystem());
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var publication = new Oxygen.Editor.ContentPipeline.Publication.CookPublicationService(runs, projects, files, new ProjectManagerService(storage, atomicFiles: files));
        var pipeline = new ContentPipelineService(projects, runs, scopes, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, documents, compatibility, files, publication);
        await using var catalog = new AssetCatalog(projects, storage, CreateEmptyImportBuiltins(), publication);
        var runtime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(catalog, projects, scopes, pipeline, documents, runs, runtime, Mock.Of<IAssetRelocationService>(), Mock.Of<IAssetAuthoring>());
        IReadOnlyList<AssetEntry> choices = [];
        IReadOnlyList<AssetEntry> rows = [];
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single(row => row.Identity == source).PrimaryBadge.Should().Be("Not imported");
        _ = choices.Should().BeEmpty();
        var result = await pipeline.ImportSourceAsync(new(projects.ActiveProject!, sourcePath, "Crate", AssetPath.Parse("/Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single(row => row.Identity == source).CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
        foreach (var output in result.CookedAssets)
        {
            var row = rows.Should().ContainSingle(row => row.Identity == output.CookedAssetUri).Subject;
            _ = row.ImportSource.Should().Be(source);
            _ = row.CookedMetadata.Should().NotBeNull("the production index, not a guessed file path, must supply the output representation");
            _ = row.Kind.ToString().Should().Be(output.Kind.ToString());
            _ = row.CookStatus!.Freshness.Should().Be(AssetCookFreshness.Current);
            _ = row.CanCook.Should().BeTrue();
        }

        _ = choices.Select(static choice => choice.Identity).Should().BeEquivalentTo(result.CookedAssets.Where(static output => output.Kind == ContentCookAssetKind.Material).Select(static output => output.CookedAssetUri));
        _ = rows.Count(row => row.Identity == source).Should().Be(1);
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
