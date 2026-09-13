// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.AssetIdentity;
using Oxygen.Editor.ContentBrowser.Materials;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Managed.Assets.Catalog;

namespace Oxygen.Editor.ContentBrowser.Tests;

/// <summary>Uses verified publication ownership, rather than filenames, to recognize engine-provided project copies.</summary>
public sealed partial class ContentBrowserAssetProviderTests
{
    /// <summary>Default material and every generated shape retain built-in identity and cannot expose standalone cooking.</summary>
    /// <returns>The asynchronous catalog and picker regression.</returns>
    [TestMethod]
    public async Task VerifiedBuiltinCopiesKeepTheirOriginAndExistingReferenceUris()
    {
        using var workspace = new TempWorkspace();
        var native = BuiltinGeometryCatalog.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false));
        var originals = native.CreateCatalogRecords();
        var copies = originals.Select(record => new AssetRecord(new Uri("asset://" + record.Generated!.CookedVirtualPath))).ToArray();
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
            HasVerifiedOutput: true,
            [new(record.Uri, copies[index].Uri, copies[index].Uri.AbsolutePath.EndsWith(".omat", StringComparison.Ordinal) ? ContentCookAssetKind.Material : ContentCookAssetKind.Geometry, "Content", copies[index].Uri.AbsolutePath)],
            [],
            [])).ToArray();
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>(statuses));
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog([.. originals, .. copies]), CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime);
        using var picker = new MaterialPickerService(provider);
        IReadOnlyList<MaterialPickerResult> choices = [];
        using var subscription = picker.Results.Subscribe(value => choices = value);
        await provider.RefreshAsync(AssetBrowserFilter.Default, this.TestContext.CancellationToken).ConfigureAwait(false);
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

        var material = await picker.ResolveAsync(new Uri("asset://" + native.DefaultMaterial.VirtualPath), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = material!.StatusText.Should().Be("Built-in");
        _ = material.BuiltinOriginUri.Should().Be(Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default"));
        _ = choices.Should().ContainSingle().Which.MaterialUri.Should().Be(Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default"));
        await picker.RefreshAsync(MaterialPickerFilter.Default with { IncludeGenerated = false }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = choices.Should().BeEmpty("a pinned cooked copy must not become a duplicate ordinary material choice");
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
        using var workspace = new TempWorkspace();
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

        var records = new List<AssetRecord> { new(engineUri) { Generated = new("Default", "oxygen.material-descriptor.v1", cookedUri.AbsolutePath) }, new(cookedUri) };
        if (authored)
        {
            records.Add(new(descriptorUri));
        }

        var state = new AssetCookStatus(engineUri, AssetCookFreshness.Current, HasPublishedOutput: true, verified, [new(engineUri, cookedUri, ContentCookAssetKind.Material, "Content", cookedUri.AbsolutePath)], [], []);
        var reader = new DelegateStatusReader((_, _, _) => Task.FromResult<IReadOnlyList<AssetCookStatus>>([state]));
        var unavailableRuntime = Oxygen.Testing.AssetStatusFixture.CreateUnavailableRuntime();
        await using var runtimeLifetime = unavailableRuntime.ConfigureAwait(false);
        using var provider = new ContentBrowserAssetProvider(new TestProjectAssetCatalog(records), CreateProjectContextService(workspace), new TestProjectCookScopeProvider(workspace), new AssetIdentityReducer(), reader, new CookDocumentRegistry(), EmptyCookRuns(), unavailableRuntime);
        var row = await provider.ResolveAsync(authored ? descriptorUri : cookedUri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = row!.IsBuiltin.Should().BeFalse();
        _ = row.BuiltinOriginUri.Should().BeNull();
        _ = row.CanCook.Should().Be(authored);
    }
}
