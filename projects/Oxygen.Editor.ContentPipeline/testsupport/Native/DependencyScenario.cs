// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class DependencyScenario
{
    internal static void AddGeometryNode(CookWorkspace workspace, AssetPath asset, string name)
    {
        var node = new SceneNode(workspace.Scene, name);
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = asset });
        workspace.Scene.RootNodes.Add(node);
    }

    internal static void WriteAuthoredGeometry(CookWorkspace workspace, bool withBuffer)
    {
        // A procedural cube authored in the project, with one surface.
        var buffers = withBuffer ? ""","buffers":[{"uri":"mesh.bin","virtual_path":"/Content/Buffers/Mesh.obuf"}]""" : string.Empty;
        if (withBuffer)
        {
            workspace.WriteText("Content/Geometry/mesh.bin", "saved buffer bytes");
        }

        workspace.WriteText("Content/Geometry/AuthoredCube.ogeo.json", """
            {"$schema":"oxygen.geometry-descriptor.v1","name":"AuthoredCube","bounds":{"min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},"lods":[{"name":"LOD0","mesh_type":"procedural","bounds":{"min":[-0.5,-0.5,-0.5],"max":[0.5,0.5,0.5]},"procedural":{"generator":"Cube","mesh_name":"Cube","params":{}},"submeshes":[{"name":"Main","material_ref":"/Content/Materials/Red.omat","surface_id":"146b1397-c3e0-bc20-fce3-c55e470b4086","views":[{"view_ref":"__all__"}]}]}]
            """ + buffers + "}");
    }
}
