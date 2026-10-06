// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeSceneData
{
    internal static void AddGeometryNode(Scene scene, string shape)
    {
        var node = new SceneNode(scene)
        {
            Name = shape
        };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri($"BasicShapes/{shape}")) });
        scene.RootNodes.Add(node);
    }

    internal static void SeedNativeNode(Scene scene, string kind, Action<SceneNode>? arrange = null)
    {
        var node = new SceneNode(scene)
        {
            Name = kind
        };
        if (string.Equals(kind, "Camera", StringComparison.Ordinal))
        {
            _ = node.AddComponent(new PerspectiveCamera { Name = "Camera" });
        }
        else if (string.Equals(kind, "Light", StringComparison.Ordinal))
        {
            _ = node.AddComponent(new DirectionalLightComponent { Name = "Light", CastsShadows = true });
        }

        arrange?.Invoke(node);
        scene.RootNodes.Add(node);
    }

    internal static void SeedShadowTransitionScene(Scene scene, int cascades)
    {
        AddGeometryNode(scene, "Cube");
        var cube = scene.RootNodes[^1];
        cube.IsActive = true;
        cube.CastsShadows = true;
        cube.ReceivesShadows = true;
        var sun = new SceneNode(scene)
        {
            Name = "Sun",
            IsActive = true
        };
        _ = sun.AddComponent(new DirectionalLightComponent { Name = "Sun", CastsShadows = true, CascadeCount = cascades });
        scene.RootNodes.Add(sun);
        sun.Components.OfType<DirectionalLightComponent>().Single().AtmosphereSlot = Oxygen.Editor.World.Serialization.AtmosphereLightSlot.Primary;
    }
}
