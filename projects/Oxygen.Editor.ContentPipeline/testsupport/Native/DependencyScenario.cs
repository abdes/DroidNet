// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Security.Cryptography;
using System.Text.Json.Nodes;
using System.Text;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class DependencyScenario
{
    internal static void AddGeometryNode(CookWorkspace workspace, Uri uri, string name)
    {
        var node = new SceneNode(workspace.Scene) { Name = name };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(uri) });
        workspace.Scene.RootNodes.Add(node);
    }

    internal static void WriteAuthoredGeometry(CookWorkspace workspace, bool withBuffer)
    {
        var catalog = JsonNode.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json")))!;
        var descriptor = catalog["geometries"]![0]!["descriptor"]!;
        descriptor["name"] = "AuthoredCube";
        descriptor["lods"]![0]!["submeshes"]![0]!["material_ref"] = "/Content/Materials/Red.omat";
        if (withBuffer)
        {
            descriptor["buffers"] = JsonNode.Parse("""[{"uri":"mesh.bin","virtual_path":"/Content/Buffers/Mesh.obuf"}]""");
            workspace.WriteText("Content/Geometry/mesh.bin", "saved buffer bytes");
        }

        workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json", descriptor.ToJsonString());
    }
}
