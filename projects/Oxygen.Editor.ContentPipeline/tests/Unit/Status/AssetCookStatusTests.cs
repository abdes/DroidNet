// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Status;

[TestClass]
internal sealed partial class AssetCookStatusTests
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

    /// <summary>An asset that cannot be a cook input is invalid on its own; the other assets are read as usual.</summary>
    /// <returns>The asynchronous isolation regression.</returns>
    [TestMethod]
    public async Task UnresolvableAssetIsInvalidWithoutFailingTheOthers()
    {
        using var project = new StatusProject();
        var outside = AssetPath.Parse("/Outside/Material.omat");

        var statuses = await project.CreateReader().ReadAsync(project.Project, [StatusProject.SourceUri, outside], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = statuses.Single(status => status.AssetUri == StatusProject.SourceUri).Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        var invalid = statuses.Single(status => status.AssetUri == outside);
        _ = invalid.Freshness.Should().Be(AssetCookFreshness.InvalidSource);
        _ = invalid.Diagnostics.Should().ContainSingle().Which.Code.Should().Be("asset_status.input_unresolved");
    }

    /// <summary>
    /// An action is named under its input source and reads the source's status under its own identity, rather than
    /// being an asset the cook cannot resolve.
    /// </summary>
    /// <returns>The asynchronous container status regression.</returns>
    [TestMethod]
    public async Task AProductReadsItsContainersStatus()
    {
        using var project = new StatusProject();
        await File.WriteAllTextAsync(Path.Combine(project.Root, "Content", "Player.input.json"), """{ "actions": [ { "name": "Jump", "type": "bool" } ] }""", this.TestContext.CancellationToken).ConfigureAwait(false);
        var jump = AssetPath.Parse("/Content/Player.input/Jump.oiact");

        var status = (await project.CreateReader().ReadAsync(project.Project, [jump], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();

        _ = status.AssetUri.Should().Be(jump);
        _ = status.Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = status.HasPublishedOutput.Should().BeFalse();
        _ = status.Diagnostics.Should().BeEmpty();
    }

    /// <summary>
    /// A diagnostic names its asset by the virtual path the engine cooks, not by the escaped URI path, so an asset in a
    /// folder such as "New folder (2)" shows its real name and matches its asset.
    /// </summary>
    /// <param name="inProject">Whether the asset is a project source that is gone, rather than one outside the project.</param>
    /// <returns>The asynchronous path regression.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task DiagnosticNamesAnAssetInAFolderWithSpacesByItsVirtualPath(bool inProject)
    {
        using var project = new StatusProject();
        var path = inProject ? "/Content/New folder (2)/Wet Rubber.omat" : "/Outside/New folder (2)/Wet Rubber.omat";
        var uri = AssetPath.Parse(path);

        var statuses = await project.CreateReader().ReadAsync(project.Project, [uri], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = statuses.Single().Diagnostics.Should().NotBeEmpty().And.OnlyContain(diagnostic => diagnostic.AffectedVirtualPath == path);
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
        const string geometry = """
            {"$schema":"oxygen.geometry-descriptor.v1","name":"Geometry","bounds":{"min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},"lods":[{"name":"LOD0","mesh_type":"procedural","bounds":{"min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},"procedural":{"generator":"Cube","mesh_name":"Cube","params":{}},"submeshes":[{"name":"Main","material_ref":"/Content/Material.omat","slot_id":"146b1397-c3e0-bc20-fce3-c55e470b4086","views":[{"view_ref":"__all__"}]}]}]}
            """;
        var uri = AssetPath.Parse("/Content/Geometry.ogeo");
        await File.WriteAllTextAsync(Path.Combine(project.Root, "Content", "Geometry.ogeo.json"), geometry, this.TestContext.CancellationToken).ConfigureAwait(false);
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
        var artifacts = verified.Artifacts!;
        await using var artifactsLifetime = artifacts.ConfigureAwait(false);
        _ = native.SetupSequence(service => service.Observation)
            .Returns(new NativeProducerObservation(1, artifacts.Fingerprint, []))
            .Returns(new NativeProducerObservation(2, Fingerprint: null, []));
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
        using var native = new Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts([new NativeArtifactLocation("producer", producer)]);
        var verifiedProducer = await native.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var producerArtifacts = verifiedProducer.Artifacts!;
        await using var producerLifetime = producerArtifacts.ConfigureAwait(false);
        var status = (await project.CreateReader(native).ReadAsync(project.Project, [StatusProject.SourceUri], this.TestContext.CancellationToken).ConfigureAwait(false)).Single();
        _ = status.Freshness.Should().Be(AssetCookFreshness.OutOfDate);
        _ = status.HasAvailableOutput.Should().BeTrue();
        _ = status.Diagnostics.Should().BeEmpty();
    }

    private sealed partial class StatusProject : IDisposable
    {
        public static readonly AssetPath SourceUri = AssetPath.Parse("/Content/Material.omat");
        private readonly DirectoryInfo directory = Directory.CreateTempSubdirectory("oxygen-status-");
        private readonly NativeAtomicFileStore files = new(new Testably.Abstractions.RealFileSystem());
        private readonly Oxygen.Managed.Core.Testing.TemporaryNativeArtifacts native;
        private readonly EngineContentPakFixture engineContent = new();
        private readonly CookPublicationService publication;
        private readonly Mock<IEngineContentPipelineApi> api = new();
        private readonly ProjectManagerService manager = new(new NativeStorageProvider(new Testably.Abstractions.RealFileSystem()));
        private string? selectedRoot;

        public StatusProject()
        {
            this.Project = TestProject.Create(new Testably.Abstractions.RealFileSystem(), this.Root).Context with { Name = "Status" };
            Directory.CreateDirectory(Path.GetDirectoryName(this.SourcePath)!);
            File.WriteAllText(this.SourcePath, MaterialJson);
            var producer = Path.Combine(this.Root, "producer.bin");
            File.WriteAllText(producer, "producer");
            this.native = new([new NativeArtifactLocation("producer", producer)]);
            _ = this.api.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((string root, NativeArtifactLease? _, CancellationToken _) => Oxygen.Testing.NativeInventoryFixture.Read(root));
            _ = TestProject.SaveProjectFile(this.Project);
            this.publication = new(Mock.Of<IContentCookCoordinator>(), new TestSession(this.Project), this.files, this.manager, this.engineContent);
        }

        public static string MaterialJson => CreateMaterialJson(0.5f);

        public string Root => this.directory.FullName;

        public string SourcePath => Path.Combine(this.Root, "Content", "Material.omat.json");

        public string OutputPath => Path.Combine(this.selectedRoot ?? throw new InvalidOperationException("Publish the fixture first."), "Material.omat");

        public ProjectContext Project { get; }

        public static string CreateMaterialJson(float roughness)
        {
            using var buffer = new MemoryStream();
            MaterialSourceWriter.Write(buffer, new MaterialSource(
                "Material",
                new MaterialPbrMetallicRoughness(1, 1, 1, 1, 0, roughness),
                MaterialAlphaMode.Opaque,
                0.5f,
                doubleSided: false));
            return Encoding.UTF8.GetString(buffer.ToArray());
        }

        public AssetCookStatusReader CreateReader(INativeCompatibilityService? compatibility = null)
            => new(this.publication, compatibility ?? this.native, this.engineContent);

        public async Task<AssetCookStatus> ReadAsync(CancellationToken cancellationToken)
            => (await this.CreateReader().ReadAsync(this.Project, [SourceUri], cancellationToken).ConfigureAwait(false)).Single();

        public async Task PublishAsync(CancellationToken cancellationToken, AssetPath? primary = null, IReadOnlyList<AssetPath>? dependencies = null)
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
            var plan = CookIncrementalPlanner.CreatePlan(producer.Fingerprint, graph.Files, graph, new(this.Project.ProjectId, [], []), []);
            using var baseline = await this.publication.AcquireReadAsync(this.Project, cancellationToken).ConfigureAwait(false);
            await using var ownership = CookOutputLease.AcquireOperation(this.Project.ProjectRoot, operation.OperationId).ConfigureAwait(false);
            var staging = await CookStagingArea.CreateAsync(operation, baseline, ["Content"], this.files, this.manager, cancellationToken).ConfigureAwait(false);
            await using var stagingLifetime = staging.ConfigureAwait(false);
            var candidate = staging.Roots.Single();
            var root = candidate.Path;
            await File.WriteAllTextAsync(Path.Combine(root, "container.index.bin"), "index", cancellationToken).ConfigureAwait(false);
            var outputs = graph.Assets.Select(asset =>
            {
                var cooked = asset.AssetUri.VirtualPath;
                return new ContentCookedAsset(asset.AssetUri, AssetPath.Parse(cooked), asset.Kind, "Content", cooked);
            }).ToArray();
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
            var provenance = this.CreateProvenance(outputs, plan, graph, inventory);
            var opening = await CookOutputReadLease.AcquireAsync(root, cancellationToken).ConfigureAwait(false);
            candidate.AcceptVerification(opening, inventory);
            var document = new CookPublicationDocument(
                CookPublicationDocument.CurrentVersion,
                this.Project.ProjectId,
                operation.OperationId,
                DateTimeOffset.UtcNow,
                CookPublicationDocument.ConfigurationIdentity(this.Project),
                staging.SealRoots(),
                provenance.Products,
                new(producer.Fingerprint, new string('A', 64), graph.Files, [], []));
            await staging.Transaction.PrepareAsync(operation, document, sourceReplacement: null, [], projectChange: null, cancellationToken).ConfigureAwait(false);
            staging.RetainForPublication();
            using var accepted = await staging.Transaction.PublishAsync(preview: null, baseline, static () => { }, cancellationToken).ConfigureAwait(false);
            this.selectedRoot = accepted.FindProjectRoot("Content")?.FullPath;
        }

        public Task<NativeCompatibilityResult> VerifyNativeAsync(Guid operation, CancellationToken token) => this.native.VerifyAsync(operation, token);

        public void Dispose()
        {
            this.native.Dispose();
            this.directory.Delete(recursive: true);
        }

        private CookProvenance CreateProvenance(ContentCookedAsset[] outputs, CookIncrementalPlan plan, CookDependencyGraph graph, ContentPipeline.Inspection.CookedInventoryReport inventory)
            => new(
                this.Project.ProjectId,
                [new CookProvenance.Root("Content", inventory.SourceKey, inventory.IndexSha256)],
                [
                    .. outputs.Select(output => new CookProvenance.Product(
                        output.SourceAssetUri,
                        plan.Fingerprints[output.SourceAssetUri],
                        graph.Dependencies[output.SourceAssetUri],
                        [new(output with { DescriptorRelativePath = output.VirtualPath["/Content/".Length..] }, "Content")])
                        {
                            ReuseFingerprint = plan.Fingerprints[output.SourceAssetUri],
                            SourceInput = graph.Assets.Single(input => input.AssetUri == output.SourceAssetUri),
                            SourceFiles = [.. graph.Files.Where(file => graph.FileDependencies[output.SourceAssetUri].Contains(file.RelativePath, StringComparer.Ordinal))],
                            DeclaredOutputs = graph.SourceFacts[output.SourceAssetUri].Outputs,
                            NativeReferences = graph.SourceFacts[output.SourceAssetUri].References,
                        }),
                ]);

        private sealed class StatusSourceFacts(ProjectContext project, ContentCookInput primary, IReadOnlyList<AssetPath> dependencies) : ICookSourceFactsProvider
        {
            public Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
            {
                cancellationToken.ThrowIfCancellationRequested();
                return Task.FromResult(new CookSourceFrontier([.. inputs.Select(this.Describe)], [], []));
            }

            private static string KindName(ContentCookAssetKind kind) => kind switch
            {
                ContentCookAssetKind.Material => "material",
                ContentCookAssetKind.Geometry => "geometry",
                ContentCookAssetKind.Texture => "texture",
                ContentCookAssetKind.Scene => "scene",
                _ => throw new InvalidOperationException($"The fixture does not describe {kind} sources."),
            };

            private CookSourceFacts Describe(ContentCookInput source)
            {
                var output = source.AssetUri.VirtualPath;
                ContentPipeline.Import.NativeLogicalDependency[] references = source.AssetUri == primary.AssetUri
                    ? [.. dependencies.Select(uri => new global::Oxygen.Editor.ContentPipeline.Import.NativeLogicalDependency(
                        CookInputResolver.Resolve(project, uri, ContentCookInputRole.Dependency).AssetUri.VirtualPath,
                        "material",
                        "/",
                        Required: true))]
                    : [];
                var hash = Convert.ToHexString(SHA256.HashData(File.ReadAllBytes(source.SourceAbsolutePath.FullPath)));
                return new(
                    source,
                    [new(output, KindName(source.Kind), "/", Required: true)],
                    [.. references],
                    [new(source.AssetUri, source.SourceAbsolutePath, source.SourceRelativePath, hash)]);
            }
        }
    }
}
