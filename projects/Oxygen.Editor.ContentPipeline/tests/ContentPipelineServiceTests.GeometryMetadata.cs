// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies native slot metadata projection and authoring request lifetime.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Opaque UUID text and omitted defaults survive the managed projection unchanged.</summary>
    [TestMethod]
    public void GeometryMetadataPreservesOpaqueSlotsAndNilDefaults()
    {
        var uri = new Uri("asset:///Content/Geometry/Test.ogeo");
        var result = CookedGeometryReport.Parse(SlotReport()).SingleOrDefault(uri)!;
        _ = result.GeometryUri.Should().Be(uri);
        _ = result.NativeGeometryKey.Should().Be(Guid.Parse("10203040-5060-7080-90a0-b0c0d0e0f001"));
        var slot = result.Slots.Should().ContainSingle().Subject;
        _ = slot.SlotId.Should().Be(Guid.Parse("00000000-0000-0000-0000-000000000001"));
        _ = slot.Bindings.Should().ContainSingle().Which.DefaultMaterialKey.Should().Be(Guid.Empty);
        _ = result.LayoutRevision.Should().Be(new string('a', 64));
    }

    /// <summary>Ambiguous identity data never reaches material authoring.</summary>
    [TestMethod]
    public void GeometryMetadataRejectsDuplicateSlotIdentities()
    {
        var source = JsonNode.Parse(SlotReport())!;
        var slots = source["geometries"]![0]!["slots"]!.AsArray();
        slots.Add(slots[0]!.DeepClone());
        Action read = () => CookedGeometryReport.Parse(source.ToJsonString());
        _ = read.Should().Throw<InvalidDataException>();
    }

    /// <summary>New builtin nodes receive native inventory without cooking a project.</summary>
    /// <returns>The asynchronous metadata query.</returns>
    [TestMethod]
    public async Task BuiltinGeometryMetadataIsAvailableBeforeCooking()
    {
        using var workspace = new TempWorkspace();
        var catalog = await new BuiltinCatalogFixture().GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", this.TestContext.CancellationToken).ConfigureAwait(false);
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        _ = api.As<IBuiltinGeometryCatalogProvider>().Setup(value => value.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>())).ReturnsAsync(catalog);
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var uri = new Uri("asset:///Engine/Generated/BasicShapes/Cube");
        var result = await pipeline.ReadAsync(workspace.ProjectContext, uri, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Should().Be(catalog.Find(uri)!.MaterialSlots);
        _ = workspace.CookCoordinator.Runs.Should().BeEmpty();
        api.Verify(value => value.ImportAsync(It.IsAny<ContentImportExecution>(), It.IsAny<CancellationToken>()), Times.Never);
    }

    /// <summary>Stale project identity cannot start a metadata query.</summary>
    /// <returns>The asynchronous request.</returns>
    [TestMethod]
    public async Task GeometryMetadataRejectsAReplacedProject()
    {
        using var workspace = new TempWorkspace();
        var original = workspace.ProjectContext;
        workspace.ContextService.Activate(original with { ProjectId = Guid.NewGuid() });
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var result = await pipeline.ReadAsync(original, new Uri("asset:///Engine/Generated/BasicShapes/Cube"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Should().BeNull();
        api.VerifyNoOtherCalls();
    }

    /// <summary>Project changes cancel a pending native catalog request before it supplies an inventory.</summary>
    /// <returns>The asynchronous lifetime regression.</returns>
    [TestMethod]
    public async Task GeometryMetadataCancelsOnProjectReplacement()
    {
        using var workspace = new TempWorkspace();
        var original = workspace.ProjectContext;
        var entered = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        _ = api.As<IBuiltinGeometryCatalogProvider>().Setup(value => value.GetBuiltinGeometryCatalogAsync(workspace.Root, "Content", It.IsAny<CancellationToken>(), It.IsAny<NativeArtifactLease>()))
            .Returns(async (string _, string _, CancellationToken token, NativeArtifactLease _) =>
            {
                entered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, token).ConfigureAwait(false);
                throw new InvalidOperationException("Canceled query unexpectedly resumed.");
            });
        var pipeline = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api.Object);
        var pending = pipeline.ReadAsync(original, new Uri("asset:///Engine/Generated/BasicShapes/Cube"), this.TestContext.CancellationToken);
        await entered.Task.WaitAsync(TimeSpan.FromSeconds(5), this.TestContext.CancellationToken).ConfigureAwait(false);
        workspace.ContextService.Activate(original with { ProjectId = Guid.NewGuid() });
        Func<Task> completion = async () => _ = await pending.ConfigureAwait(false);
        _ = await completion.Should().ThrowAsync<OperationCanceledException>().ConfigureAwait(false);
    }

    private static string SlotReport() => """
        {
          "schema_version": 1,
          "geometries": [{
            "geometry_asset_key": "10203040-5060-7080-90a0-b0c0d0e0f001",
            "layout_revision": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "slots": [{
              "slot_id": "00000000-0000-0000-0000-000000000001",
              "display_name": "Surface",
              "bindings": [{"lod_index": 0, "submesh_index": 1,
                "default_material_key": "00000000-0000-0000-0000-000000000000"}]
            }]
          }]
        }
        """;
}
