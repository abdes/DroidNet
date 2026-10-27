// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.Models;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.ModelEditor.Tests;

/// <summary>
/// A model's facts (model editor 5.4): its source, its last import from its scene product's cook state, the counts of
/// its source file from the source analysis, and the most LODs of the geometries its last import produced.
/// </summary>
[TestClass]
public sealed class ModelFactsReaderTests
{
    private static readonly AssetPath Car = AssetPath.Parse("/Content/Car.glb");
    private static readonly AssetPath Body = AssetPath.Parse("/Content/Car/Body.ogeo");
    private static readonly AssetPath Wheel = AssetPath.Parse("/Content/Car/Wheel.ogeo");

    private static readonly ModelSourceFacts SourceFacts = new()
    {
        Format = "glb",
        Right = "+X",
        Up = "+Y",
        Front = "+Z",
        NodeCount = 3,
        MeshCount = 2,
        Materials = ["Paint"],
        Textures = [],
    };

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task ReadAsync_ImportedModel_ReadsItsSourceItsImportAndItsLods()
    {
        using var harness = new Harness(complete: true);
        harness.Surfaces.Publish(MetadataOf(Body, lods: 4));
        harness.Surfaces.Publish(MetadataOf(Wheel, lods: 2));

        var facts = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = facts.Source.Should().Be(Car);
        _ = facts.LastImport.Should().Be(ModelImportStatus.TextOf(ModelImportState.Imported));
        _ = facts.SourceFacts.Should().BeSameAs(SourceFacts);
        _ = facts.LodCount.Should().Be(4);
    }

    [TestMethod]
    public async Task ReadAsync_SourceNotRead_LeavesItsCountsOut()
    {
        using var harness = new Harness(complete: false);

        var facts = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = facts.SourceFacts.Should().BeNull();
    }

    [TestMethod]
    public async Task ReadAsync_NoImportedGeometry_LeavesTheLodsOut()
    {
        using var harness = new Harness(complete: true);

        var facts = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = facts.LodCount.Should().BeNull();
    }

    [TestMethod]
    public async Task ReadAsync_AgainAndAgain_AnalyzesTheSourceOnce()
    {
        using var harness = new Harness(complete: true);

        _ = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);
        var again = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = again.SourceFacts.Should().BeSameAs(SourceFacts);
        harness.Analyzer.Verify(value => value.AnalyzeAsync(It.IsAny<ProjectContext>(), It.IsAny<string>(), It.IsAny<CancellationToken>()), Times.Once(), "the facts change only when the model is imported again");
    }

    [TestMethod]
    public async Task ReadAsync_AfterTheModelIsImportedAgain_ReadsItsFactsAgain()
    {
        using var harness = new Harness(complete: true);
        _ = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        harness.Structures.Publish(harness.StructureOf(Body));
        harness.Structures.NotifyChanged();
        harness.Surfaces.Publish(MetadataOf(Body, lods: 5));
        var reimported = await harness.Reader.ReadAsync(Car, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = reimported.LodCount.Should().Be(5);
        harness.Analyzer.Verify(value => value.AnalyzeAsync(It.IsAny<ProjectContext>(), It.IsAny<string>(), It.IsAny<CancellationToken>()), Times.Exactly(2));
    }

    private static GeometrySurfaceMetadata MetadataOf(AssetPath geometry, int lods)
        => new(
            geometry,
            Guid.NewGuid(),
            "1",
            [new(Guid.NewGuid(), "Surface", Visible: true, CastsShadows: true, [.. Enumerable.Range(0, lods).Select(static lod => new GeometrySurfaceBinding((uint)lod, 0, Guid.Empty))])],
            SceneSchema.Geometry.Defaults.LodPolicy);

    // A project holding Car.glb, imported, whose instance renders Body and Wheel.
    private sealed class Harness : IDisposable
    {
        private readonly TestSession session;

        public Harness(bool complete)
        {
            var project = TestProject.Create(new MockFileSystem());
            var path = project.PathOf(Car.ToString());
            _ = project.Context.FileSystem.Directory.CreateDirectory(project.Context.FileSystem.Path.GetDirectoryName(path.FullPath)!);
            project.Context.FileSystem.File.WriteAllBytes(path.FullPath, "glTF"u8.ToArray());
            this.session = new TestSession(project.Context);
            this.Structures.Publish(this.StructureOf(Body, Wheel));
            var analyzer = this.Analyzer;
            _ = analyzer.Setup(value => value.AnalyzeAsync(project.Context, path.FullPath, It.IsAny<CancellationToken>()))
                .ReturnsAsync(new ModelSourceAnalysis(complete, complete ? SourceFacts : null, []));
            var product = AssetFileTypes.SceneProductOf(Car)!;
            IReadOnlyList<AssetEntry> assets =
            [
                new AssetEntry(product, product.Name, AssetKind.Scene, AssetState.Cooked, DerivedState: null, AssetRuntimeAvailability.NotApplicable, product.VirtualPath, SourcePath: null, DescriptorPath: null, CookedPath: null, AssetGuid: null, [], IsSelectable: true)
                {
                    CookStatus = new AssetCookStatus(product, AssetCookFreshness.Current, HasPublishedOutput: true, CookedOutputAvailability.Present, [], []),
                },
            ];
            var feed = Mock.Of<IAssetFeed>(value => value.Items == Observable.Return(assets));
            this.Reader = new ModelFactsReader(this.session, feed, analyzer.Object, this.Structures, this.Surfaces);
        }

        public FakeSceneStructures Structures { get; } = new();

        public Mock<IModelSourceAnalyzer> Analyzer { get; } = new();

        public FakeSurfaceProvider Surfaces { get; } = new();

        public IModelFacts Reader { get; }

        // Car as its import publishes it: a root, and a node rendering each geometry.
        public SceneStructure StructureOf(params AssetPath[] geometries)
        {
            var scene = new StructureScene(Guid.NewGuid(), Car.VirtualPath);
            var root = NodeId.New();
            return new SceneStructure(
                Car,
                scene,
                [
                    new StructureNode(NodePath.Of(root), "Car", Parent: null, scene, Instance: null, Geometry: null, HasCamera: false),
                    .. geometries.Select(geometry => new StructureNode(NodePath.Of(root, NodeId.New()), geometry.Name, NodePath.Of(root), scene, Instance: null, geometry, HasCamera: false)),
                ],
                []);
        }

        public void Dispose()
        {
            ((IDisposable)this.Reader).Dispose();
            this.Surfaces.Dispose();
            this.session.Dispose();
        }
    }
}
