// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Inspection;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class GeometryMetadataTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Opaque UUID text and omitted defaults survive the managed projection unchanged.</summary>
    [TestMethod]
    public void GeometryMetadataPreservesOpaqueSurfacesAndNilDefaults()
    {
        var uri = AssetPath.Parse("/Content/Geometry/Test.ogeo");
        var result = CookedGeometryReport.Parse(SurfaceReport()).SingleOrDefault(uri)!;
        _ = result.GeometryUri.Should().Be(uri);
        _ = result.NativeGeometryKey.Should().Be(Guid.Parse("10203040-5060-7080-90a0-b0c0d0e0f001"));
        var surface = result.Surfaces.Should().ContainSingle().Subject;
        _ = surface.SurfaceId.Should().Be(Guid.Parse("00000000-0000-0000-0000-000000000001"));
        _ = surface.Visible.Should().BeTrue();
        _ = surface.CastsShadows.Should().BeFalse("the asset's surface defaults reach the inspector");
        _ = surface.Bindings.Should().ContainSingle().Which.DefaultMaterialKey.Should().Be(Guid.Empty);
        _ = result.LayoutRevision.Should().Be(new string('a', 64));
    }

    /// <summary>The asset's LOD policy and LOD count reach the inspector, read from the native report's sorted keys.</summary>
    [TestMethod]
    public void GeometryMetadataReadsTheLodPolicyAndCountsTheLods()
    {
        var source = JsonNode.Parse(SurfaceReport())!;
        var geometry = source["geometries"]![0]!;
        geometry["lod_policy"] = JsonNode.Parse("""{"hysteresis_ratio": 0.2, "thresholds": [0.25, 0.5, 0.8], "type": "distance"}""");
        geometry["surfaces"]![0]!["bindings"]!.AsArray().Add(JsonNode.Parse("""{"lod_index": 3, "submesh_index": 0, "default_material_key": "00000000-0000-0000-0000-000000000000", "triangle_count": 6}"""));

        var result = CookedGeometryReport.Parse(source.ToJsonString()).SingleOrDefault(AssetPath.Parse("/Content/Geometry/Test.ogeo"))!;

        _ = result.LodPolicy.Should().Be(new LodPolicy.Distance([0.25f, 0.5f, 0.8f], 0.2f));
        _ = result.LodCount.Should().Be(4);
    }

    /// <summary>A LOD's triangles are the sum over the submeshes its surfaces bind there, read from each binding.</summary>
    [TestMethod]
    public void GeometryMetadataSumsEachLodsTriangles()
    {
        var source = JsonNode.Parse(SurfaceReport())!;
        var surfaces = source["geometries"]![0]!["surfaces"]!.AsArray();
        surfaces[0]!["bindings"]!.AsArray().Add(JsonNode.Parse("""{"lod_index": 1, "submesh_index": 1, "default_material_key": "00000000-0000-0000-0000-000000000000", "triangle_count": 40}"""));
        var second = surfaces[0]!.DeepClone();
        second["surface_id"] = "00000000-0000-0000-0000-000000000002";
        second["bindings"] = JsonNode.Parse("""[{"lod_index": 0, "submesh_index": 0, "default_material_key": "00000000-0000-0000-0000-000000000000", "triangle_count": 20}]""");
        surfaces.Add(second);

        var result = CookedGeometryReport.Parse(source.ToJsonString()).SingleOrDefault(AssetPath.Parse("/Content/Geometry/Test.ogeo"))!;

        _ = result.TriangleCountOf(0).Should().Be(120);
        _ = result.TriangleCountOf(1).Should().Be(40);
        _ = result.TriangleCountOf(2).Should().Be(0, "the geometry has no third LOD");
    }

    /// <summary>A policy the engine would not accept is a defect of the report, not a value to show.</summary>
    [TestMethod]
    public void GeometryMetadataRejectsAnUnorderedLodPolicy()
    {
        var source = JsonNode.Parse(SurfaceReport())!;
        source["geometries"]![0]!["lod_policy"] = JsonNode.Parse("""{"hysteresis_ratio": 0.1, "thresholds": [0.5, 0.25], "type": "distance"}""");

        Action read = () => CookedGeometryReport.Parse(source.ToJsonString());

        _ = read.Should().Throw<InvalidDataException>().WithMessage("*LOD policy*");
    }

    /// <summary>Ambiguous identity data never reaches material authoring.</summary>
    [TestMethod]
    public void GeometryMetadataRejectsDuplicateSurfaceIdentities()
    {
        var source = JsonNode.Parse(SurfaceReport())!;
        var surfaces = source["geometries"]![0]!["surfaces"]!.AsArray();
        surfaces.Add(surfaces[0]!.DeepClone());
        Action read = () => CookedGeometryReport.Parse(source.ToJsonString());
        _ = read.Should().Throw<InvalidDataException>();
    }

    /// <summary>Stale project identity cannot start a metadata query.</summary>
    /// <returns>The asynchronous request.</returns>
    [TestMethod]
    public async Task GeometryMetadataRejectsAReplacedProject()
    {
        using var workspace = new CookWorkspace();
        var original = workspace.ProjectContext;
        workspace.Activate(original with { ProjectId = Guid.NewGuid() });
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        var pipeline = CreateService(workspace, api.Object, new CapturingSceneDescriptorGenerator([]));
        var result = await pipeline.ReadAsync(original, EngineAssets.BasicShape("Cube"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Should().BeNull();
        api.VerifyNoOtherCalls();
    }

    private static string SurfaceReport() => """
        {
          "schema_version": 1,
          "geometries": [{
            "geometry_asset_key": "10203040-5060-7080-90a0-b0c0d0e0f001",
            "layout_revision": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "lod_policy": {"lod_index": 0, "type": "fixed"},
            "surfaces": [{
              "surface_id": "00000000-0000-0000-0000-000000000001",
              "display_name": "Surface",
              "visible": true,
              "casts_shadows": false,
              "bindings": [{"lod_index": 0, "submesh_index": 1,
                "default_material_key": "00000000-0000-0000-0000-000000000000", "triangle_count": 100}]
            }]
          }]
        }
        """;
}
