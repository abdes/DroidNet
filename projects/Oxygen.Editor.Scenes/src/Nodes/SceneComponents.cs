// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Components;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Builds the edits that add components, with the defaults and consequences each kind brings.</summary>
public static class SceneComponents
{
    /// <summary>The kind of a geometry component, a cube until another geometry is chosen.</summary>
    public const string Geometry = "GeometryComponent";

    /// <summary>The kind of a perspective camera.</summary>
    public const string PerspectiveCamera = "PerspectiveCamera";

    /// <summary>The kind of an orthographic camera.</summary>
    public const string OrthographicCamera = "OrthographicCamera";

    /// <summary>The kind of a directional light.</summary>
    public const string DirectionalLight = "DirectionalLight";

    /// <summary>The kind of a point light.</summary>
    public const string PointLight = "PointLight";

    /// <summary>The kind of a spot light.</summary>
    public const string SpotLight = "SpotLight";

    /// <summary>The kind of a local fog volume.</summary>
    public const string LocalFogVolume = "LocalFogVolume";

    /// <summary>The kind of a Scripting component, with one empty script slot.</summary>
    public const string Scripting = "Scripting";

    /// <summary>Builds the edit for the inspector's "add component": the component, then the edits its kind requires.</summary>
    /// <param name="authoring">The scene's authoring, to read the node from.</param>
    /// <param name="node">The node.</param>
    /// <param name="kind">The component kind, one of this class's constants.</param>
    /// <returns>The edit to execute; a batch when adding the component has consequences.</returns>
    /// <exception cref="ArgumentException">The node is not in the scene, or the kind is unknown.</exception>
    public static ISceneEdit Add(SceneAuthoring authoring, NodeId node, string kind)
    {
        ArgumentNullException.ThrowIfNull(authoring);
        var target = authoring.Scene.AllNodes.FirstOrDefault(candidate => candidate.Id == node)
            ?? throw new ArgumentException("The node is not in the scene.", nameof(node));
        var component = Create(kind);
        var add = new AddComponent(node, component.Dehydrate(), target.Components.Count);
        var consequences = RulesOf(component)?.OnAdd(target) ?? [];
        return consequences.Count == 0 ? add : new SceneEditBatch(add.Label, [add, .. consequences]);
    }

    /// <summary>Creates a component of a kind with its defaults and a new identity.</summary>
    /// <param name="kind">The component kind, one of this class's constants.</param>
    /// <returns>The component.</returns>
    /// <exception cref="ArgumentException">The kind is unknown.</exception>
    public static GameComponent Create(string kind)
        => kind switch
        {
            Geometry => new GeometryComponent
            {
                Name = "Geometry",
                Geometry = EngineAssets.BasicShape("Cube"),
            },
            PerspectiveCamera => new PerspectiveCamera { Name = "Perspective Camera" },
            OrthographicCamera => new OrthographicCamera { Name = "Orthographic Camera" },
            DirectionalLight => new DirectionalLightComponent { Name = "Directional Light", CastsShadows = true, AtmosphereSlot = AtmosphereLightSlot.None },
            PointLight => new PointLightComponent { Name = "Point Light", LuminousFluxLumens = 1_600f, Range = 10f },
            SpotLight => new SpotLightComponent { Name = "Spot Light", LuminousFluxLumens = 1_600f, Range = 15f },
            LocalFogVolume => new LocalFogVolumeComponent { Name = "Local Fog Volume" },
            Scripting => new ScriptingComponent { Name = ScriptingComponent.DefaultName, Slots = [new ScriptSlot()] },
            _ => throw new ArgumentException($"Component kind '{kind}' is not supported.", nameof(kind)),
        };

    /// <summary>Gets the kind of a component, as <see cref="Add"/> takes it.</summary>
    /// <param name="component">The component, such as one a menu offers.</param>
    /// <returns>The kind.</returns>
    /// <exception cref="ArgumentException">The component's kind cannot be added.</exception>
    public static string KindOf(GameComponent component)
    {
        ArgumentNullException.ThrowIfNull(component);
        return component switch
        {
            GeometryComponent => Geometry,
            World.Components.PerspectiveCamera => PerspectiveCamera,
            World.Components.OrthographicCamera => OrthographicCamera,
            DirectionalLightComponent => DirectionalLight,
            PointLightComponent => PointLight,
            SpotLightComponent => SpotLight,
            LocalFogVolumeComponent => LocalFogVolume,
            ScriptingComponent => Scripting,
            _ => throw new ArgumentException($"A {component.GetType().Name} cannot be added.", nameof(component)),
        };
    }

    private static IComponentRules? RulesOf(GameComponent component)
        => component switch
        {
            DirectionalLightComponent => SceneProperties.RulesOf(SceneProperties.DirectionalLightKind),
            _ => null,
        };
}
