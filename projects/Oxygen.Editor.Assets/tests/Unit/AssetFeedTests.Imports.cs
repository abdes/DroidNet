// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Projects retained source and actual named output relationships onto browser and picker rows.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>A source without import settings exposes Import without inventing assignable outputs.</summary>
    /// <returns>The asynchronous before-import projection test.</returns>
    [TestMethod]
    public async Task UnimportedModelHasNoInventedPickerOutputs()
    {
        using var workspace = new AssetWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: false, this.TestContext.CancellationToken).ConfigureAwait(false);
        var reader = new DelegateStatusReader((_, _, _) => throw new InvalidOperationException("Unimported source has no cooking status to inspect."));
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new(source)]), workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        IReadOnlyList<AssetEntry> choices = [];
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        var row = await provider.ResolveAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.PrimaryBadge.Should().Be("Not imported");
        _ = row.TypeDisplayName.Should().Be("glTF source");
        _ = row.CanCook.Should().BeFalse();
        _ = row.CanReimport.Should().BeFalse();
        _ = row.Information.Description.Should().Contain("Import this model");
        _ = choices.Should().BeEmpty();
    }

    /// <summary>Previously published names remain visible and typed even if their files or catalog rows need regeneration.</summary>
    /// <param name="verified">Whether the recorded output is still intact.</param>
    /// <returns>The asynchronous source/output projection test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task ModelOutputsKeepSourceOwnershipAndTypedChoices(bool verified)
    {
        using var workspace = new AssetWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var state = ModelStatus(workspace, source, verified);
        var reader = new DelegateStatusReader((_, uris, _) =>
        {
            _ = uris.Should().Contain(source);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]);
        });
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new(source)]), workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        IReadOnlyList<AssetEntry> rows = [];
        IReadOnlyList<AssetEntry> choices = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        provider.Assets(AssetRule.For(AssetKind.Material)).Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => choices = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = rows.Should().HaveCount(4);
        _ = rows.Should().OnlyHaveUniqueItems(static row => row.Identity);
        var model = rows.Single(row => row.Identity == source);
        _ = model.PrimaryState.Should().Be(AssetState.Source);
        _ = model.CanReimport.Should().BeTrue();
        _ = model.Information.Facts.Single(static fact => string.Equals(fact.Label, "Outputs", StringComparison.Ordinal)).Value.Should().Contain("Main (Geometry)").And.Contain("Paint (Material)").And.Contain("Crate (Scene)");
        var outputs = rows.Where(row => row.Identity != source).ToArray();
        _ = outputs.Should().OnlyContain(row => row.ImportSource == source && row.CanCook && row.CanReimport && row.DescriptorPath == null && row.SourcePath == null);
        _ = outputs.Should().OnlyContain(row => row.CookStatus!.HasAvailableOutput == verified);
        _ = outputs.Should().OnlyContain(row => row.Information.Facts.Any(fact => fact.Label == "Imported from" && fact.Value == source.VirtualPath));
        _ = choices.Should().ContainSingle().Which.Identity.Should().Be(state.Outputs.Single(static output => output.Kind == ContentCookAssetKind.Material).CookedAssetUri);
    }

    /// <summary>Equal virtual names in a foreign library do not acquire the project's source ownership or reimport action.</summary>
    /// <param name="library">Whether the effective output is supplied by a foreign library.</param>
    /// <returns>The asynchronous physical-origin projection test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task ModelOutputOwnershipMatchesItsPhysicalRoot(bool library)
    {
        using var workspace = new AssetWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var projects = workspace.ActivateProject();
        var project = projects.ActiveProject!;
        var ownedRoot = FileSystemPath.From(workspace.SourcePath(".cooked/Content"));
        Directory.CreateDirectory(ownedRoot.FullPath);
        var publication = new FakeCookPublication(project.ProjectId, project.ProjectRoot);
        publication.Publish([], new Dictionary<string, FileSystemPath> { ["Content"] = ownedRoot });
        using var selected = publication.Lease();
        var state = ModelStatus(workspace, source, verified: true) with { OutputRoots = selected.ProjectRoots };
        var output = state.Outputs.Single(static asset => asset.Kind == ContentCookAssetKind.Material);
        var root = library ? FileSystemPath.From(workspace.SourcePath("Library")) : ownedRoot;
        const string descriptor = "Models/Crate/Materials/Paint.omat";
        _ = Directory.CreateDirectory(Path.GetDirectoryName(root.Combine(descriptor).FullPath)!);
        await File.WriteAllTextAsync(root.Combine(descriptor).FullPath, "output", this.TestContext.CancellationToken).ConfigureAwait(false);
        var metadata = new CookedAssetMetadata(root, descriptor, Guid.NewGuid(), new(1, 2), 1, 6, new string('0', 64)) { VirtualPath = AssetPath.Parse(output.VirtualPath) };
        var records = new AssetRecord[] { new(source), new(output.CookedAssetUri) { Cooked = metadata } };
        var reader = new DelegateStatusReader((_, uris, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>(uris.Select(uri => state with { AssetUri = uri }).ToArray()));
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog(records) { Publication = selected }, projects, new WorkspaceCookScopeProvider(workspace), cookStatus: reader);
        var row = await provider.ResolveAsync(output.CookedAssetUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.CanCook.Should().Be(!library);
        _ = row.CanReimport.Should().Be(!library);
        if (library)
        {
            _ = row.ImportSource.Should().BeNull();
        }
        else
        {
            _ = row.ImportSource.Should().Be(source);
        }
        _ = row.CookedMetadata.Should().BeSameAs(metadata);
    }

    /// <summary>A source cook updates all related output rows without another catalog/status scan.</summary>
    /// <returns>The asynchronous live-operation projection test.</returns>
    [TestMethod]
    public async Task ModelCookActivityUpdatesNamedOutputsWithoutScanning()
    {
        using var workspace = new AssetWorkspace();
        var source = await WriteModelSourceAsync(workspace, configured: true, this.TestContext.CancellationToken).ConfigureAwait(false);
        var state = ModelStatus(workspace, source, verified: true);
        var reads = 0;
        var reader = new DelegateStatusReader((_, _, _) =>
        {
            _ = Interlocked.Increment(ref reads);
            return Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]);
        });
        var projects = workspace.ActivateProject();
        var runs = new Mock<ICookRunService>();
        IReadOnlyList<CookRunSnapshot> snapshots = [];
        _ = runs.SetupGet(value => value.Runs).Returns(() => snapshots);
        using var provider = AssetFeeds.Create(new TestProjectAssetCatalog([new(source)]), projects, new WorkspaceCookScopeProvider(workspace), cookStatus: reader, cooks: runs.Object);
        IReadOnlyList<AssetEntry> rows = [];
        provider.Items.Subscribe(new Observer<IReadOnlyList<AssetEntry>>(value => rows = value), this.TestContext.CancellationToken);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var run = new CookRunSnapshot { OperationId = Guid.NewGuid(), ProjectId = projects.ActiveProject!.ProjectId, ProjectRoot = workspace.Root, DisplayName = "Crate", Request = new(CookTargetKind.Asset, source), State = CookRunState.Cooking };
        snapshots = [run];
        runs.Raise(value => value.RunChanged += null, new CookRunChangedEventArgs(run));
        _ = rows.Should().HaveCount(4).And.OnlyContain(static row => row.PrimaryBadge == "Cooking");
        _ = reads.Should().Be(1);
    }

    private static async Task<AssetPath> WriteModelSourceAsync(AssetWorkspace workspace, bool configured, CancellationToken cancellationToken)
    {
        var identity = AssetPath.Parse("/Content/SourceMedia/DCC/Crate/model.gltf");
        var path = workspace.SourcePath(identity.VirtualPath.TrimStart('/'));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllTextAsync(path, "{}", cancellationToken).ConfigureAwait(false);
        if (configured)
        {
            await File.WriteAllTextAsync(path + ".import.json", "{}", cancellationToken).ConfigureAwait(false);
        }

        return identity;
    }

    private static AssetCookStatus ModelStatus(AssetWorkspace workspace, AssetPath source, bool verified) => new(
        source,
        verified ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate,
        HasPublishedOutput: true,
        verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing,
        [
            new(source, AssetPath.Parse("/Content/Models/Crate/Geometry/Main.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Models/Crate/Geometry/Main.ogeo"),
            new(source, AssetPath.Parse("/Content/Models/Crate/Materials/Paint.omat"), ContentCookAssetKind.Material, "Content", "/Content/Models/Crate/Materials/Paint.omat"),
            new(source, AssetPath.Parse("/Content/Models/Crate/Scenes/Crate.oscene"), ContentCookAssetKind.Scene, "Content", "/Content/Models/Crate/Scenes/Crate.oscene"),
        ],
        [])
    { SourcePaths = [FileSystemPath.From(workspace.SourcePath(source.VirtualPath.TrimStart('/')))] };
}
