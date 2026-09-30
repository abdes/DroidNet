// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Authoring.Materials;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Checks status against real publication transactions with controlled product bytes.</summary>
[TestClass]
[SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed partial class AssetCookStatusReaderTests
{
    /// <summary>Gets or sets the running test context.</summary>
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
                ProjectId = Guid.NewGuid(), ProjectRoot = this.Root, Name = "Status", Category = Category.Games,
                AuthoringMounts = [new("Content", "Content")], LocalFolderMounts = [], Scenes = [],
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

        public async Task PublishAsync(CancellationToken cancellationToken, Uri? primary = null)
        {
            var operation = new ContentCookOperation(Guid.NewGuid(), this.Project, 1);
            var input = CookInputResolver.Resolve(this.Project, primary ?? SourceUri, ContentCookInputRole.Primary);
            var graph = await new CookDependencyDiscovery(new NativeSourceFactsFixture(this.Project, this.Documents)).DiscoverAsync(this.Project, [input], cancellationToken).ConfigureAwait(false);
            _ = graph.Diagnostics.Should().BeEmpty();
            var verified = await this.native.VerifyAsync(operation.OperationId, cancellationToken).ConfigureAwait(false);
            var producer = verified.Artifacts!;
            await using var producerLifetime = producer.ConfigureAwait(false);
            _ = verified.Succeeded.Should().BeTrue();
            var plan = CookIncrementalPlanner.CreatePlan(producer.Fingerprint, graph.Files, graph,
                new(this.Project.ProjectId, [], []), ImmutableDictionary<string, Inspection.CookedInventoryReport>.Empty);
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
    }
}
