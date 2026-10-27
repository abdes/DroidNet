// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reactive.Linq;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Documents;
using Oxygen.Editor.LivePreview.Content;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.LivePreview.Tests.Content;

/// <summary>
/// Checks that preview cooks follow what the open scene documents reference: every stale reference on open and reload,
/// each asset a change starts referencing, and a cancel only when no scene references the asset any more.
/// </summary>
[TestClass]
[TestCategory("Content Demand")]
public sealed partial class SceneContentDemandTests
{
    private static readonly AssetPath Geometry = AssetPath.Parse("/Content/Geometry/Custom.ogeo");
    private static readonly AssetPath Material = AssetPath.Parse("/Content/Materials/Custom.omat");

    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task OpenedScene_DemandsEveryStaleReferenceIncludingOverrideMaterials()
    {
        using var fixture = new DemandFixture();

        _ = await fixture.OpenAsync(withGeometry: true).ConfigureAwait(false);

        _ = fixture.Demanded.Should().BeEquivalentTo([Geometry, Material]);
    }

    [TestMethod]
    public async Task ReloadedScene_DemandsItsReferencesAgain()
    {
        using var fixture = new DemandFixture();
        var (document, built) = await fixture.OpenAsync(withGeometry: true).ConfigureAwait(false);
        fixture.CompleteAll();
        await Task.Delay(100, this.TestContext.CancellationToken).ConfigureAwait(false);
        fixture.Requests.Clear();

        fixture.Documents.Write(document.File, built.Dehydrate());
        _ = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = fixture.Demanded.Should().BeEquivalentTo([Geometry, Material]);
    }

    [TestMethod]
    public async Task AssignedGeometry_IsDemandedAndKeptWhileReferenced()
    {
        using var fixture = new DemandFixture();
        var (document, built) = await fixture.OpenEngineCubeAsync().ConfigureAwait(false);
        var node = built.RootNodes.Single();
        var authoring = SceneContent.Of(document).Authoring;

        _ = await authoring.ExecuteAsync(new SetGeometry([node.Id], Geometry)).ConfigureAwait(false);

        _ = fixture.Demanded.Should().Equal(Geometry);
        _ = fixture.Requests.Single().Token.IsCancellationRequested.Should().BeFalse("a selection or anything else but the reference does not cancel it");
    }

    [TestMethod]
    public async Task UndoOfTheAssignment_CancelsTheDemand()
    {
        using var fixture = new DemandFixture();
        var (document, built) = await fixture.OpenEngineCubeAsync().ConfigureAwait(false);
        var node = built.RootNodes.Single();
        var authoring = SceneContent.Of(document).Authoring;
        _ = await authoring.ExecuteAsync(new SetGeometry([node.Id], Geometry)).ConfigureAwait(false);

        _ = await authoring.UndoAsync().ConfigureAwait(false);

        _ = fixture.Requests.Single().Token.IsCancellationRequested.Should().BeTrue();
    }

    [TestMethod]
    public async Task RedoAfterUndo_DemandsTheAssetAgainWhileTheCancelledCookUnwinds()
    {
        using var fixture = new DemandFixture();
        var (document, built) = await fixture.OpenEngineCubeAsync().ConfigureAwait(false);
        var node = built.RootNodes.Single();
        var authoring = SceneContent.Of(document).Authoring;
        _ = await authoring.ExecuteAsync(new SetGeometry([node.Id], Geometry)).ConfigureAwait(false);

        _ = await authoring.UndoAsync().ConfigureAwait(false);
        _ = await authoring.RedoAsync().ConfigureAwait(false);

        _ = fixture.Demanded.Should().Equal(Geometry, Geometry);
        _ = fixture.Requests[^1].Token.IsCancellationRequested.Should().BeFalse("the scene references the asset again");
    }

    [TestMethod]
    public async Task RemovingTheComponent_CancelsItsDemands()
    {
        using var fixture = new DemandFixture();
        var (document, built) = await fixture.OpenAsync(withGeometry: true).ConfigureAwait(false);
        var node = SceneContent.Of(document).Scene.AllNodes.Single(candidate => candidate.Id == built.RootNodes.Single().Id);
        var geometry = node.Components.OfType<GeometryComponent>().Single();

        _ = await SceneContent.Of(document).Authoring.ExecuteAsync(new RemoveComponent(node.Id, geometry.Id)).ConfigureAwait(false);

        _ = fixture.Requests.Should().OnlyContain(static request => request.Token.IsCancellationRequested);
    }

    [TestMethod]
    public async Task ClosingTheDocument_CancelsItsDemands()
    {
        using var fixture = new DemandFixture();
        var (document, _) = await fixture.OpenAsync(withGeometry: true).ConfigureAwait(false);

        fixture.Documents.Open.Close(document);

        _ = fixture.Requests.Should().HaveCount(2).And.OnlyContain(static request => request.Token.IsCancellationRequested);
    }

    [TestMethod]
    public async Task FreshOrEngineAssets_AreNotDemanded()
    {
        using var fixture = new DemandFixture { Freshness = AssetCookFreshness.Current };

        _ = await fixture.OpenAsync(withGeometry: true).ConfigureAwait(false);

        _ = fixture.Requests.Should().BeEmpty();
    }

    private sealed partial class DemandFixture : IDisposable
    {
        private readonly Mock<IAssetFeed> assets = new();
        private readonly Mock<IContentPipelineService> pipeline = new();
        private readonly List<TaskCompletionSource<ContentCookResult>> cooks = [];
        private readonly SceneContentDemand demand;

        public DemandFixture()
        {
            _ = this.assets.SetupGet(value => value.Items).Returns(Observable.Return<IReadOnlyList<AssetEntry>>([]));
            _ = this.assets.Setup(value => value.ResolveAsync(It.IsAny<AssetPath>(), It.IsAny<CancellationToken>()))
                .ReturnsAsync((AssetPath asset, CancellationToken _) => Entry(asset, this.Freshness));
            _ = this.pipeline.Setup(value => value.CookPreviewAssetAsync(It.IsAny<AssetPath>(), It.IsAny<ProjectContext>(), It.IsAny<CancellationToken>()))
                .Returns((AssetPath asset, ProjectContext _, CancellationToken token) =>
                {
                    this.Requests.Add((asset, token));
                    var cook = new TaskCompletionSource<ContentCookResult>(TaskCreationOptions.RunContinuationsAsynchronously);
                    this.cooks.Add(cook);
                    return cook.Task.WaitAsync(token);
                });
            this.demand = new SceneContentDemand(this.Documents.Open, new DocumentScenes(this.Documents.Open), this.assets.Object, this.pipeline.Object, this.Documents.Session);
        }

        public TestSceneDocuments Documents { get; } = new(TestProject.Create(new MockFileSystem(), @"C:\LivePreviewTests"));

        public AssetCookFreshness Freshness { get; init; } = AssetCookFreshness.NeedsCooking;

        public List<(AssetPath Asset, CancellationToken Token)> Requests { get; } = [];

        public IEnumerable<AssetPath> Demanded => this.Requests.Select(static request => request.Asset);

        public async Task<(Document Document, Scene Built)> OpenAsync(bool withGeometry)
        {
            var built = new Scene { Name = "Test Scene" };
            var node = new SceneNode(built, "Cube");
            built.RootNodes.Add(node);
            if (withGeometry)
            {
                var geometry = new GeometryComponent { Name = "Geometry", Geometry = Geometry };
                geometry.Overrides = geometry.Overrides.With(
                    SceneSchema.Geometry.Surface.Material,
                    new PartId(SceneSchema.Geometry.Surface.Kind, Guid.Parse("10000000-0000-0000-0000-000000000001")),
                    new string('a', 64),
                    Material);

                node.Components.Add(geometry);
            }

            var document = await this.Documents.OpenAsync(built.Dehydrate()).ConfigureAwait(false);
            await Task.Yield();
            return (document, built);
        }

        public async Task<(Document Document, Scene Built)> OpenEngineCubeAsync()
        {
            var built = new Scene { Name = "Test Scene" };
            var node = new SceneNode(built, "Cube");
            built.RootNodes.Add(node);
            node.Components.Add(new GeometryComponent { Name = "Geometry", Geometry = EngineAssets.BasicShape("Cube") });
            var document = await this.Documents.OpenAsync(built.Dehydrate()).ConfigureAwait(false);
            await Task.Yield();
            return (document, built);
        }

        public void CompleteAll()
        {
            // The fixture ends the cooks; no token cancels them.
            foreach (var cook in this.cooks)
            {
                _ = cook.TrySetCanceled(CancellationToken.None);
            }
        }

        public void Dispose()
        {
            this.demand.Dispose();
            this.Documents.Dispose();
        }

        private static AssetEntry Entry(AssetPath asset, AssetCookFreshness freshness)
            => new(asset, asset.Name, asset.VirtualPath.Contains("/Materials/", StringComparison.Ordinal) ? AssetKind.Material : AssetKind.Geometry, AssetState.Descriptor, DerivedState: null, AssetRuntimeAvailability.NotMounted, asset.VirtualPath, SourcePath: null, DescriptorPath: FileSystemPath.From(@"C:\LivePreviewTests\Content").Combine(asset.RelativePath + AssetPath.AuthoringSuffix), CookedPath: null, AssetGuid: null, DiagnosticCodes: [], IsSelectable: true)
            {
                CookStatus = new(asset, freshness, HasPublishedOutput: false, OutputAvailability: CookedOutputAvailability.Missing, [], []),
            };
    }
}
