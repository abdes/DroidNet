// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>The default nodes the editor creates, as data with new node and component ids.</summary>
public static class SceneNodeTemplates
{
    /// <summary>Creates a primitive node: a built-in shape.</summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="kind">The shape.</param>
    /// <param name="placement">Where the node goes; the scene origin when <see langword="null"/>.</param>
    /// <returns>The node's data.</returns>
    /// <exception cref="ArgumentOutOfRangeException">The kind is not defined.</exception>
    public static SceneNodeData Primitive(Scene scene, PrimitiveKind kind, NodePlacement? placement = null)
    {
        var name = Enum.IsDefined(kind) ? kind.ToString() : throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown primitive kind.");
        var node = new SceneNode(scene) { Name = name };
        _ = node.AddComponent(new GeometryComponent
        {
            Name = "Geometry",
            Geometry = new AssetReference<GeometryAsset>(AssetUris.BuildGeneratedUri($"BasicShapes/{name}")),
        });
        return Finish(node, placement);
    }

    /// <summary>Creates a light node.</summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="kind">The light kind.</param>
    /// <param name="placement">
    /// Where the node goes; the kind's default position when <see langword="null"/>. A placement without a rotation keeps
    /// the kind's default orientation.
    /// </param>
    /// <returns>The node's data.</returns>
    /// <exception cref="ArgumentOutOfRangeException">The kind is not defined.</exception>
    public static SceneNodeData Light(Scene scene, LightKind kind, NodePlacement? placement = null)
    {
        LightComponent light = kind switch
        {
            LightKind.Directional => new DirectionalLightComponent { Name = "Directional Light" },
            LightKind.Point => new PointLightComponent { Name = "Point Light", LuminousFluxLumens = 1_600f, Range = 10f },
            LightKind.Spot => new SpotLightComponent { Name = "Spot Light", LuminousFluxLumens = 1_600f, Range = 15f },
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown light kind."),
        };
        var node = new SceneNode(scene) { Name = $"{kind} Light" };
        if (node.Components.OfType<TransformComponent>().FirstOrDefault() is { } transform)
        {
            transform.LocalPosition = kind switch
            {
                LightKind.Point => new Vector3(0f, 2f, 0f),
                LightKind.Spot => new Vector3(0f, 3f, 3f),
                _ => Vector3.Zero,
            };
            transform.LocalRotation = kind switch
            {
                LightKind.Directional => DirectionalLightComponent.DefaultLocalRotation,
                LightKind.Spot => Quaternion.CreateFromYawPitchRoll(0f, -0.7853982f, 0f),
                _ => Quaternion.Identity,
            };
        }

        _ = node.AddComponent(light);
        return Finish(node, placement);
    }

    /// <summary>Creates a node carrying a local fog volume.</summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="placement">Where the node goes; the scene origin when <see langword="null"/>.</param>
    /// <returns>The node's data.</returns>
    public static SceneNodeData LocalFogVolume(Scene scene, NodePlacement? placement = null)
    {
        var node = new SceneNode(scene) { Name = "Local Fog Volume" };
        _ = node.AddComponent(new LocalFogVolumeComponent { Name = "Local Fog Volume" });
        return Finish(node, placement);
    }

    /// <summary>Creates a camera node.</summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="kind">The camera kind.</param>
    /// <param name="placement">Where the node goes; the scene origin when <see langword="null"/>.</param>
    /// <returns>The node's data.</returns>
    /// <exception cref="ArgumentOutOfRangeException">The kind is not defined.</exception>
    public static SceneNodeData Camera(Scene scene, CameraKind kind, NodePlacement? placement = null)
    {
        CameraComponent camera = kind switch
        {
            CameraKind.Perspective => new PerspectiveCamera { Name = "Perspective Camera" },
            CameraKind.Orthographic => new OrthographicCamera { Name = "Orthographic Camera" },
            _ => throw new ArgumentOutOfRangeException(nameof(kind), kind, "Unknown camera kind."),
        };
        var node = new SceneNode(scene) { Name = "Camera" };
        _ = node.AddComponent(camera);
        return Finish(node, placement);
    }

    /// <summary>Creates an empty node.</summary>
    /// <param name="scene">The scene the node is for.</param>
    /// <param name="name">The node's name.</param>
    /// <param name="placement">Where the node goes; the scene origin when <see langword="null"/>.</param>
    /// <returns>The node's data.</returns>
    public static SceneNodeData Empty(Scene scene, string name = "NodeValue", NodePlacement? placement = null)
        => Finish(new SceneNode(scene) { Name = name }, placement);

    /// <summary>Builds the insert that appends a new node: last among its parent's children, last in its explorer place.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="node">The node's data.</param>
    /// <param name="parent">The parent, or <see langword="null"/> for a root node.</param>
    /// <param name="folder">The folder its entry goes in, in the parent's scope; the scope's own list when <see langword="null"/>.</param>
    /// <returns>The insert.</returns>
    public static NodeInsert Append(Scene scene, IList<ExplorerEntryData> layout, SceneNodeData node, NodeId? parent = null, FolderId? folder = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);
        var parentNode = parent is { } id ? scene.AllNodes.First(candidate => candidate.Id == id) : null;
        var siblings = parentNode?.Children.Count ?? scene.RootNodes.Count;
        var place = folder is { } inFolder
            ? LayoutPosition.InFolder(inFolder, 0)
            : parent is { } underNode ? LayoutPosition.UnderNode(underNode, 0) : LayoutPosition.AtRoot(0);
        var entries = SceneLayout.ContainerOf(layout, place)?.Count ?? 0;
        return new NodeInsert(node, new NodePosition(parent, siblings), place with { Index = entries });
    }

    // Created nodes are roots: their local transform is their world transform.
    private static SceneNodeData Finish(SceneNode node, NodePlacement? placement)
    {
        if (placement is not null && node.Components.OfType<TransformComponent>().FirstOrDefault() is { } transform)
        {
            transform.LocalPosition = placement.Position;
            if (placement.Rotation is { } rotation)
            {
                transform.LocalRotation = rotation;
            }
        }

        return node.Dehydrate();
    }
}
