// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.Assets.TestSupport;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
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
        using var runs = new ContentCookCoordinator(projects, NullLogger<ContentCookCoordinator>.Instance);
        using var compatibility = Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var scopes = new WorkspaceCookScopeProvider(workspace);
        var storage = new NativeStorageProvider(new RealFileSystem());
        var files = new NativeAtomicFileStore(new RealFileSystem());
        var engineContent = EngineContentPak.Installed();
        var publication = new Oxygen.Editor.ContentPipeline.Publication.CookPublicationService(runs, projects, files, new ProjectManagerService(storage, atomicFiles: files), engineContent);
        var pipeline = new ContentPipelineService(projects, runs, scopes, new SceneDescriptorGenerator(), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, compatibility, files, publication, engineContent);
        await using var catalog = new AssetCatalog(projects, storage, engineContent, publication);
        using var provider = AssetFeeds.Create(catalog, projects, scopes, cookStatus: pipeline, cooks: runs);
        IReadOnlyList<AssetEntry> choices = [];
        IReadOnlyList<AssetEntry> rows = [];
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Single(row => row.Identity == source).PrimaryBadge.Should().Be("Not imported");
        _ = choices.Should().OnlyContain(static choice => choice.Origin == AssetOrigin.Engine, "only the engine content pak's materials exist before the import");
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

        _ = choices.Where(static choice => choice.Origin == AssetOrigin.Content).Select(static choice => choice.Identity).Should().BeEquivalentTo(result.CookedAssets.Where(static output => output.Kind == ContentCookAssetKind.Material).Select(static output => output.CookedAssetUri));
        _ = rows.Count(row => row.Identity == source).Should().Be(1);
    }
}
