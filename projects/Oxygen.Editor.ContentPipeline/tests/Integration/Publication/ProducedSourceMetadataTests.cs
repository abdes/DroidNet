// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.TestSupport;
using static Oxygen.Editor.ContentPipeline.TestSupport.PublicationScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Publication;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ProducedSourceMetadataTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Later authored settings changes do not corrupt immutable publication metadata.</summary>
    /// <returns>The asynchronous publication regression.</returns>
    [TestMethod]
    public async Task ProducedSettingsCommitWithoutRewritingConsumedSnapshot()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var update = await this.CreateSourceUpdateAsync(project, "Model").ConfigureAwait(false);
        var captured = Path.Combine(project.Root, ".build/cook", project.Operation.OperationId.ToString("N"), "inputs", update.RelativePath);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(captured)!);
        await File.WriteAllBytesAsync(captured, update.Before, this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, producedSourceFiles: [update]).ConfigureAwait(false);
        using var accepted = await transaction.PublishAsync(null, project.Baseline, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await File.ReadAllBytesAsync(captured, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.Before);
        _ = (await File.ReadAllBytesAsync(Path.Combine(project.Root, update.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.After);
        await File.WriteAllTextAsync(Path.Combine(project.Root, update.RelativePath), "later authored change", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var writer = await CookOutputLease.AcquireWriteAsync(project.Root, this.TestContext.CancellationToken).ConfigureAwait(false);
        var loaded = await CookPublicationTransaction.LoadAsync(project.Context, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        using var selection = await CookPublicationReadLease.OpenUnderGateAsync(project.Context, project.Files, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = selection.PublicationId.Should().Be(project.Operation.OperationId);
        project.AssertNew();
    }

    /// <summary>Explicit replacement stages produced settings separately without installing the same file twice.</summary>
    /// <returns>The asynchronous replacement regression.</returns>
    [TestMethod]
    public async Task ProducedSettingsJoinExplicitBundleReplacement()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var update = await this.CreateSourceUpdateAsync(project, "Model").ConfigureAwait(false);
        var published = Path.Combine(project.Root, SourceBundle);
        await File.WriteAllTextAsync(Path.Combine(published, "model.gltf"), "source", this.TestContext.CancellationToken).ConfigureAwait(false);
        var captured = Path.Combine(project.Root, ".build/cook", project.Operation.OperationId.ToString("N"), "inputs", SourceBundle);
        var before = await CookRootImage.CaptureAsync(published, captured, this.TestContext.CancellationToken).ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken,
            sourceReplacement: new("Model", before), producedSourceFiles: [update]).ConfigureAwait(false);
        using var accepted = await transaction.PublishAsync(null, project.Baseline, static () => { }, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = (await File.ReadAllBytesAsync(Path.Combine(captured, "model.gltf.import.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.Before);
        _ = (await File.ReadAllBytesAsync(Path.Combine(published, "model.gltf.import.json"), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.After);
        project.AssertNew();
    }

    /// <summary>A source edit after preparation is preserved rather than overwritten by stale native metadata.</summary>
    /// <returns>The asynchronous conflict regression.</returns>
    [TestMethod]
    public async Task ProducedSettingsRejectAnExternalEditBeforeInstallation()
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var update = await this.CreateSourceUpdateAsync(project, "Model").ConfigureAwait(false);
        var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, producedSourceFiles: [update]).ConfigureAwait(false);
        var path = Path.Combine(project.Root, update.RelativePath);
        await File.WriteAllTextAsync(path, "external edit", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> publish = () => transaction.PublishAsync(null, project.Baseline, static () => { }, this.TestContext.CancellationToken);
        _ = await publish.Should().ThrowAsync<DroidNet.Storage.StorageWriteConflictException>().ConfigureAwait(false);
        _ = (await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("external edit");
        project.AssertOld();
    }

    /// <summary>File updates and cooked directories restore together after partial source publication.</summary>
    /// <param name="persisted">Whether recovery reopens an interrupted on-disk transaction.</param>
    /// <param name="projectOwned">Whether the source is directly authored outside a copied model bundle.</param>
    /// <returns>The asynchronous recovery regression.</returns>
    [TestMethod]
    [DataRow(false, false)]
    [DataRow(true, false)]
    [DataRow(false, true)]
    [DataRow(true, true)]
    public async Task ProducedSettingsRollbackAcrossMultipleSources(bool persisted, bool projectOwned)
    {
        using var project = new PublicationProject(hadPrevious: true);
        await using var staging = await project.StageAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var first = await this.CreateSourceUpdateAsync(project, "First", projectOwned).ConfigureAwait(false);
        var second = await this.CreateSourceUpdateAsync(project, "Second", projectOwned).ConfigureAwait(false);
        var replica = Directory.CreateTempSubdirectory("oxygen-source-metadata-recovery-");
        try
        {
            var transaction = await project.PrepareAsync(staging, this.TestContext.CancellationToken, async boundary =>
            {
                if (boundary == "SourceFile:" + first.RelativePath)
                {
                    if (persisted)
                    {
                        await CopyPersistedStateAsync(project.Root, replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
                    }

                    throw new IOException("Injected source metadata interruption");
                }
            }, producedSourceFiles: [first, second]).ConfigureAwait(false);
            Func<Task> publish = () => transaction.PublishAsync(null, project.Baseline, static () => { }, this.TestContext.CancellationToken);
            _ = await publish.Should().ThrowAsync<IOException>().ConfigureAwait(false);
            project.AssertOld();
            foreach (var update in new[] { first, second })
            {
                _ = (await File.ReadAllBytesAsync(Path.Combine(project.Root, update.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.Before);
            }

            if (persisted)
            {
                using var writer = await CookOutputLease.AcquireWriteAsync(replica.FullName, this.TestContext.CancellationToken).ConfigureAwait(false);
                var loaded = await CookPublicationTransaction.LoadAsync(project.Context with { ProjectRoot = replica.FullName }, project.Operation.OperationId, project.Files, project.Manager, writer, this.TestContext.CancellationToken).ConfigureAwait(false);
                await loaded.RecoverAsync(writer).ConfigureAwait(false);
                foreach (var update in new[] { first, second })
                {
                    _ = (await File.ReadAllBytesAsync(Path.Combine(replica.FullName, update.RelativePath), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Equal(update.Before);
                }
            }
        }
        finally
        {
            replica.Delete(recursive: true);
        }
    }

    /// <summary>The accepted source state reuses output while consumed fingerprint evidence remains unchanged.</summary>
    /// <returns>The asynchronous planner regression.</returns>
    [TestMethod]
    public async Task PublishedProvenanceDoesNotCausePerpetualRecooking()
    {
        using var project = new PublicationProject(hadPrevious: true);
        var update = await this.CreateSourceUpdateAsync(project, "Model").ConfigureAwait(false);
        var uri = new Uri("asset:///Content/SourceMedia/DCC/Model/model.gltf");
        var input = new ContentCookInput(uri, ContentCookAssetKind.ForeignSource, "Content", "Content/SourceMedia/DCC/Model/model.gltf",
            Path.Combine(project.Root, "Content/SourceMedia/DCC/Model/model.gltf"), null, ContentCookInputRole.Primary);
        ImmutableArray<CookSnapshotInput> consumed = [new(null, Path.Combine(project.Root, update.RelativePath), update.RelativePath, update.BeforeHash)];
        var graph = new CookDependencyGraph([input], consumed,
            ImmutableDictionary<Uri, ImmutableArray<Uri>>.Empty.Add(uri, []),
            ImmutableDictionary<Uri, ImmutableArray<string>>.Empty.Add(uri, [update.RelativePath]), [], [], []);
        var accepted = CookProducedSourceFile.ExpectedInputs(consumed, [update]);
        const string producer = "native-producer";
        var beforeFingerprint = CookIncrementalPlanner.Fingerprint(input, producer, consumed, graph);
        var reuseFingerprint = CookIncrementalPlanner.Fingerprint(input, producer, accepted, graph);
        var output = new ContentCookedAsset(uri, new("asset:///Content/Geometry/Model.ogeo"), ContentCookAssetKind.Geometry, "Content", "/Content/Geometry/Model.ogeo")
        {
            DescriptorRelativePath = "Geometry/Model.ogeo",
        };
        var path = Path.Combine(project.Root, ".cooked/Content/Geometry/Model.ogeo");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllTextAsync(path, "geometry", this.TestContext.CancellationToken).ConfigureAwait(false);
        var product = new CookProvenance.Product(uri, beforeFingerprint, [], [new(output, "Content")]) { ReuseFingerprint = reuseFingerprint };
        var rootPath = Path.Combine(project.Root, ".cooked", "Content");
        Oxygen.Testing.NativeInventoryFixture.WriteIndex(rootPath, [new(output.VirtualPath, output.Kind) { DescriptorRelativePath = "Geometry/Model.ogeo" }]);
        var inventory = Oxygen.Testing.NativeInventoryFixture.Read(rootPath);
        var root = new CookProvenance.Root("Content", inventory.SourceKey, inventory.IndexSha256);
        var previous = new CookProvenance(project.Context.ProjectId, [root], [product]);
        var verified = ImmutableDictionary<string, global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport>.Empty.Add("Content", inventory);
        var plan = CookIncrementalPlanner.CreatePlan(producer, accepted, graph, previous, verified);
        _ = plan.Reusable.Keys.Should().Contain(uri);
        _ = product.Fingerprint.Should().Be(beforeFingerprint).And.NotBe(reuseFingerprint);
        _ = consumed.Single().DiscoveryHash.Should().Be(update.BeforeHash);
        var changed = accepted.SetItem(0, accepted[0] with { DiscoveryHash = new string('F', 64) });
        var edited = CookIncrementalPlanner.CreatePlan(producer, changed, graph, previous, verified);
        _ = edited.Reusable.Should().BeEmpty();
    }

    private async Task<CookProducedSourceFile> CreateSourceUpdateAsync(PublicationProject project, string bundle, bool projectOwned = false)
    {
        var primary = projectOwned ? bundle + ".gltf" : "model.gltf";
        var root = projectOwned ? "Content/SourceMedia/DCC" : "Content/SourceMedia/DCC/" + bundle;
        var source = new RetainedImportSource(root, primary, [new(primary, Convert.ToHexString(SHA256.HashData("source"u8)))]);
        var settings = NativeSceneImportSettings.Create(source, "Content", bundle, bundle);
        var candidate = NativeMaterialSlotProvenance.Parse(JsonSerializer.SerializeToElement(new
        {
            schema_version = 1,
            source_identity = settings.MaterialSlotProvenance.SourceIdentity,
            geometries = new[]
            {
                new
                {
                    geometry_asset_key = "10000000-0000-0000-0000-000000000001", source_geometry_anchor = "mesh0",
                    source_layout_witness = new string('a', 64), layout_revision = new string('b', 64),
                    slots = new[] { new { slot_id = "20000000-0000-0000-0000-000000000001", display_name = "Surface",
                        bindings = new[] { new { lod_index = 0, submesh_index = 0, default_material_key = "30000000-0000-0000-0000-000000000001" } } } },
                    allocations = new[] { new { declaration_key = 0, slot_id = "20000000-0000-0000-0000-000000000001" } },
                },
            },
        }));
        var relative = settings.BundleRoot + "/" + settings.PrimaryRelativePath + ".import.json";
        var update = new CookProducedSourceFile(relative, settings.ToBytes(), (settings with { MaterialSlotProvenance = candidate }).ToBytes());
        var path = Path.Combine(project.Root, relative);
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        await File.WriteAllBytesAsync(path, update.Before, this.TestContext.CancellationToken).ConfigureAwait(false);
        return update;
    }
}
