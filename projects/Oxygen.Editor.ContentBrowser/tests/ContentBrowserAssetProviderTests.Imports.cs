// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Projects retained source and actual named output relationships onto browser and picker rows.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>A source without import settings exposes Import without inventing assignable outputs.</summary>
    /// <returns>The asynchronous before-import projection test.</returns>
    [TestMethod]
    public async Task UnimportedModelHasNoInventedPickerOutputs()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var reader = new DelegateStatusReader((_, _, _) => throw new InvalidOperationException("Unimported source has no cooking status to inspect."));
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([new(source)]), CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), runtime);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<MaterialPickerResult> choices = [];
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);
        var row = await provider.ResolveAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.PrimaryBadge.Should().Be("Not imported");
        _ = row.TypeDisplayName.Should().Be("glTF source");
        _ = row.CanCook.Should().BeFalse();
        _ = row.CanReimport.Should().BeFalse();
        _ = row.Information.Description.Should().Contain("Import this model");
        _ = choices.Should().BeEmpty();
        var query = new AssetBrowserQuery();
        query.StatusOptions.Single(static option => string.Equals(option.Label, "Not imported", StringComparison.Ordinal)).IsSelected = true;
        _ = query.Matches(row).Should().BeTrue();
    }

    /// <summary>Previously published names remain visible and typed even if their files or catalog rows need regeneration.</summary>
    /// <param name="verified">Whether the recorded output is still intact.</param>
    /// <returns>The asynchronous source/output projection test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task ModelOutputsKeepSourceOwnershipAndTypedChoices(bool verified)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var state = ModelStatus(workspace, source, verified);
        var reader = new DelegateStatusReader((_, uris, _) =>
        {
            _ = uris.Should().Contain(source);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]);
        });
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([new(source)]), CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), runtime);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        IReadOnlyList<MaterialPickerResult> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        picker.Results.Subscribe(new Observer<IReadOnlyList<MaterialPickerResult>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().HaveCount(4);
        _ = rows.Should().OnlyHaveUniqueItems(static row => row.IdentityUri);
        var model = rows.Single(row => row.IdentityUri == source);
        _ = model.PrimaryState.Should().Be(AssetState.Source);
        _ = model.CanReimport.Should().BeTrue();
        _ = model.Information.Facts.Single(static fact => string.Equals(fact.Label, "Outputs", StringComparison.Ordinal)).Value.Should().Contain("Main (Geometry)").And.Contain("Paint (Material)").And.Contain("Crate (Scene)");
        var outputs = rows.Where(row => row.IdentityUri != source).ToArray();
        _ = outputs.Should().OnlyContain(row => row.ImportSourceUri == source && row.CanCook && row.CanReimport && row.DescriptorPath == null && row.SourcePath == null);
        _ = outputs.Should().OnlyContain(row => row.CookStatus!.HasAvailableOutput == verified);
        _ = outputs.Should().OnlyContain(row => row.Information.Facts.Any(fact => fact.Label == "Imported from" && fact.Value == source.AbsolutePath));
        _ = choices.Should().ContainSingle().Which.MaterialUri.Should().Be(state.Outputs.Single(static output => output.Kind == ContentCookAssetKind.Material).CookedAssetUri);
    }

    /// <summary>Equal virtual names in a foreign library do not acquire the project's source ownership or reimport action.</summary>
    /// <param name="library">Whether the effective output is supplied by a foreign library.</param>
    /// <returns>The asynchronous physical-origin projection test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task ModelOutputOwnershipMatchesItsPhysicalRoot(bool library)
    {
        using var workspace = new TempWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var projects = CreateProjectContextService(workspace);
        var sourceKey = Guid.CreateVersion7();
        var ownedRoot = CookPublicationPaths.Generation(workspace.Root, sourceKey);
        Directory.CreateDirectory(ownedRoot);
        await File.WriteAllBytesAsync(Path.Combine(ownedRoot, CookedGeneration.MarkerFileName), [], this.TestContext.CancellationToken).ConfigureAwait(false);
        var project = projects.ActiveProject!;
        var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, project.ProjectId, Guid.NewGuid(), DateTimeOffset.UtcNow,
            CookPublicationDocument.ConfigurationIdentity(project), [new(CookPublicationRootOwner.Project, "Content", sourceKey, new string('0', 64), LibraryPath: null)], [], CookInputs: null);
        var files = new DroidNet.Storage.Native.NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
        using var gate = await CookOutputLease.AcquireWriteAsync(workspace.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var documentPath = CookPublicationPaths.Document(workspace.Root, document.OperationId);
        Directory.CreateDirectory(Path.GetDirectoryName(documentPath)!);
        var version = await files.WriteAsync(documentPath, System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(document, CookPublicationDocument.JsonOptions), DroidNet.Storage.FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        var head = new CookPublicationHead(CookPublicationHead.CurrentVersion, document.OperationId, version.Sha256);
        _ = await files.WriteAsync(CookPublicationPaths.Head(workspace.Root), System.Text.Json.JsonSerializer.SerializeToUtf8Bytes(head, CookPublicationDocument.JsonOptions), DroidNet.Storage.FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var selected = await CookPublicationReadLease.OpenUnderGateAsync(project, files, gate, this.TestContext.CancellationToken).ConfigureAwait(false);
        var state = ModelStatus(workspace, source, verified: true) with { OutputRoots = selected.ProjectRoots };
        var output = state.Outputs.Single(static asset => asset.Kind == ContentCookAssetKind.Material);
        var root = library ? workspace.SourcePath("Library") : ownedRoot;
        const string descriptor = "Models/Crate/Materials/Paint.omat";
        _ = Directory.CreateDirectory(Path.GetDirectoryName(Path.Combine(root, descriptor))!);
        await File.WriteAllTextAsync(Path.Combine(root, descriptor), "output", this.TestContext.CancellationToken).ConfigureAwait(false);
        var metadata = new CookedAssetMetadata(root, descriptor, Guid.NewGuid(), new(1, 2), 1, 6, new string('0', 64)) { VirtualPath = output.VirtualPath };
        var records = new AssetRecord[] { new(source), new(output.CookedAssetUri) { Cooked = metadata } };
        var reader = new DelegateStatusReader((_, uris, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>(uris.Select(uri => state with { AssetUri = uri }).ToArray()));
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog(records) { Publication = selected }, projects, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), runtime);
        var row = await provider.ResolveAsync(output.CookedAssetUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.CanCook.Should().Be(!library);
        _ = row.CanReimport.Should().Be(!library);
        _ = row.ImportSourceUri.Should().Be(library ? null : source);
        _ = row.CookedMetadata.Should().BeSameAs(metadata);
    }

    /// <summary>A source cook updates all related output rows without another catalog/status scan.</summary>
    /// <returns>The asynchronous live-operation projection test.</returns>
    [TestMethod]
    public async Task ModelCookActivityUpdatesNamedOutputsWithoutScanning()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var state = ModelStatus(workspace, source, verified: true);
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]);
        });
        var projects = CreateProjectContextService(workspace);
        var runs = new Mock<ICookRunService>();
        IReadOnlyList<CookRunSnapshot> snapshots = [];
        _ = runs.SetupGet(value => value.Runs).Returns(() => snapshots);
        var runtime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = runtime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([new(source)]), projects, new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), runs.Object, runtime);
        IReadOnlyList<ContentBrowserAssetItem> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<ContentBrowserAssetItem>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
        var run = new CookRunSnapshot { OperationId = Guid.NewGuid(), ProjectId = projects.ActiveProject!.ProjectId, ProjectRoot = workspace.Root, DisplayName = "Crate", Request = new(CookTargetKind.Asset, source), State = CookRunState.Cooking };
        snapshots = [run];
        runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Should().HaveCount(4).And.OnlyContain(static row => row.PrimaryBadge == "Cooking");
        _ = reads.Should().Be(1);
    }

    private static async Task<Uri> WriteModelSourceAsync(TempWorkspace workspace, bool configured, CancellationToken cancellationToken)
    {
        var uri = new Uri("asset:///Content/SourceMedia/DCC/Crate/model.gltf");
        var path = workspace.SourcePath(uri.AbsolutePath.TrimStart('/'));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllTextAsync(path, "{}", cancellationToken).ConfigureAwait(false);
        if (configured)
        {
            await File.WriteAllTextAsync(path + ".import.json", "{}", cancellationToken).ConfigureAwait(false);
        }

        return uri;
    }

    private static AssetCookStatus ModelStatus(TempWorkspace workspace, Uri source, bool verified) => new(
        source,
        verified ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate,
        HasPublishedOutput: true,
        verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing,
        [
            new(source, new("asset:///Content/Models/Crate/Geometry/Main.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Models/Crate/Geometry/Main.ogeo"),
            new(source, new("asset:///Content/Models/Crate/Materials/Paint.omat"), ContentCookAssetKind.Material, "Content", "/Content/Models/Crate/Materials/Paint.omat"),
            new(source, new("asset:///Content/Models/Crate/Scenes/Crate.oscene"), ContentCookAssetKind.Scene, "Content", "/Content/Models/Crate/Scenes/Crate.oscene"),
        ],
        [],
        []) { SourcePaths = [workspace.SourcePath(source.AbsolutePath.TrimStart('/'))] };
}
