// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Text.Json;
using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Status;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetCookStatusTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Source changes affect freshness; cooked-byte integrity is checked explicitly.</summary>
    /// <param name="changeOutput">Whether to corrupt prior output instead of changing saved input.</param>
    /// <returns>The asynchronous hash-identity regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task FreshnessUsesSourceIdentityWhileIntegrityUsesValidation(bool changeOutput)
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var operationDirectories = Directory.EnumerateDirectories(Path.Combine(project.Root, ".build", "cook")).ToArray();
        var initial = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = initial.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = initial.HasAvailableOutput.Should().BeTrue();
        var path = changeOutput ? project.OutputPath : project.SourcePath;
        var timestamp = File.GetLastWriteTimeUtc(path);
        if (changeOutput)
        {
            await File.WriteAllTextAsync(path, "tampered", this.TestContext.CancellationToken).ConfigureAwait(false);
        }
        else
        {
            await File.WriteAllTextAsync(path, StatusProject.CreateMaterialJson(0.7f), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        File.SetLastWriteTimeUtc(path, timestamp);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(changeOutput ? AssetCookFreshness.Current : AssetCookFreshness.OutOfDate);
        _ = status.HasPublishedOutput.Should().BeTrue();
        _ = status.HasAvailableOutput.Should().BeTrue();
        _ = Oxygen.Testing.NativeInventoryFixture.Read(Path.GetDirectoryName(project.OutputPath)!).IsValid.Should().Be(!changeOutput);
        _ = status.Outputs.Should().ContainSingle();
        _ = Directory.EnumerateDirectories(Path.Combine(project.Root, ".build", "cook")).Should().BeEquivalentTo(operationDirectories);
    }

    /// <summary>Unsaved edits remain separate from the last acknowledged saved product.</summary>
    /// <returns>The asynchronous dirty-overlay regression.</returns>
    [TestMethod]
    public async Task UnsavedDocumentRetainsItsVerifiedSavedOutput()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var hash = Convert.ToHexString(SHA256.HashData(await File.ReadAllBytesAsync(project.SourcePath, this.TestContext.CancellationToken).ConfigureAwait(false)));
        using var registration = project.Documents.Register(project.SourcePath, token => Task.FromResult<CookDocumentReadLease?>(
            new(new CookDocumentState(Guid.NewGuid(), project.SourcePath, "Material", 2, 1, IsDirty: true, hash), static () => { })));
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.Current);
        _ = status.HasUnsavedChanges.Should().BeTrue();
        _ = status.HasAvailableOutput.Should().BeTrue();
    }

    /// <summary>Changed source remains unvalidated until cooking and retains its prior usable output.</summary>
    /// <returns>The asynchronous invalid-source regression.</returns>
    [TestMethod]
    public async Task UnvalidatedSourceChangeKeepsPriorOutputFacts()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(project.SourcePath, "invalid json", this.TestContext.CancellationToken).ConfigureAwait(false);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = status.HasAvailableOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().BeEmpty();
    }

    /// <summary>New descriptors do not require native availability merely to report Needs cooking.</summary>
    /// <returns>The asynchronous uncooked-source regression.</returns>
    [TestMethod]
    public async Task NewSourceNeedsCookingWithoutInvokingTheNativeProducer()
    {
        using var project = new StatusProject();
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        var status = (await project.CreateReader(native.Object).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = status.HasPublishedOutput.Should().BeFalse();
        _ = status.Diagnostics.Should().BeEmpty();
        native.VerifyNoOtherCalls();
    }

    /// <summary>Built-ins without project contributions have no missing descriptor or native-tool requirement.</summary>
    /// <returns>The asynchronous engine-identity inspection regression.</returns>
    [TestMethod]
    public async Task BuiltinsWithoutProjectOutputsDoNotRequireSourceDescriptors()
    {
        using var project = new StatusProject();
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        Uri[] identities = [Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("BasicShapes/Cube"), Oxygen.Managed.Core.AssetUris.BuildGeneratedUri("Materials/Default")];
        var statuses = await project.CreateReader(native.Object).ReadAsync(project.Project, identities, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = statuses.Should().HaveCount(2).And.OnlyContain(status => !status.HasPublishedOutput && status.SourcePaths.IsEmpty && status.Diagnostics.IsEmpty);
        native.VerifyNoOtherCalls();
    }

    /// <summary>Unchanged geometry becomes out of date when its material changes or loses published bytes.</summary>
    /// <param name="damageOutput">Whether to damage output instead of changing material source.</param>
    /// <returns>The asynchronous dependency regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task DependencyChangesAffectConsumerStatus(bool damageOutput)
    {
        using var project = new StatusProject();
        var catalog = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var geometry = catalog["geometries"]![0]!["descriptor"]!;
        geometry["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Content/Material.omat";
        var uri = new Uri("asset:///Content/Geometry.ogeo.json");
        await File.WriteAllTextAsync(Path.Combine(project.Root, "Content", "Geometry.ogeo.json"), geometry.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await project.PublishAsync(this.TestContext.CancellationToken, uri, [StatusProject.SourceUri]).ConfigureAwait(false);
        var reader = project.CreateReader();
        var before = (await reader.ReadAsync(project.Project, [uri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = before.Freshness.Should().Be(AssetCookFreshness.Current);
        if (damageOutput)
        {
            File.Delete(project.OutputPath);
        }
        else
        {
            await File.WriteAllTextAsync(project.SourcePath, StatusProject.CreateMaterialJson(0.8f), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        var after = (await reader.ReadAsync(project.Project, [uri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = after.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = after.HasPublishedOutput.Should().BeTrue();
        _ = after.HasAvailableOutput.Should().Be(!damageOutput);
    }

    /// <summary>A producer invalidated during status evaluation cannot receive a stale Current result.</summary>
    /// <returns>The asynchronous concurrent-input regression.</returns>
    [TestMethod]
    public async Task ProducerChangedDuringStatusCannotBeCurrent()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var native = new Mock<INativeCompatibilityService>(MockBehavior.Strict);
        var verified = await project.VerifyNativeAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var artifacts = verified.Artifacts!;
        _ = native.SetupSequence(service => service.Observation)
            .Returns(new NativeProducerObservation(1, artifacts.Fingerprint, []))
            .Returns(new NativeProducerObservation(2, null, []));
        var status = (await project.CreateReader(native.Object).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.Unknown);
        _ = status.HasAvailableOutput.Should().BeTrue();
        native.Verify(service => service.VerifyAsync(It.IsAny<Guid>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    /// <summary>An unavailable native producer does not erase prior output or pretend it is current.</summary>
    /// <returns>The asynchronous native-unavailability regression.</returns>
    [TestMethod]
    public async Task UnavailableProducerRetainsPriorOutputFacts()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(project.Root, "producer.bin"), "changed producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await project.VerifyNativeAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.Unknown);
        _ = status.HasPublishedOutput.Should().BeTrue();
        _ = status.HasAvailableOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().NotBeEmpty();
    }

    /// <summary>A different valid native build requires a recook without invalidating the prior bytes.</summary>
    /// <returns>The asynchronous producer-identity regression.</returns>
    [TestMethod]
    public async Task ChangedProducerRequiresCooking()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        var producer = Path.Combine(project.Root, "producer.bin");
        await File.WriteAllTextAsync(producer, "new producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var native = new Oxygen.Testing.TemporaryNativeArtifacts([new NativeArtifactLocation("producer", producer)]);
        var verifiedProducer = await native.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        await using var producerLifetime = verifiedProducer.Artifacts!;
        var status = (await project.CreateReader(native).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = status.HasAvailableOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().BeEmpty();
    }

    /// <summary>A deleted source retains an unsaved owner's identity and prior output facts.</summary>
    /// <returns>The asynchronous missing-source regression.</returns>
    [TestMethod]
    public async Task MissingSourcePreservesDirtyOwnerAndPriorOutput()
    {
        using var project = new StatusProject();
        await project.PublishAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        using var owner = project.Documents.Register(project.SourcePath, _ => Task.FromResult<global::Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentReadLease?>(new(new(Guid.NewGuid(), project.SourcePath, "Material", 2, 1, IsDirty: true, new string('A', 64)), static () =>
{ })));
        File.Delete(project.SourcePath);
        var status = await project.ReadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = status.Freshness.Should().Be(AssetCookFreshness.MissingSource);
        _ = status.HasUnsavedChanges.Should().BeTrue();
        _ = status.HasAvailableOutput.Should().BeTrue();
    }

    private sealed partial class StatusProject : IDisposable
    {
        public static readonly Uri SourceUri = new("asset:///Content/Material.omat.json");
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-status-");
        private readonly NativeAtomicFileStore files = new(new Testably.Abstractions.RealFileSystem());
        private readonly Oxygen.Testing.TemporaryNativeArtifacts native;
        private readonly CookPublicationService publication;
        private readonly Mock<IEngineContentPipelineApi> api = new();
        private readonly ProjectManagerService manager = new(new NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));
        private string? selectedRoot;
        public StatusProject()
        {
            this.Project = new ProjectContext
            {
                ProjectId = Guid.NewGuid(),
                ProjectRoot = this.Root,
                Name = "Status",
                Category = Category.Games,
                AuthoringMounts = [new("Content", "Content")],
                LocalFolderMounts = [],
                Scenes = [],
            };
            Directory.CreateDirectory(Path.GetDirectoryName(this.SourcePath)!);
            File.WriteAllText(this.SourcePath, MaterialJson);
            var producer = Path.Combine(this.Root, "producer.bin");
            File.WriteAllText(producer, "producer");
            this.native = new([new NativeArtifactLocation("producer", producer)]);
            this.api.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((string root, NativeArtifactLease? _, CancellationToken _) => Oxygen.Testing.NativeInventoryFixture.Read(root));
            var info = new ProjectInfo(this.Project.ProjectId, this.Project.Name, this.Project.Category, this.Root)
            {
                AuthoringMounts = [.. this.Project.AuthoringMounts],
            };
            File.WriteAllText(Path.Combine(this.Root, "Project.oxy"), ProjectInfo.ToJson(info));
            this.publication = new(Mock.Of<IContentCookCoordinator>(), Mock.Of<IProjectContextService>(), this.files, this.manager);
        }

        public static string MaterialJson => CreateMaterialJson(0.5f);

        public string Root => this.directory.FullName;

        public string SourcePath => Path.Combine(this.Root, "Content", "Material.omat.json");

        public string OutputPath => Path.Combine(this.selectedRoot ?? throw new InvalidOperationException("Publish the fixture first."), "Material.omat");

        public ProjectContext Project { get; }

        public CookDocumentRegistry Documents { get; } = new();

        public static string CreateMaterialJson(float roughness)
        {
            using var buffer = new MemoryStream();
            MaterialSourceWriter.Write(buffer, new MaterialSource(
                "Material",
                new MaterialPbrMetallicRoughness(1, 1, 1, 1, 0, roughness, baseColorTexture: null, metallicRoughnessTexture: null),
                normalTexture: null,
                occlusionTexture: null,
                MaterialAlphaMode.Opaque,
                0.5f,
                doubleSided: false));
            return Encoding.UTF8.GetString(buffer.ToArray());
        }

        public AssetCookStatusReader CreateReader(INativeCompatibilityService? compatibility = null)
            => new(this.Documents, this.publication, compatibility ?? this.native);

        public async Task<AssetCookStatus> ReadAsync(CancellationToken cancellationToken)
            => (await this.CreateReader().ReadAsync(this.Project, [SourceUri], cancellationToken).ConfigureAwait(false)).Single();
        public async Task PublishAsync(CancellationToken cancellationToken, Uri? primary = null, IReadOnlyList<Uri>? dependencies = null)
        {
            var operation = new ContentCookOperation(Guid.NewGuid(), this.Project, 1);
            var input = CookInputResolver.Resolve(this.Project, primary ?? SourceUri, ContentCookInputRole.Primary);
            var facts = new StatusSourceFacts(this.Project, input, dependencies ?? []);
            var graph = await new CookDependencyDiscovery(facts).DiscoverAsync(this.Project, [input], cancellationToken).ConfigureAwait(false);
            _ = graph.Diagnostics.Should().BeEmpty();
            var verified = await this.native.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
            var producer = verified.Artifacts!;
            await using var producerLifetime = producer.ConfigureAwait(false);
            _ = verified.Succeeded.Should().BeTrue();
            var plan = CookIncrementalPlanner.CreatePlan(producer.Fingerprint, graph.Files, graph, new(this.Project.ProjectId, [], []), ImmutableDictionary<string, global::Oxygen.Editor.ContentPipeline.Inspection.CookedInventoryReport>.Empty);
            using var baseline = await this.publication.AcquireReadAsync(this.Project, cancellationToken).ConfigureAwait(false);
            using var ownership = CookOutputLease.AcquireOperation(this.Root, operation.OperationId);
            await using var staging = await CookStagingArea.CreateAsync(operation, baseline, ["Content"], this.files, this.manager, cancellationToken).ConfigureAwait(false);
            var candidate = staging.Roots.Single();
            var root = candidate.Path;
            await File.WriteAllTextAsync(Path.Combine(root, "container.index.bin"), "index", cancellationToken).ConfigureAwait(false);
            var outputs = graph.Assets.Select(asset => new ContentCookedAsset(asset.AssetUri, new Uri("asset://" + asset.OutputVirtualPath), asset.Kind, "Content", asset.OutputVirtualPath ?? throw new InvalidOperationException("Expected a cooked path."))).ToArray();
            var indexed = new List<CookedAssetEntry>();
            foreach (var output in outputs)
            {
                var relative = output.VirtualPath["/Content/".Length..];
                var path = Path.Combine(root, relative);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                await File.WriteAllTextAsync(path, "original", cancellationToken).ConfigureAwait(false);
                indexed.Add(new CookedAssetEntry(output.VirtualPath, output.Kind) { DescriptorRelativePath = relative });
            }

            Oxygen.Testing.NativeInventoryFixture.WriteIndex(root, indexed, candidate.SourceKey);
            var inventory = Oxygen.Testing.NativeInventoryFixture.Read(root);
            var evidence = new CookProvenance.Root("Content", inventory.SourceKey, inventory.IndexSha256);
            var provenance = new CookProvenance(
                this.Project.ProjectId,
                [evidence],
                [
                    .. outputs.Select(output => new CookProvenance.Product(output.SourceAssetUri, plan.Fingerprints[output.SourceAssetUri], graph.Dependencies[output.SourceAssetUri],
                        [new(output with { DescriptorRelativePath = output.VirtualPath["/Content/".Length..] }, "Content")])
                        {
                            ReuseFingerprint = plan.Fingerprints[output.SourceAssetUri],
                            SourceInput = graph.Assets.Single(input => input.AssetUri == output.SourceAssetUri),
                            SourceFiles = [.. graph.Files.Where(file => graph.FileDependencies[output.SourceAssetUri].Contains(file.RelativePath, StringComparer.Ordinal))],
                            DeclaredOutputs = graph.SourceFacts[output.SourceAssetUri].Outputs,
                            NativeReferences = graph.SourceFacts[output.SourceAssetUri].References,
                        }),
                ]);
            var opening = await CookOutputReadLease.AcquireAsync(root, cancellationToken).ConfigureAwait(false);
            candidate.AcceptVerification(opening, inventory);
            var document = new CookPublicationDocument(CookPublicationDocument.CurrentVersion, this.Project.ProjectId, operation.OperationId,
                DateTimeOffset.UtcNow, CookPublicationDocument.ConfigurationIdentity(this.Project), staging.SealRoots(), provenance.Products,
                new(producer.Fingerprint, new string('A', 64), graph.Files, [], [], []));
            await staging.Transaction.PrepareAsync(operation, document, sourceReplacement: null, [], projectChange: null, cancellationToken).ConfigureAwait(false);
            staging.RetainForPublication();
            using var accepted = await staging.Transaction.PublishAsync(preview: null, baseline, static () => { }, cancellationToken).ConfigureAwait(false);
            this.selectedRoot = accepted.FindProjectRoot("Content");
        }

        public void Dispose()
        {
            this.native.Dispose();
            this.directory.Delete(recursive: true);
        }

        private sealed class StatusSourceFacts(ProjectContext project, ContentCookInput primary, IReadOnlyList<Uri> dependencies) : ICookSourceFactsProvider
        {
            public Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
            {
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(new CookSourceFrontier([.. inputs.Select(this.Describe)], [], [], []));
            }

            private CookSourceFacts Describe(ContentCookInput source)
                => new(source,
                    [new(source.OutputVirtualPath ?? throw new InvalidOperationException("The fixture requires a descriptor output."),
                        source.Kind.ToString().ToLowerInvariant(), "/", Required: true)],
                    source.AssetUri == primary.AssetUri
                        ? [.. dependencies.Select(uri => new global::Oxygen.Editor.ContentPipeline.Import.NativeLogicalDependency(
                            CookInputResolver.Resolve(project, uri, ContentCookInputRole.Dependency).OutputVirtualPath
                                ?? throw new InvalidOperationException("The fixture requires a material output."),
                            "material", "/", Required: true))]
                        : [],
                    [new(source.AssetUri, source.SourceAbsolutePath, source.SourceRelativePath,
                        Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source.SourceAbsolutePath))))]);
        }

        public Task<NativeCompatibilityResult> VerifyNativeAsync(Guid operation, CancellationToken token) => this.native.VerifyAsync(operation, token);
    }
}
