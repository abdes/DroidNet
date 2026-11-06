// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

/// <summary>
/// A geometry can read its buffers from a buffer container beside it. Only the native cook says whether the container
/// joins the cook and runs before the geometry, so no lower level reaches this.
/// </summary>
[TestClass]
public sealed class BufferContainerCookTests
{
    private const int VertexStride = 80;
    private const int IndexStride = 4;

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Cooking the geometry brings in the container that owns its buffers, and both publish.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task AGeometryCooksWithTheContainerThatOwnsItsBuffers()
    {
        using var workspace = new CookWorkspace([new("Content", MountKind.Source, "Content")], EngineContentPak.Installed());
        WriteBoxWithContainer(workspace);
        var cook = NativeCook.Create(workspace);

        var result = await cook.Service.CookAssetAsync(AssetPath.Parse("/Content/Geo/Box.ogeo"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Code + " " + issue.Message)));
    }

    /// <summary>A buffer no container names is still reported, as the geometry's own problem.</summary>
    /// <returns>The asynchronous cook.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ABufferNoContainerNamesIsReported()
    {
        using var workspace = new CookWorkspace([new("Content", MountKind.Source, "Content")], EngineContentPak.Installed());
        WriteBoxWithContainer(workspace, vertexBuffer: "Other_vb.obuf");
        var cook = NativeCook.Create(workspace);

        var result = await cook.Service.CookAssetAsync(AssetPath.Parse("/Content/Geo/Box.ogeo"), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.IsPublished.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(static issue => issue.Code == "geometry.buffer.sidecar_missing");
    }

    private static void WriteBoxWithContainer(CookWorkspace workspace, string vertexBuffer = "Box_vb.obuf")
    {
        var bounds = new JsonObject { ["min"] = new JsonArray(-1, -1, -1), ["max"] = new JsonArray(1, 1, 1) };
        workspace.WriteMaterial("Content/Geo/Box.omat.json", "Box");
        var geometry = AuthoredFiles.Geometry("/Content/Geo/Box.ogeo")
            .SubmeshMaterial(0, 0, "/Content/Geo/Box.omat")
            .Set("/bounds", bounds.DeepClone())
            .Set("/lods/0/mesh_type", "standard")
            .Set("/lods/0/bounds", bounds.DeepClone())
            .Set("/lods/0/buffers", new JsonObject { ["vb_ref"] = "Box_vb.obuf", ["ib_ref"] = "Box_ib.obuf" })
            .Set("/lods/0/submeshes/0/bounds", bounds.DeepClone());

        // The kit's geometry is procedural; a standard mesh has no generator.
        var document = JsonNode.Parse(geometry.Text)!;
        _ = document["lods"]![0]!.AsObject().Remove("procedural");
        workspace.WriteText("Content/Geo/Box.ogeo.json", document.ToJsonString());
        var buffers = new JsonObject
        {
            ["name"] = "Box_buffers",
            ["buffers"] = new JsonArray(
                new JsonObject { ["source"] = "Box_vb.bin", ["virtual_path"] = vertexBuffer, ["usage_flags"] = 1, ["element_stride"] = VertexStride, ["alignment"] = 16 },
                new JsonObject { ["source"] = "Box_ib.bin", ["virtual_path"] = "Box_ib.obuf", ["usage_flags"] = 2, ["element_stride"] = IndexStride, ["alignment"] = IndexStride }),
        };
        workspace.WriteText("Content/Geo/Box.buffers.json", buffers.ToJsonString());
        File.WriteAllBytes(Path.Combine(workspace.Root, "Content", "Geo", "Box_vb.bin"), new byte[VertexStride * 3]);
        File.WriteAllBytes(Path.Combine(workspace.Root, "Content", "Geo", "Box_ib.bin"), [0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0]);
    }
}
