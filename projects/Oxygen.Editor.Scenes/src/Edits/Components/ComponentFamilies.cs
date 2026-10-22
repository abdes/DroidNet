// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Edits.Components;

/// <summary>The component families a node holds at most one of each.</summary>
public static class ComponentFamilies
{
    /// <summary>The family of a node's transform.</summary>
    public const string Transform = SceneSchema.Transform.Kind;

    /// <summary>The family of a node's geometry.</summary>
    public const string Geometry = SceneSchema.Geometry.Kind;

    /// <summary>The family of a node's camera, perspective or orthographic.</summary>
    public const string Camera = "camera";

    /// <summary>The family of a node's light: directional, point or spot.</summary>
    public const string Light = "light";

    /// <summary>The family of a node's local fog volume.</summary>
    public const string LocalFogVolume = SceneSchema.LocalFogVolume.Kind;

    /// <summary>The family of a node's Scripting component.</summary>
    public const string Scripting = SceneSchema.Scripting.Kind;

    /// <summary>Gets a component's family.</summary>
    /// <param name="component">The component.</param>
    /// <returns>The family.</returns>
    public static string KindOf(GameComponent component)
        => component switch
        {
            TransformComponent => Transform,
            GeometryComponent => Geometry,
            CameraComponent => Camera,
            LightComponent => Light,
            LocalFogVolumeComponent => LocalFogVolume,
            ScriptingComponent => Scripting,
            _ => throw new ArgumentOutOfRangeException(nameof(component), component.GetType().Name, "Unknown component family."),
        };

    /// <summary>Tells why a node cannot take a component, if it cannot.</summary>
    /// <param name="node">The node.</param>
    /// <param name="component">The component's data.</param>
    /// <returns>Why the node cannot take it, or <see langword="null"/> when it can.</returns>
    internal static SceneDiagnostic? Conflict(SceneNode node, ComponentData component)
        => component switch
        {
            TransformData or MetadataData or HierarchyData or NodeFlagsData => Duplicate("The node's required components are present on every scene node."),
            GeometryComponentData when node.Components.OfType<GeometryComponent>().Any() => Duplicate("This node already has a geometry component."),
            CameraComponentData when node.Components.OfType<CameraComponent>().Any() => Duplicate("This node already has a camera component."),
            LightComponentData when node.Components.OfType<LightComponent>().Any() => Duplicate("This node already has a light component."),
            LocalFogVolumeData when node.Components.OfType<LocalFogVolumeComponent>().Any() => Duplicate("This node already has a local fog volume."),
            ScriptingData when node.Components.OfType<ScriptingComponent>().Any() => Duplicate("This node already has a Scripting component; add a script slot to it instead."),
            GeometryComponentData or CameraComponentData or LightComponentData or LocalFogVolumeData or ScriptingData => null,
            _ => SceneDiagnostic.Error(SceneEditCodes.ComponentUnsupported, $"Component type '{component.GetType().Name}' cannot be added."),
        };

    private static SceneDiagnostic Duplicate(string message) => SceneDiagnostic.Error(SceneEditCodes.ComponentDuplicate, message);
}
