// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Editor.Assets.TestSupport;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>Uses verified publication ownership, rather than filenames, to recognize engine-provided project copies.</summary>
public sealed partial class AssetFeedTests
{
    /// <summary>A foreign override is not labelled built-in, while its verified masked project copy retains its origin.</summary>
    /// <returns>The asynchronous source-qualified provenance regression.</returns>
    [TestMethod]
    public async Task ForeignOverrideCannotBorrowBuiltinProvenanceFromAnotherRoot()
    {
        using var workspace = new AssetWorkspace();
        var engineUri = Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default");
        var cookedUri = new Uri("asset:///Content/Materials/OxygenEditor_Default.omat");
        var projectRoot = workspace.SourcePath(".cooked/Content");
        var libraryRoot = workspace.SourcePath("Library");
        const string descriptor = "Materials/OxygenEditor_Default.omat";
        foreach (var root in new[] { projectRoot, libraryRoot })
        {
            _ = Directory.CreateDirectory(Path.Combine(root, "Materials"));
            await File.WriteAllTextAsync(Path.Combine(root, descriptor), "output", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var own = new CookedAssetMetadata(projectRoot, descriptor, Guid.CreateVersion7(), new(1, 2), 1, 6, new string('0', 64)) { VirtualPath = cookedUri.AbsolutePath };
        var foreign = own with { RootFolderPath = libraryRoot, SourceIdentity = Guid.CreateVersion7(), AssetKey = new(3, 4) };
        AssetRecord[] records =
        [
            new(engineUri) { Generated = new("Default", "oxygen.material-descriptor.v1", cookedUri.AbsolutePath, GeneratedAssetCategory.Standard) },
            new(cookedUri) { Cooked = foreign, OverriddenCookedSources = [own] },
        ];
        var state = new AssetCookStatus(engineUri, AssetCookFreshness.Current, HasPublishedOutput: true, OutputAvailability: CookedOutputAvailability.Present, [new(engineUri, cookedUri, ContentCookAssetKind.Material, "Content", cookedUri.AbsolutePath)], [], []) { OutputRoots = System.Collections.Immutable.ImmutableDictionary<string, string>.Empty.Add("Content", workspace.SourcePath(".cooked/Content")) };
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]));
        var projects = workspace.ActivateProject();
        var runtime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(new TestProjectAssetCatalog(records), projects, new WorkspaceCookScopeProvider(workspace), reader, new CookDocumentRegistry(), EmptyCookRuns(), runtime, NoRelocations(), NoAuthoring());
        var resolved = (await provider.ResolveAsync(cookedUri, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        _ = resolved.IsBuiltin.Should().BeFalse();
        _ = resolved.BuiltinOriginUri.Should().BeNull();
        var projectCopy = AssetIdentity.FromCookedSource(resolved, own, projects.ActiveProject!);
        _ = projectCopy.IsBuiltin.Should().BeTrue();
        _ = projectCopy.BuiltinOriginUri.Should().Be(engineUri);
        _ = projectCopy.IsCookedSourceOverridden.Should().BeTrue();
        _ = projectCopy.CanCook.Should().BeFalse();
    }

    /// <summary>Default material and every generated shape retain built-in identity and cannot expose standalone cooking.</summary>
    /// <returns>The asynchronous catalog and picker regression.</returns>
    [TestMethod]
    public async Task VerifiedBuiltinCopiesKeepTheirOriginAndExistingReferenceUris()
    {
        using var workspace = new AssetWorkspace();
        var native = BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
        var originals = native.CreateCatalogRecords();
        var copies = originals.Select(record => IndexedOutput(new Uri("asset://" + record.Generated!.CookedVirtualPath), workspace.SourcePath(".cooked" + record.Generated.CookedVirtualPath), record.Generated.CookedVirtualPath.EndsWith(".omat", StringComparison.Ordinal) ? (byte)1 : (byte)2)).ToArray();
        foreach (var record in copies)
        {
            var path = workspace.SourcePath(".cooked" + record.Uri.AbsolutePath);
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            await File.WriteAllTextAsync(path, "verified by the status authority", this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var statuses = originals.Select((record, index) => new AssetCookStatus(
            record.Uri,
            AssetCookFreshness.Current,
            HasPublishedOutput: true,
            OutputAvailability: CookedOutputAvailability.Present,
            [new(record.Uri, copies[index].Uri, copies[index].Uri.AbsolutePath.EndsWith(".omat", StringComparison.Ordinal) ? ContentCookAssetKind.Material : ContentCookAssetKind.Geometry, "Content", copies[index].Uri.AbsolutePath)],
            [],
            [])
        { OutputRoots = System.Collections.Immutable.ImmutableDictionary<string, string>.Empty.Add("Content", workspace.SourcePath(".cooked/Content")) }).ToArray();
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>(statuses));
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(new TestProjectAssetCatalog([.. originals, .. copies]), workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), reader, new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
        IReadOnlyList<AssetEntry> items = [];
        using var subscription = provider.Items.Subscribe(value => items = value);
        await provider.RefreshAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        foreach (var (original, copy) in originals.Zip(copies))
        {
            var row = await provider.ResolveAsync(copy.Uri, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = row!.IdentityUri.Should().Be(copy.Uri, "existing scene references must remain valid");
            _ = row.BuiltinOriginUri.Should().Be(original.Uri);
            _ = row.PrimaryBadge.Should().Be("Built-in");
            _ = row.DisplayName.Should().Be(original.Name);
            _ = row.CanCook.Should().BeFalse();
            _ = row.DescriptorPath.Should().BeNull();
            _ = row.CookStatus.Should().BeNull("generated runtime contributions are not authored inputs");
        }

        var defaultCopy = new Uri("asset://" + native.DefaultMaterial.VirtualPath);
        var material = await provider.ResolveAsync(defaultCopy, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = AssetStatusText.ForEntry(material!).Should().Be("Built-in");
        _ = material!.BuiltinOriginUri.Should().Be(Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default"));
        _ = AssetIdentity.GroupBuiltins(items).Where(static item => item.Kind == AssetKind.Material).Should().ContainSingle()
            .Which.IdentityUri.Should().Be(Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default"));
        IReadOnlyList<AssetEntry> contentMaterials = [];
        using var contentSubscription = provider.Assets(AssetRule.For(AssetKind.Material), defaultCopy).Subscribe(value => contentMaterials = value);
        _ = contentMaterials.Should().BeEmpty("a kept built-in copy must not become a duplicate ordinary material");
    }

    /// <summary>Similar names and authored descriptors cannot be mistaken for engine-owned content.</summary>
    /// <param name="verified">Whether the output still matches its engine provenance.</param>
    /// <param name="authored">Whether the project has an independent authored descriptor at this identity.</param>
    /// <returns>The asynchronous identity-collision regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, true)]
    public async Task UnprovenOrAuthoredMaterialsAreNotReclassifiedAsBuiltins(bool verified, bool authored)
    {
        using var workspace = new AssetWorkspace();
        var engineUri = Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default");
        var cookedUri = new Uri("asset:///Content/Materials/OxygenEditor_Default.omat");
        var descriptorUri = new Uri(cookedUri + ".json");
        var cookedPath = workspace.SourcePath(".cooked/Content/Materials/OxygenEditor_Default.omat");
        Directory.CreateDirectory(Path.GetDirectoryName(cookedPath)!);
        await File.WriteAllTextAsync(cookedPath, "output", this.TestContext.CancellationToken).ConfigureAwait(false);
        if (authored)
        {
            WriteMaterial(workspace.SourcePath("Content/Materials/OxygenEditor_Default.omat.json"));
        }

        var records = new List<AssetRecord> { new(engineUri) { Generated = new("Default", "oxygen.material-descriptor.v1", cookedUri.AbsolutePath, GeneratedAssetCategory.Standard) }, new(cookedUri) };
        if (authored)
        {
            records.Add(new(descriptorUri));
        }

        var state = new AssetCookStatus(engineUri, AssetCookFreshness.Current, HasPublishedOutput: true, verified ? CookedOutputAvailability.Present : CookedOutputAvailability.Missing, [new(engineUri, cookedUri, ContentCookAssetKind.Material, "Content", cookedUri.AbsolutePath)], [], []) { OutputRoots = System.Collections.Immutable.ImmutableDictionary<string, string>.Empty.Add("Content", workspace.SourcePath(".cooked/Content")) };
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]));
        var unavailableRuntime = AssetStatusFixture.CreateUnavailableRuntimeStatus();
        using var provider = new AssetFeed(new TestProjectAssetCatalog(records), workspace.ActivateProject(), new WorkspaceCookScopeProvider(workspace), reader, new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime, NoRelocations(), NoAuthoring());
        var row = await provider.ResolveAsync(authored ? descriptorUri : cookedUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.IsBuiltin.Should().BeFalse();
        _ = row.BuiltinOriginUri.Should().BeNull();
        _ = row.CanCook.Should().Be(authored);
    }
}
