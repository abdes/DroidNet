// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Incremental;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

/// <summary>
/// Each identity has one producing source. A cook that finds a second source producing an identity, or one differing
/// only in case, refuses both sources with <see cref="AssetCookDiagnosticCodes.IdentityCollision"/>.
/// </summary>
[TestClass]
public sealed class ProducerCollisionTests : IDisposable
{
    private static readonly AssetPath Box = AssetPath.Parse("/Content/Geometry/Box.ogeo");
    private static readonly AssetPath Crate = AssetPath.Parse("/Content/Geometry/Crate.ogeo");

    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-producer-collision-tests", Guid.NewGuid().ToString("N"));
    private readonly ProjectContext project;

    public ProducerCollisionTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Geometry"));
        File.WriteAllText(Path.Combine(this.root, "Content", "Geometry", "Box.ogeo.json"), "{}");
        File.WriteAllText(Path.Combine(this.root, "Content", "Geometry", "Crate.ogeo.json"), "{}");
        this.project = ProjectContext.FromProject(new Project(new ProjectInfo("Test", Category.Games, this.root)
        {
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        }));
    }

    public TestContext TestContext { get; set; } = null!;

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    [DataRow("/Content/Geometry/Box.obuf", DisplayName = "Same identity")]
    [DataRow("/Content/Geometry/box.obuf", DisplayName = "Identity differing only in case")]
    public async Task TwoSourcesOfOneIdentityAreBothRefused(string crateBuffer)
    {
        var facts = new Facts(new Dictionary<AssetPath, string> { [Box] = "/Content/Geometry/Box.obuf", [Crate] = crateBuffer });

        var graph = await new CookDependencyDiscovery(facts).DiscoverAsync(this.project, [this.Input(Box), this.Input(Crate)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().AllSatisfy(static diagnostic => diagnostic.Code.Should().Be(AssetCookDiagnosticCodes.IdentityCollision))
            .And.Subject.Select(static diagnostic => Path.GetFileName(diagnostic.AffectedPath)).Should().BeEquivalentTo("Box.ogeo.json", "Crate.ogeo.json");
    }

    [TestMethod]
    public async Task AnIdentityAnAcceptedSourceStillProducesIsRefused()
    {
        var facts = new Facts(new Dictionary<AssetPath, string> { [Crate] = "/Content/Geometry/Box.obuf" });
        var accepted = this.Accepted(Box, "/Content/Geometry/Box.obuf");

        var graph = await new CookDependencyDiscovery(facts, resolveProducer: accepted.CreateProducerLookup(this.project))
            .DiscoverAsync(this.project, [this.Input(Crate)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Select(static diagnostic => diagnostic.Code).Should().Equal(AssetCookDiagnosticCodes.IdentityCollision, AssetCookDiagnosticCodes.IdentityCollision);
    }

    [TestMethod]
    public async Task AnIdentityOfASourceNoLongerInTheProjectIsFree()
    {
        var facts = new Facts(new Dictionary<AssetPath, string> { [Crate] = "/Content/Geometry/Box.obuf" });
        var accepted = this.Accepted(Box, "/Content/Geometry/Box.obuf");
        File.Delete(Path.Combine(this.root, "Content", "Geometry", "Box.ogeo.json"));

        var graph = await new CookDependencyDiscovery(facts, resolveProducer: accepted.CreateProducerLookup(this.project))
            .DiscoverAsync(this.project, [this.Input(Crate)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().BeEmpty("a source that was moved or deleted produces nothing any more");
    }

    [TestMethod]
    public async Task ASourceRecookingItsOwnIdentityIsNotACollision()
    {
        var facts = new Facts(new Dictionary<AssetPath, string> { [Box] = "/Content/Geometry/Box.obuf" });
        var accepted = this.Accepted(Box, "/Content/Geometry/Box.obuf");

        var graph = await new CookDependencyDiscovery(facts, resolveProducer: accepted.CreateProducerLookup(this.project))
            .DiscoverAsync(this.project, [this.Input(Box)], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().BeEmpty();
    }

    [TestMethod]
    public async Task TwoFilesBesideEachOtherOfOneIdentityAreBothRefused()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Player"));
        File.WriteAllText(Path.Combine(this.root, "Content", "Player", "move.lua"), "return {}");
        File.WriteAllText(Path.Combine(this.root, "Content", "Player", "move.luau"), "return {}");
        var script = this.Input(AssetPath.Parse("/Content/Player/move.oscript"));

        var graph = await new CookDependencyDiscovery(new Facts(new Dictionary<AssetPath, string>()))
            .DiscoverAsync(this.project, [script], this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = graph.Diagnostics.Should().AllSatisfy(static diagnostic => diagnostic.Code.Should().Be(AssetCookDiagnosticCodes.IdentityCollision))
            .And.Subject.Select(static diagnostic => Path.GetFileName(diagnostic.AffectedPath)).Should().BeEquivalentTo("move.lua", "move.luau");
    }

    private ContentCookInput Input(AssetPath identity) => CookInputResolver.Resolve(this.project, identity, ContentCookInputRole.Primary);

    private CookProvenance Accepted(AssetPath source, string buffer)
        => new(
            this.project.ProjectId,
            [],
            [
                new CookProvenance.Product(source, "fingerprint", [], [])
                {
                    ReuseFingerprint = "fingerprint",
                    SourceInput = this.Input(source),
                    DeclaredOutputs = [new NativeLogicalDependency(source.VirtualPath, "geometry", "/", Required: true), new NativeLogicalDependency(buffer, "buffer", "/", Required: true)],
                },
            ]);

    // Each source produces itself, and a geometry one buffer.
    private sealed class Facts(IReadOnlyDictionary<AssetPath, string> buffers) : ICookSourceFactsProvider
    {
        public Task<CookSourceFrontier> ReadAsync(IReadOnlyList<ContentCookInput> inputs, CancellationToken cancellationToken)
            => Task.FromResult(new CookSourceFrontier([.. inputs.Select(this.Describe)], [], []));

        private CookSourceFacts Describe(ContentCookInput source)
        {
            List<NativeLogicalDependency> outputs = [new(source.AssetUri.VirtualPath, "asset", "/", Required: true)];
            if (buffers.TryGetValue(source.AssetUri, out var buffer))
            {
                outputs.Add(new(buffer, "buffer", "/", Required: true));
            }

            return new(source, [.. outputs], [], [new(source.AssetUri, source.SourceAbsolutePath, source.SourceRelativePath, new string('A', 64))]);
        }
    }
}
